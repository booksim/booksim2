#include "booksim.hpp"
#include <sstream>
#include <math.h>
#include <fstream>
#include "sstrafficmanager.hpp"
#include "random_utils.hpp"

extern vector<int> has_message;

#define REPORT_INTERVAL 100000
SSTrafficManager::SSTrafficManager(  const Configuration &config, const vector<BSNetwork *> & net )
  : TrafficManager(config, net)
{

  vc_classes=0;
  nodes.resize(_limit);
  vc_ptrs = new int[ _sources ];
  memset(vc_ptrs,0, _sources *sizeof(int));
  flit_size = config.GetInt("channel_width");
  _network_time = 0;
  next_report = REPORT_INTERVAL;
  channel_width = config.GetInt("channel_width");/*bits*/
}

SSTrafficManager::~SSTrafficManager( )
{
  delete[] vc_ptrs;
}


void SSTrafficManager::DisplayStats(){


  double max_latency_change = 0.0;
  double max_accepted_change = 0.0;

  for(int c = 0; c < _classes; ++c) {

    double cur_latency = _latency_stats[c]->Average( );
    int dmin;
    double min, avg;
    dmin = _ComputeStats( _accepted_flits[c], &avg, &min );
    double cur_accepted = avg;

    cout << "Class " << c+1 << ":" << endl;
    cout << "Number of packets = "<< _latency_stats[c]->NumSamples()<<endl;
    cout << "Minimum latency = " << _latency_stats[c]->Min( ) << endl;
    cout << "Average latency = " << cur_latency << endl;
    cout << "Maximum latency = " << _latency_stats[c]->Max( ) << endl;
    cout << "Average fragmentation = " << _frag_stats[c]->Average( ) << endl;
    cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
    cout << "Total in-flight flits = " << _total_in_flight_flits[c].size() << " (" << _measured_in_flight_flits[c].size() << " measured)" << endl;
    if(_stats_out) {
      *_stats_out << "lat(" << c+1 << ") = " << cur_latency << ";" << endl
		  << "lat_hist(" << c+1 << ",:) = " << *_latency_stats[c] << ";" << endl
		  << "frag_hist(" << c+1 << ",:) = " << *_frag_stats[c] << ";" << endl
		  << "pair_sent(" << c+1 << ",:) = [ ";
      for(unsigned int i = 0; i < _sources; ++i) {
	for(unsigned int j = 0; j < _dests; ++j) {
	  *_stats_out << _pair_latency[c][i*_dests+j]->NumSamples( ) << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "pair_lat(" << c+1 << ",:) = [ ";
      for(unsigned int i = 0; i < _sources; ++i) {
	for(unsigned int j = 0; j < _dests; ++j) {
	  *_stats_out << _pair_latency[c][i*_dests+j]->Average( ) << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "pair_lat(" << c+1 << ",:) = [ ";
      for(unsigned int i = 0; i < _sources; ++i) {
	for(unsigned int j = 0; j < _dests; ++j) {
	  *_stats_out << _pair_tlat[c][i*_dests+j]->Average( ) << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "sent(" << c+1 << ",:) = [ ";
      for ( unsigned int d = 0; d < _dests; ++d ) {
	*_stats_out << _sent_flits[c][d]->Average( ) << " ";
      }
      *_stats_out << "];" << endl
		  << "accepted(" << c+1 << ",:) = [ ";
      for ( unsigned int d = 0; d < _dests; ++d ) {
	*_stats_out << _accepted_flits[c][d]->Average( ) << " ";
      }
      *_stats_out << "];" << endl;
      *_stats_out << "inflight(" << c+1 << ") = " << _total_in_flight_flits[c].size() << ";" << endl;
      *_stats_out << "network_time = "<<_network_time<<";"<<endl;
      *_stats_out << "system_time = "<<_time<<";"<<endl;
    }
	
    
  }

}



void SSTrafficManager::_RetireFlit( Flit *f, int dest )
{

  //send to the output message buffer
  if(f){
    if(f->tail){
      SS_Network::Message* msg = packet_payload[f->pid].ptr();
      assert(msg);
      msg->DeliveryTime = Time(_time);
      nodes[dest]->messageIs(msg,(const Link*)1 /*bypass null check*/);
      packet_payload.erase(f->pid);
    }
  }
  TrafficManager::_RetireFlit(f, dest);
}


void SSTrafficManager::_GeneratePacket( int source, int stype, 
				      int cl, int time )
{
  assert(stype!=0);
  //refusing to generate packets for nodes greater than limit
  if(source >=_limit){
    return ;
  }

  //read the message, the argument should be which vc.
  SS_Network::Message* msg = nodes[source]->message(-1);


  Flit::FlitType packet_type = Flit::ANY_TYPE;
  /*Size is in bytes, channel width is in bits*/
  int size =ceil(float(msg->Size*8)/float(channel_width));

  int ttime = time;
  int packet_destination=msg->Destination->id();
  bool record = false;
  


  if ((packet_destination <0) || (packet_destination >= _dests)) {
    cerr << "Incorrect packet destination " << packet_destination
	 << " for stype " << packet_type
	 << "!" << endl;
    Error( "" );
  }

  record = true;

  int subnetwork = ((packet_type == Flit::ANY_TYPE) ? 
		    RandomInt(_subnets-1) :
		    _subnet_map[packet_type]);
  bool watch = gWatchOut && (_packets_to_watch.count(_cur_pid) > 0);
  
  if ( watch ) { 
    *gWatchOut << GetSimTime() << " | "
		<< "node" << source << " | "
		<< "Enqueuing packet " << _cur_pid
		<< " at time " << time
		<< "." << endl;
  }
  
  for ( int i = 0; i < size; ++i ) {
    Flit * f  = Flit::New();
    f->id     = _cur_id++;
    assert(_cur_id);
    f->pid    = _cur_pid;
    f->watch  = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
    f->subnetwork = subnetwork;
    f->src    = source;
    f->time   = time;
    f->ttime  = ttime;
    f->record = record;
    f->cl     = cl;
    if(i == size-1){
      packet_payload[_cur_pid] = msg;
    }
    _total_in_flight_flits[f->cl].insert(make_pair(f->id, f));
    if(record) {
      _measured_in_flight_flits[f->cl].insert(make_pair(f->id, f));
    }
    
    if(gTrace){
      cout<<"New Flit "<<f->src<<endl;
    }
    f->type = packet_type;

    if ( i == 0 ) { // Head flit
      f->head = true;
      //packets are only generated to nodes smaller or equal to limit
      f->dest = packet_destination;

    } else {
      f->head = false;
      f->dest = -1;
    }
    switch( _pri_type ) {
    case class_based:
      f->pri = cl;
      assert(f->pri >= 0);
      break;
    case age_based:
      f->pri = numeric_limits<int>::max() - (_replies_inherit_priority ? ttime : time);
      assert(f->pri >= 0);
      break;
    case sequence_based:
      f->pri = numeric_limits<int>::max() - _packets_sent[source];
      assert(f->pri >= 0);
      break;
    default:
      f->pri = 0;
    }
    if ( i == ( size - 1 ) ) { // Tail flit
      f->tail = true;
    } else {
      f->tail = false;
    }
    
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
  ++_cur_pid;
  assert(_cur_pid);

  nodes[source]->pop(-1);
}



void SSTrafficManager::SSInject(){
  
  for ( int input = 0; input < _limit; ++input ) {
    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (input,class)
      // that is currently empty
      if ( _partial_packets[input][c].empty() ) {
	if(has_message[input]!=0){
	  _GeneratePacket( input, 1/*avoid 0 check*/, 0/*no class*/, _time );
	}
      }
    }
  }
}



void SSTrafficManager::_Step(int t)
{

  _time = t;

  bool flits_in_flight = false;
  for(int c = 0; c < _classes; ++c) {
    flits_in_flight |= !_total_in_flight_flits[c].empty();
  }

 for ( int source = 0; source < _limit; ++source ) {
    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
      Credit * const c = _net[subnet]->ReadCredit( source );
      if ( c ) {
	_buf_states[source][subnet]->ProcessCredit(c);
	c->Free();
      }
    }
  }
 vector<map<int, Flit *> > flits(_subnets);
  
  for ( int subnet = 0; subnet < _subnets; ++subnet ) {
    for ( int dest = 0; dest < _limit; ++dest ) {
      Flit * const f = _net[subnet]->ReadFlit( dest );
      if ( f ) {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | "
		     << "node" << dest << " | "
		     << "Ejecting flit " << f->id
		     << " (packet " << f->pid << ")"
		     << " from VC " << f->vc
		     << "." << endl;
	}
	flits[subnet].insert(make_pair(dest, f));
      }
      if( ( _sim_state == warming_up ) || ( _sim_state == running ) ) {
	for(int c = 0; c < _classes; ++c) {
	  _accepted_flits[c][dest]->AddSample( (f && (f->cl == c)) ? 1 : 0 );
	}
      }
    }
    _net[subnet]->ReadInputs( );
  }

  SSInject();

  for(int source = 0; source < _limit; ++source) {
    Flit * f = NULL;
    for(map<int, pair<int, vector<int> > >::reverse_iterator iter = _class_prio_map.rbegin();
	iter != _class_prio_map.rend();
	++iter) {
      
      int const & base = iter->second.first;
      vector<int> const & classes = iter->second.second;
      int const count = classes.size();
      
      for(int j = 1; j <= count; ++j) {
	
	int const offset = (base + j) % count;
	int const c = classes[offset];
	
	if(!_partial_packets[source][c].empty()) {
	  f = _partial_packets[source][c].front();
	  assert(f);

	  int const subnet = f->subnetwork;

	  BufferState * const dest_buf = _buf_states[source][subnet];

	  if(f->head && f->vc == -1) { // Find first available VC
	    
	    OutputSet route_set;
	    _rf(NULL, f, 0, &route_set, true);
	    set<OutputSet::sSetElement> const & os = route_set.GetSet();
	    assert(os.size() == 1);
	    OutputSet::sSetElement const & se = *os.begin();
	    assert(se.output_port == 0);
	    int const & vc_start = se.vc_start;
	    int const & vc_end = se.vc_end;
	    int const vc_count = vc_end - vc_start + 1;
	    for(int i = 1; i <= vc_count; ++i) {
	      int const vc = vc_start + (_last_vc[source][subnet][c] + i) % vc_count;
	      if(dest_buf->IsAvailableFor(vc) && dest_buf->HasCreditFor(vc)) {
		f->vc = vc;
		break;
	      }
	    }
	    if(f->vc != -1) {
	      dest_buf->TakeBuffer(f->vc);
	      _last_vc[source][subnet][c] = f->vc - vc_start;
	    }
	  }
	  
	  if((f->vc != -1) && (dest_buf->HasCreditFor(f->vc))) {
	    
	    _partial_packets[source][c].pop_front();
	    dest_buf->SendingFlit(f);
	    
	    if(_pri_type == network_age_based) {
	      f->pri = numeric_limits<int>::max() - _time;
	      assert(f->pri >= 0);
	    }
	    
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | "
			 << "node" << source << " | "
			 << "Injecting flit " << f->id
			 << " into subnet " << subnet
			 << " at time " << _time
			 << " with priority " << f->pri
			 << "." << endl;
	    }
	    
	    // Pass VC "back"
	    if(!_partial_packets[source][c].empty() && !f->tail) {
	      Flit * nf = _partial_packets[source][c].front();
	      nf->vc = f->vc;
	    }
	    
	    ++_injected_flow[source];
	    
	    _net[subnet]->WriteFlit(f, source);
	    
	    iter->second.first = offset;
	    
	    break;
	    
	  } else {
	    f = NULL;
	  }
	}
      }
      if(f) {
	break;
      }
    }
    if(((_sim_mode != batch) && (_sim_state == warming_up)) || (_sim_state == running)) {
      for(int c = 0; c < _classes; ++c) {
	_sent_flits[c][source]->AddSample((f && (f->cl == c)) ? 1 : 0);
      }
    }
  }
  for(int subnet = 0; subnet < _subnets; ++subnet) {
    for(int dest = 0; dest < _limit; ++dest) {
      map<int, Flit *>::const_iterator iter = flits[subnet].find(dest);
      if(iter != flits[subnet].end()) {
	Flit * const & f = iter->second;
	++_ejected_flow[dest];
	f->atime = _time;
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | "
		     << "node" << dest << " | "
		     << "Injecting credit for VC " << f->vc 
		     << " into subnet " << subnet 
		     << "." << endl;
	}
	Credit * const c = Credit::New();
	c->vc.insert(f->vc);
	_net[subnet]->WriteCredit(c, dest);
	_RetireFlit(f, dest);
      }
    }
    flits[subnet].clear();
    _net[subnet]->Evaluate( );
    _net[subnet]->WriteOutputs( );
  }
  
  for (int subnet = 0; subnet < _subnets; ++subnet) {
    for(int router = 0; router < _routers; ++router) {
      _received_flow[subnet*_routers+router] += _router_map[subnet][router]->GetReceivedFlits();
      _sent_flow[subnet*_routers+router] += _router_map[subnet][router]->GetSentFlits();
      _router_map[subnet][router]->ResetFlitStats();
    }
  }

  _network_time++;
  
  if(_time>next_report){
    while(_time>next_report){
      printf("Booksim report System time %d\tnetwork time %d\n",_time, _network_time);
      next_report +=REPORT_INTERVAL;
    }
    DisplayStats();
  }
  assert(_time);
  if(gTrace){
    cout<<"TIME "<<_time<<endl;
  }

}
