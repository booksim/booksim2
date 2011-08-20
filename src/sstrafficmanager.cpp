#include "booksim.hpp"
#include <sstream>
#include <math.h>
#include <fstream>
#include "sstrafficmanager.hpp"
#include "random_utils.hpp"
#include "memory/cacheState.h"

extern vector<int> has_message;

#define REPORT_INTERVAL 10000000
#define TRACE_INTERVAL 100000*4
int last_trace = 0;
bool perfect=false;
int perfect_latency=1;
queue<Flit* > retardq;
queue<int > retardt;
SSTrafficManager::SSTrafficManager(  const Configuration &config, const vector<BSNetwork *> & net )
  : TrafficManager(config, net)
{
  gWatchOut = &cout;
  vc_classes=0;
  nodes.resize(_limit);
  vc_ptrs = new int[ _sources ];
  memset(vc_ptrs,0, _sources *sizeof(int));
  flit_size = config.GetInt("channel_width");
  _network_time = 0;
  next_report = REPORT_INTERVAL;
  channel_width = config.GetInt("channel_width");/*bits*/


  packet_size_stat = new Stats( this, "packet_size", 1.0, 40 );

  for(int i = 0; i<Memory::CacheRequest::SignalOriginalAck+1; i++){
    type_pair_sent.push_back( new Stats(this, "type",1.0, _sources*_dests));
  }
  
  string trace_out = config.GetStr("trace_out");
  if(trace_out!=""){
    cout<<"Generating Trace"<<endl;
    _trace_out = new ofstream(trace_out.c_str());
    
  } else {
    _trace_out == NULL;
  }

  trace_queue.reserve(3*TRACE_INTERVAL);

  perfect=(config.GetInt("perfect_network")==1);
  perfect_latency=config.GetInt("perfect_latency");
}

SSTrafficManager::~SSTrafficManager( )
{
  delete[] vc_ptrs;
}


void SSTrafficManager::printPartialStats(int t, int left){
  
  for(int c = 0; c < _classes; ++c) {
    double cur_latency = _latency_stats[c]->Average( );
    int dmin;
    double min, avg;
    dmin = _ComputeStats( _accepted_flits[c], &avg, &min );
    double cur_accepted = avg;
    if(_stats_out) {
      *_stats_out << "lat_post"<<left<<"(" << c+1 << ") = " << cur_latency << ";" << endl
		  << "lat_hist_post"<<left<<"(" << c+1 << ",:) = " << *_latency_stats[c] << ";" << endl
		  << "frag_hist_post"<<left<<"(" << c+1 << ",:) = " << *_frag_stats[c] << ";" << endl
		  << "pair_sent_post"<<left<<"(" << c+1 << ",:) = [ ";
      for(unsigned int i = 0; i < _sources; ++i) {
	for(unsigned int j = 0; j < _dests; ++j) {
	  *_stats_out << _pair_latency[c][i*_dests+j]->NumSamples( ) << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "pair_lat_post"<<left<<"(" << c+1 << ",:) = [ ";
      for(unsigned int i = 0; i < _sources; ++i) {
	for(unsigned int j = 0; j < _dests; ++j) {
	  *_stats_out << _pair_latency[c][i*_dests+j]->Average( ) << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "pair_lat_post"<<left<<"(" << c+1 << ",:) = [ ";
      for(unsigned int i = 0; i < _sources; ++i) {
	for(unsigned int j = 0; j < _dests; ++j) {
	  *_stats_out << _pair_tlat[c][i*_dests+j]->Average( ) << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "sent_post"<<left<<"(" << c+1 << ",:) = [ ";
      for ( unsigned int d = 0; d < _dests; ++d ) {
	*_stats_out << _sent_flits[c][d]->Average( ) << " ";
      }
      *_stats_out << "];" << endl
		  << "accepted_post"<<left<<"(" << c+1 << ",:) = [ ";
      for ( unsigned int d = 0; d < _dests; ++d ) {
	*_stats_out << _accepted_flits[c][d]->Average( ) << " ";
      }
      *_stats_out << "];" << endl;
      *_stats_out << "inflight_post"<<left<<"(" << c+1 << ") = " << _total_in_flight_flits[c].size() << ";" << endl;
      *_stats_out << "network_time_post"<<left<<" = "<<_network_time<<";"<<endl;
      *_stats_out << "system_time_post"<<left<<" = "<<_time<<";"<<endl;
      *_stats_out << "packet_size_post"<<left<<" = " << *packet_size_stat<<";"<<endl;
    }
    if(_trace_out!=NULL){
      for(int i = 0; i<trace_queue.size(); i+=4){
	*_trace_out<<trace_queue[i]<<"\t"<<trace_queue[i+1]<<"\t"<<trace_queue[i+2]<<"\t"<<trace_queue[i+3]<<"\n";
      }
      trace_queue.clear();
    }
    
  }

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
    cout << "Average packet size = "<<packet_size_stat->Average()<<endl;
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
      *_stats_out << "packet_size = " << *packet_size_stat<<";"<<endl;
      
      for(int i = 0; i<Memory::CacheRequest::SignalOriginalAck+1; i++){
	*_stats_out <<"pair_sent_type"<<i<<" = "<<*type_pair_sent[i]<<";"<<endl;
      }
  

    }
    if(_trace_out!=NULL){
      for(int i = 0; i<trace_queue.size(); i+=4){
	*_trace_out<<trace_queue[i]<<"\t"<<trace_queue[i+1]<<"\t"<<trace_queue[i+2]<<"\t"<<trace_queue[i+3]<<"\n";
      }
      trace_queue.clear();
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

      //      assert(dynamic_cast<Memory::MemoryMessage*> (msg));
      /* this is a freaking gamble, but I assume all message network sees 
	 are memeory, this will crash and burn on active messages*/
      Memory::MemoryMessage* mmsg = (Memory::MemoryMessage*)msg;  


      if(_trace_out){
	trace_queue.push_back(_time-last_trace);
	trace_queue.push_back(dest);
	trace_queue.push_back(f->src);
	trace_queue.push_back(-((int)mmsg->_request->_type));
	last_trace = _time;
      }

      type_pair_sent[(int)mmsg->_request->_type]->AddSample(f->src*_dests+dest);

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
  packet_size_stat->AddSample(size);

  

  int ttime = time;
  int packet_destination=msg->Destination->id();



  if(packet_destination == source){
    msg->DeliveryTime = Time(_time);
    nodes[source]->messageIs(msg,(const Link*)1 /*bypass null check*/);
    nodes[source]->pop(-1);
    return;
  }
  bool record = false;

  if(_trace_out){
    Memory::MemoryMessage* mmsg = (Memory::MemoryMessage*)msg;  
    trace_queue.push_back(time-last_trace);
    trace_queue.push_back(source);
    trace_queue.push_back(packet_destination);
    trace_queue.push_back((int)mmsg->_request->_type);

    last_trace = time;
  }



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
  

  if(perfect){

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
      packet_payload[_cur_pid] = msg;
      _total_in_flight_flits[f->cl].insert(make_pair(f->id, f));
      if(record) {
	_measured_in_flight_flits[f->cl].insert(make_pair(f->id, f));
      }
      f->type = packet_type;
      f->head = true;
      f->tail = true;
      //packets are only generated to nodes smaller or equal to limit
      f->dest = packet_destination;
      ++_cur_pid;
      f->atime = time+perfect_latency;
      nodes[source]->pop(-1);
      retardq.push(f);
      retardt.push(time+perfect_latency);
      return;
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

  while(!retardq.empty() && retardt.front()<=_time){
    _RetireFlit(retardq.front(), retardq.front()->dest);
    retardq.pop();
    retardt.pop();
  }
  
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

  
  
  if(_trace_out!=NULL && trace_queue.size()>=TRACE_INTERVAL){
    for(int i = 0; i<trace_queue.size(); i+=4){
      *_trace_out<<trace_queue[i]<<"\t"<<trace_queue[i+1]<<"\t"<<trace_queue[i+2]<<"\t"<<trace_queue[i+3]<<"\n";
    }
    trace_queue.clear();
  }
  
  assert(_time);
  if(gTrace){
    cout<<"TIME "<<_time<<endl;
  }

}
