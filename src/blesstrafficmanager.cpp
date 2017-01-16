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
: TrafficManager(config, net), _golden_turn(0), _golden_in_flight(0)
{
    _retire_stats.resize(config.GetInt("classes"));
}

BlessTrafficManager::~BlessTrafficManager( ) {}

void BlessTrafficManager::_Step( )
{
    bool flits_in_flight = false;
    for(int c = 0; c < _classes; ++c) {
        flits_in_flight |= !_total_in_flight_flits[c].empty();
    }
    if(flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)){
        _deadlock_timer = 0;
        cout << "WARNING: Possible network deadlock.\n";
    }

    vector<map<int, Flit *> > flits(_subnets);

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
                    f->itime = _time;
                    f->pri = numeric_limits<int>::max() - f->itime;
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
                    
                    _net[subnet]->WriteFlit(f, n);

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
    int packet_destination = _traffic_pattern[cl]->dest(source);
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

        _total_in_flight_flits[f->cl].insert(make_pair(f->id, f));
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
        // switch( _pri_type ) {
        // case class_based:
        //     f->pri = _class_priority[cl];
        //     assert(f->pri >= 0);
        //     break;
        // case age_based:
        //     f->pri = numeric_limits<int>::max() - time;
        //     assert(f->pri >= 0);
        //     break;
        // case sequence_based:
        //     f->pri = numeric_limits<int>::max() - _packet_seq_no[source];
        //     assert(f->pri >= 0);
        //     break;
        // default:
        //     f->pri = 0;
        // }
        // f->pri = size - i;
        // if ( i == ( size - 1 ) ) { // Tail flit
        //     f->tail = true;
        // } else {
        //     f->tail = false;
        // }

        f->pri = numeric_limits<int>::max() - time;
        assert(f->pri >= 0);

        if(!_IsGoldenInFlight() && _IsGoldenTurn(source))
        {
            f->golden = 1;
            _golden_flits.push_back(f->id);
        }
        else
        {
            f->golden = 0;
        }

        f->vc  = -1;

        if ( f->watch ) {
            *gWatchOut << GetSimTime() << " | "
                       << "node" << source << " | "
                       << "Enqueuing flit " << f->id
                       << " (packet " << f->pid
                       << ") at time " << time
                       << " with golden status " << f->golden
                       << "." << endl;
        }

        _partial_packets[source][cl].push_back( f );
    }

    if(!_IsGoldenInFlight() && _IsGoldenTurn(source))
    {
        assert(!_golden_flits.empty());
        _UpdateGoldenStatus(source);
    }
}

void BlessTrafficManager::_UpdateGoldenStatus( int source )
{
    _golden_in_flight = 1;
    _golden_turn = (source + 1)%_nodes;
}

void BlessTrafficManager::_RetireFlit( Flit *f, int dest )
{
    int first = 0;

    _deadlock_timer = 0;

    assert(_total_in_flight_flits[f->cl].count(f->id) > 0);
    _total_in_flight_flits[f->cl].erase(f->id);

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
                    << ", frag = " << (f->atime - head->atime) - (f->id - head->id) // NB: In the spirit of solving problems using ugly hacks, we compute the packet length by taking advantage of the fact that the IDs of flits within a packet are contiguous.
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
    else
    {
        //  First flit of packet is being retired
        first = 1;
        Stat_Util *s = new Stat_Util;
        s->f = f;
        s->pending = (f->size) - 1;
        _retire_stats[f->cl].insert(make_pair(f->pid, s));
    }
    // if ( f->tail ) {
    //     Flit * head;
    //     if(f->head) {
    //         head = f;
    //     } else {
    //         map<int, Flit *>::iterator iter = _retired_packets[f->cl].find(f->pid);
    //         assert(iter != _retired_packets[f->cl].end());
    //         head = iter->second;
    //         _retired_packets[f->cl].erase(iter);
    //         assert(head->head);
    //         assert(f->pid == head->pid);
    //     }
    //     if ( f->watch ) {
    //         *gWatchOut << GetSimTime() << " | "
    //                    << "node" << dest << " | "
    //                    << "Retiring packet " << f->pid
    //                    << " (plat = " << f->atime - head->ctime
    //                    << ", nlat = " << f->atime - head->itime
    //                    << ", frag = " << (f->atime - head->atime) - (f->id - head->id) // NB: In the spirit of solving problems using ugly hacks, we compute the packet length by taking advantage of the fact that the IDs of flits within a packet are contiguous.
    //                    << ", src = " << head->src
    //                    << ", dest = " << head->dest
    //                    << ", golden = " << head->golden
    //                    << ")." << endl;
    //     }

    //     //code the source of request, look carefully, its tricky ;)
    //     if (f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST) {
    //         PacketReplyInfo* rinfo = PacketReplyInfo::New();
    //         rinfo->source = f->src;
    //         rinfo->time = f->atime;
    //         rinfo->record = f->record;
    //         rinfo->type = f->type;
    //         _repliesPending[dest].push_back(rinfo);
    //     } else {
    //         if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
    //             _requestsOutstanding[dest]--;
    //         } else if(f->type == Flit::ANY_TYPE) {
    //             _requestsOutstanding[f->src]--;
    //         }

    //     }

    //     // Only record statistics once per packet (at tail)
    //     // and based on the simulation state
    //     if ( ( _sim_state == warming_up ) || f->record ) {

    //         _hop_stats[f->cl]->AddSample( f->hops );

    //         if((_slowest_packet[f->cl] < 0) ||
    //            (_plat_stats[f->cl]->Max() < (f->atime - head->itime)))
    //             _slowest_packet[f->cl] = f->pid;
    //         _plat_stats[f->cl]->AddSample( f->atime - head->ctime);
    //         _nlat_stats[f->cl]->AddSample( f->atime - head->itime);
    //         _frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );

    //         if(_pair_stats){
    //             _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - head->ctime );
    //             _pair_nlat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - head->itime );
    //         }
    //     }

    //     if(f != head) {
    //         head->Free();
    //     }

    // }

    if(f->golden)
    {
        if ( _golden_flits.empty() )
        {
            ostringstream err;
            err << "Flit " << f->id << " claiming to be golden at " << dest;
            Error( err.str( ) );
        }
        vector<int>::iterator it = find( _golden_flits.begin(), _golden_flits.end(), f->id );
        assert( it != _golden_flits.end());
        _golden_flits.erase(it);
        if( _golden_flits.empty() )
        {
            _golden_in_flight = 0;
        }
    }

    if(!first) {
        f->Free();
    }
}