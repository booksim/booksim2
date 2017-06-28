/*	Ameya: new traffic manager to handle bufferless networks
*/
#include <sstream>
#include <cmath>
#include <fstream>
#include <limits>
#include <cstdlib>
#include <ctime>

#include "blesstrafficmanager.hpp"
#include "random_utils.hpp"
#include "packet_reply_info.hpp"

BlessTrafficManager::BlessTrafficManager( const Configuration &config, 
					  const vector<Network *> & net )
: TrafficManager(config, net), _golden_turn(0), _golden_packet(-1), position(0)
{
    _golden_epoch = config.GetInt("k")*config.GetInt("n")*3;    //  3 -> 3-cycle router
    _retire_stats.resize(config.GetInt("classes"));
    _router_flits_in_flight.resize(_routers);

    _file_inject = config.GetInt("file_inject");
    _eoif = 1;
    if(_file_inject)
    {
        _eoif = 0;
        _inject_file = config.GetStr("inject_file");
        assert(_inject_file != "");
    }
}

BlessTrafficManager::~BlessTrafficManager( ) {}

void BlessTrafficManager::_Step( )
{
    if((_time == 0)&&(_file_inject))
    {
        _Read_File(position);
    }

    bool flits_in_flight = false;
    for(int c = 0; c < _classes; ++c) {
        flits_in_flight |= !_total_in_flight_flits[c].empty();
    }
    if(flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)){
        _deadlock_timer = 0;
        cout << "WARNING: Possible network deadlock.\n";
    }

    vector<map<int, Flit *> > flits(_subnets);

    if(GetSimTime()%_golden_epoch == 0)
        _UpdateGoldenStatus();     

    for ( int subnet = 0; subnet < _subnets; ++subnet )
    {
        for ( int n = 0; n < _nodes; ++n )
        {
            Flit * const f = _net[subnet]->ReadFlit( n );
            if ( f )
            {
                if(f->watch) {
                  *gWatchOut << GetSimTime() << " | "
                             << "node" << n << " | "
                             << "Ejecting flit " << f->id
                             << " (packet " << f->pid << ")"
                             << " from VC " << f->vc
                             << "." << endl;
                }
                flits[subnet].insert(make_pair(n, f));
                if((_sim_state == warming_up) || (_sim_state == running))
                {
                    ++_accepted_flits[f->cl][n];
                    // Update of _accepted_packets[f->cl][n] moved to _RetireFlit
                }
            }
        }
        _net[subnet]->ReadInputs( );
    }

    if ( !_empty_network ) {
        _Inject();
    }

    for(int subnet = 0; subnet < _subnets; ++subnet) {
        for(int n = 0; n < _nodes; ++n) {
            Flit * f = NULL;

            int const last_class = _last_class[n][subnet];

            int class_limit = _classes;

            for(int i = 1; i <= class_limit; ++i)
            {
                int const c = (last_class + i) % _classes;

                list<Flit *> & pp = _partial_packets[n][c];

                if(pp.empty()) {
                    continue;
                }

                f = pp.front();
                if( _net[subnet]->CheckInject(n) )
                {
                    assert(f->pri >= 0);

                    _last_class[n][subnet] = f->cl;
                    
                    if(f->watch) {
                        *gWatchOut << GetSimTime() << " | "
                                   << "node" << n << " | "
                                   << "Injecting flit " << f->id
                                   << " into subnet " << subnet
                                   << " at time " << _time
                                   << " with priority " << f->pri << " | "
                                   << " Destination " << f->dest
                                   << "." << endl;
                    }

                    if((_sim_state == warming_up) || (_sim_state == running)) {
                        ++_sent_flits[f->cl][n];
                        if(f->head) {
                            ++_sent_packets[f->cl][n];
                        }
                    }

#ifdef TRACK_FLOWS
                    ++_injected_flits[f->cl][n];
#endif
                    //  Edit 25/01/17
                    _router[subnet][n]->QueueFlit(f);

                    pp.pop_front();
                }
            }
        }
    }

    for(int subnet = 0; subnet < _subnets; ++subnet)
    {
        for(int n = 0; n < _nodes; ++n)
        {
            map<int, Flit *>::const_iterator iter = flits[subnet].find(n);
            if(iter != flits[subnet].end())
            {
                Flit * const f = iter->second;

                f->atime = _time;

#ifdef TRACK_FLOWS
                ++_ejected_flits[f->cl][n];
#endif

                _RetireFlit(f, n);
            }
        }
        flits[subnet].clear();
        _net[subnet]->Evaluate( );
        _net[subnet]->WriteOutputs( );
    }
    ++_time;
    assert(_time);
    if(gTrace)
    {
        cout<<"TIME "<<_time<<endl;
    }
}

void BlessTrafficManager::_GeneratePacket( int source, int stype,
                                      int cl, int time )
{
    assert(stype!=0);

    Flit::FlitType packet_type = Flit::ANY_TYPE;
    int size = _GetNextPacketSize(cl); //input size
    int pid = _cur_pid++;
    assert(_cur_pid);
    int packet_destination = ((_file_inject==1)? f_dest : _traffic_pattern[cl]->dest(source));  //  Nandan
    bool record = false;
    bool watch = gWatchOut && (_packets_to_watch.count(pid) > 0);
    if(_use_read_write[cl]){
        if(stype > 0) {
            if (stype == 1) {
                packet_type = Flit::READ_REQUEST;
                size = _read_request_size[cl];
            } else if (stype == 2) {
                packet_type = Flit::WRITE_REQUEST;
                size = _write_request_size[cl];
            } else {
                ostringstream err;
                err << "Invalid packet type: " << packet_type;
                Error( err.str( ) );
            }
        } else {
            PacketReplyInfo* rinfo = _repliesPending[source].front();
            if (rinfo->type == Flit::READ_REQUEST) {//read reply
                size = _read_reply_size[cl];
                packet_type = Flit::READ_REPLY;
            } else if(rinfo->type == Flit::WRITE_REQUEST) {  //write reply
                size = _write_reply_size[cl];
                packet_type = Flit::WRITE_REPLY;
            } else {
                ostringstream err;
                err << "Invalid packet type: " << rinfo->type;
                Error( err.str( ) );
            }
            packet_destination = rinfo->source;
            time = rinfo->time;
            record = rinfo->record;
            _repliesPending[source].pop_front();
            rinfo->Free();
        }
    }

    if ((packet_destination <0) || (packet_destination >= _nodes)) {
        ostringstream err;
        err << "Incorrect packet destination " << packet_destination
            << " for stype " << packet_type;
        Error( err.str( ) );
    }

    if ( ( _sim_state == running ) ||
         ( ( _sim_state == draining ) && ( time < _drain_time ) ) ) {
        record = _measure_stats[cl];
    }

    int subnetwork = ((packet_type == Flit::ANY_TYPE) ?
                      RandomInt(_subnets-1) :
                      _subnet[packet_type]);

    if ( watch ) {
        *gWatchOut << GetSimTime() << " | "
                   << "node" << source << " | "
                   << "Enqueuing packet " << pid
                   << " at time " << time
                   << "." << endl;
    }

    //  For fast location of golden flits
    vector<Flit *> pkt;
    for ( int i = 0; i < size; ++i ) {
        Flit * f  = Flit::New();
        f->id     = _cur_id++;
        assert(_cur_id);
        f->pid    = pid;
        f->watch  = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
        f->subnetwork = subnetwork;
        f->src    = source;
        f->ctime  = time;
        f->record = record;
        f->cl     = cl;
        f->size   = size;
        f->golden = 0;

        _total_in_flight_flits[f->cl].insert(make_pair(f->id, f));
        pkt.push_back( f );

        if(record) {
            _measured_in_flight_flits[f->cl].insert(make_pair(f->id, f));
        }

        if(gTrace){
            cout<<"New Flit "<<f->src<<endl;
        }
        f->type = packet_type;
        f->dest = packet_destination;
        if ( i == 0 ) { // Head flit
            f->head = true; 
        } else {
            f->head = false;
        }

        f->pri = numeric_limits<int>::max() - time;
        assert(f->pri >= 0);
        
        f->vc  = -1;

        if ( f->watch ) {
            *gWatchOut << GetSimTime() << " | "
                       << "node" << source << " | "
                       << "Enqueuing flit " << f->id
                       << " (packet " << f->pid
                       << ") at time " << time
                       << "." << endl;
        }

        _partial_packets[source][cl].push_back( f );
    }

    _router_flits_in_flight[source].insert(make_pair(pid, pkt));

    //  Nandan
    if(_file_inject)
        _Read_File(position);
}

void BlessTrafficManager::_UpdateGoldenStatus( )
{
    assert(GetSimTime()%_golden_epoch == 0);
    map<int, vector<Flit *> >::iterator iter = _router_flits_in_flight[_golden_turn].find(_golden_packet);
    
    // Ameya: check exclusion
    // if((iter->second).size()>0)
    //     getchar();
    
    _golden_turn = (_golden_turn + 1)%_routers;
    if(!_router_flits_in_flight[_golden_turn].empty())
    {
        iter = _router_flits_in_flight[_golden_turn].begin();
        _golden_packet = iter->first;
        vector<Flit *>& pkt = iter->second;
        vector<Flit *>::iterator flt;
        for(flt = pkt.begin(); flt != pkt.end(); ++flt)
        {
            (*flt)->golden = 1;
            if ( (*flt)->watch ) {
                *gWatchOut << GetSimTime() << " | "
                           << " BlessTrafficManager | "
                           << "Updating priority to golden for flit" << (*flt)->id
                           << " (packet " << (*flt)->pid
                           << ", src = " << (*flt)->src
                           << ", dest = " << (*flt)->dest
                           << ", golden = " << (*flt)->golden
                           << ", hops = " << (*flt)->hops
                           << ")." << endl;
            }
        }
    }
}

void BlessTrafficManager::_RetireFlit( Flit *f, int dest )
{
    int first = 0;

    _deadlock_timer = 0;

    assert(_total_in_flight_flits[f->cl].count(f->id) > 0);
    _total_in_flight_flits[f->cl].erase(f->id);
    
    assert(_router_flits_in_flight[f->src].count(f->pid) > 0);
    map<int, vector<Flit *> >::iterator iter1 = _router_flits_in_flight[f->src].find(f->pid);
    vector<Flit *>& pkt = iter1->second;
    vector<Flit *>::iterator flt;
    for(flt = pkt.begin(); flt != pkt.end(); ++flt)
    {
        if( (*flt)->id == f->id)
        {
            pkt.erase(flt);
            break;
        }
    }
    if(pkt.empty())
    {
        _router_flits_in_flight[f->src].erase(iter1);
    }

    if(f->record) {
        assert(_measured_in_flight_flits[f->cl].count(f->id) > 0);
        _measured_in_flight_flits[f->cl].erase(f->id);
    }

    if ( f->watch ) {
        *gWatchOut << GetSimTime() << " | "
                   << "node" << dest << " | "
                   << "Retiring flit " << f->id
                   << " (packet " << f->pid
                   << ", src = " << f->src
                   << ", dest = " << f->dest
                   << ", golden = " << f->golden
                   << ", hops = " << f->hops
                   << ", flat = " << f->atime - f->itime
                   << ")." << endl;
    }

    if ( f->dest != dest ) {
        ostringstream err;
        err << "Flit " << f->id << " arrived at incorrect output " << dest;
        Error( err.str( ) );
    }

    if((_slowest_flit[f->cl] < 0) ||
       (_flat_stats[f->cl]->Max() < (f->atime - f->itime)))
        _slowest_flit[f->cl] = f->id;
    _flat_stats[f->cl]->AddSample( f->atime - f->itime);
    if(_pair_stats){
        _pair_flat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->itime );
    }

    map<int, Stat_Util *>::iterator iter = _retire_stats[f->cl].find(f->pid);
    if( iter == _retire_stats[f->cl].end() )
    {
        //  First flit of packet is being retired
        first = 1;
        Stat_Util *s = new Stat_Util;
        s->f = f;
        s->pending = (f->size) - 1;
        if(s->pending==0)
        {
            if ( f->watch ) {
                *gWatchOut << GetSimTime() << " | "
                    << "node" << dest << " | "
                    << "Retiring packet " << f->pid
                    << " (plat = " << f->atime - f->ctime
                    << ", nlat = " << f->atime - f->itime
                    << ", frag = 0"
                    << ", src = " << f->src
                    << ", dest = " << f->dest
                    << ", golden = " << f->golden
                    << ")." << endl;
            }

            // code the source of request, look carefully, its tricky ;)
            if (f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST) {
                PacketReplyInfo* rinfo = PacketReplyInfo::New();
                rinfo->source = f->src;
                rinfo->time = f->atime;
                rinfo->record = f->record;
                rinfo->type = f->type;
                _repliesPending[dest].push_back(rinfo);
            } else {
                if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
                    _requestsOutstanding[dest]--;
                } else if(f->type == Flit::ANY_TYPE) {
                    _requestsOutstanding[f->src]--;
                }
            }

            // Only record statistics once per packet (at tail)
            // and based on the simulation state
            if ( ( _sim_state == warming_up ) || f->record ) {

                _hop_stats[f->cl]->AddSample( f->hops );

                if((_slowest_packet[f->cl] < 0) ||
                   (_plat_stats[f->cl]->Max() < (f->atime - f->itime)))
                    _slowest_packet[f->cl] = f->pid;
                _plat_stats[f->cl]->AddSample( f->atime - f->ctime);
                _nlat_stats[f->cl]->AddSample( f->atime - f->itime);
                _frag_stats[f->cl]->AddSample( 0 );

                if(_pair_stats){
                    _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->ctime );
                    _pair_nlat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->itime );
                }
            }

            ++_accepted_packets[f->cl][dest];   //  Ameya: Moved here from _Step()

            //  To ensure flit is freed
            first = 0;
        }
        else
        {
            _retire_stats[f->cl].insert(make_pair(f->pid, s));
        }
    }
    if( iter != _retire_stats[f->cl].end() )
    {
        Stat_Util *prime = iter->second;
        prime->pending--;
        if(prime->pending == 0)
        {
            //  Entire packet has arrived
            Flit * head = prime->f;
            free(prime);
            _retire_stats[f->cl].erase(iter);
            if ( f->watch ) {
                *gWatchOut << GetSimTime() << " | "
                    << "node" << dest << " | "
                    << "Retiring packet " << f->pid
                    << " (plat = " << f->atime - head->ctime
                    << ", nlat = " << f->atime - head->itime
                    << ", frag = " << (f->atime - head->atime) - (f->size)
                    << ", src = " << head->src
                    << ", dest = " << head->dest
                    << ", golden = " << head->golden
                    << ")." << endl;
            }
            // code the source of request, look carefully, its tricky ;)
            if (f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST) {
                PacketReplyInfo* rinfo = PacketReplyInfo::New();
                rinfo->source = f->src;
                rinfo->time = f->atime;
                rinfo->record = f->record;
                rinfo->type = f->type;
                _repliesPending[dest].push_back(rinfo);
            } else {
                if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
                    _requestsOutstanding[dest]--;
                } else if(f->type == Flit::ANY_TYPE) {
                    _requestsOutstanding[f->src]--;
                }
            }

            // Only record statistics once per packet (at tail)
            // and based on the simulation state
            if ( ( _sim_state == warming_up ) || f->record ) {

                _hop_stats[f->cl]->AddSample( f->hops );

                if((_slowest_packet[f->cl] < 0) ||
                   (_plat_stats[f->cl]->Max() < (f->atime - head->itime)))
                    _slowest_packet[f->cl] = f->pid;
                _plat_stats[f->cl]->AddSample( f->atime - head->ctime);
                _nlat_stats[f->cl]->AddSample( f->atime - head->itime);
                _frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );

                if(_pair_stats){
                    _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - head->ctime );
                    _pair_nlat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - head->itime );
                }
            }
            if(f != head) {
                head->Free();
            }
            ++_accepted_packets[f->cl][dest];   //  Ameya: Moved here from _Step()        
        }
    }
    
    if(!first) {
        f->Free();
    }
}

void BlessTrafficManager::_Inject()
{
    if(_file_inject)
    {
        for ( int input = 0; input < _nodes; ++input ) {
            for ( int c = 0; c < _classes; ++c ) {
                int stype = _IssuePacket( input, request_type, c );

                if ( stype != 0 ) { //generate a packet
                    _GeneratePacket( input, stype, c, _time);
                }
                
                if ( ( _sim_state == draining ) &&
                     ( _qtime[input][c] > _drain_time ) ) {
                    _qdrained[input][c] = true;
                }
            }
        }
    }
    else
    {
        TrafficManager::_Inject();
    }
}

int BlessTrafficManager::_IssuePacket( int source, char request_type, int cl)
{
    assert(_file_inject==1);
    int result = 0;
    if((_time == f_time)&&(source == f_source))
    {
        if(request_type == 'r')     //  READ_REQUEST
        {
            result = 1;
            _requestsOutstanding[source]++;
        }
        else    //  assume WRITE_REQUEST
        {
            result = 2;
            _requestsOutstanding[source]++;
        }
    }
    else if(_use_read_write[cl]){       //  no allocated packet in inject file
        if (!_repliesPending[source].empty()) {
            if(_repliesPending[source].front()->time <= _time) {
                result = -1;
            }
        }
    }

    if(result != 0) {
        _packet_seq_no[source]++;
    }
    return result;
}

void BlessTrafficManager::_Read_File( int position )
{
    ifstream spec;
    spec.open(_inject_file.c_str(), std::ios::in);
    string a;
    // string b;
    int j;
    spec.seekg(position);
    for(j = 0; j < 4; j++)
    {
        if(j == 0)
            spec >> f_time;
        if(j == 1)
            spec >> a;
        if(j == 2)
            spec >> request_type;
        if(j == 3)
            spec >> f_source;
    }                                   
    position = spec.tellg();
    if( spec.eof() )
    {
        // check
        _eoif = 1;
    }
    // string::iterator itb = b.begin(); 
    // for (string::iterator ita=a.begin() + 2; ita!=a.end(); ++ita)
    // {
    //     *itb = *ita;
    //     itb++; 
    // }
    f_dest = Calculate_Dest( a ); // check this
    spec.close();
}

int BlessTrafficManager::Calculate_Dest( string address )
{
    return 0;
}