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

  nodes.resize(_limit);
  vc_classes =0;
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
    }
	
  
  }

}



void SSTrafficManager::_RetireFlit( Flit *f, int dest )
{

  //send to the output message buffer
  if(f){
    if(f->tail){
      SS_Network::Message* msg = packet_payload[f->pid].ptr();
      //printf("%ld Retiring packet %d from  %d to %d on vc %d\n",_time, msg->Id ,f->src, f->dest, msg->VirtualChannel);

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

  SS_Network::Message* msg = nodes[source]->message(-1);


  Flit::FlitType packet_type = Flit::ANY_TYPE;
  int size =ceil(float(msg->Size*8)/float(channel_width));/*Size is in bytes, channel width is in bits*/

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

  int subnetwork = DivisionAlgorithm(packet_type);
  
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
      //obliviously assign a packet to xy or yx route
      if(_use_xyyx){
	if(RandomInt(1)){
	  f->x_then_y = true;
	} else {
	  f->x_then_y = false;
	}
      }
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

    _partial_packets[source][cl][subnetwork].push_back( f );
  }
  ++_cur_pid;
  assert(_cur_pid);

  nodes[source]->pop(-1);
}



void SSTrafficManager::SSInject(){

  // Receive credits and inject new traffic
  for ( unsigned int input = 0; input < _sources; ++input ) {
    for (int i = 0; i < _duplicate_networks; ++i) {
      Credit * cred = _net[i]->ReadCredit( input );
      if ( cred ) {
        _buf_states[input][i]->ProcessCredit( cred );
        cred->Free();
      }
    }
    
    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (input,class)
      // that is currently empty
      if ( (_duplicate_networks > 1) || _partial_packets[input][c][0].empty() ) {
	if(has_message[input]!=0){
	  _GeneratePacket( input, 1/*avoid 0 check*/, 0/*no class*/, _time );
	}
      }
    }

    // Now, check partially issued packets to
    // see if they can be issued
    for (int i = 0; i < _duplicate_networks; ++i) {
      Flit * f = NULL;
      for (int c = _classes - 1; c >= 0; --c) {
	if ( !_partial_packets[input][c][i].empty( ) ) {
	  f = _partial_packets[input][c][i].front( );
	  if ( f->head && f->vc == -1) { // Find first available VC
	    
	    if ( _voqing ) {
	      if ( _buf_states[input][i]->IsAvailableFor( f->dest ) ) {
		f->vc = f->dest;
	      }
	    } else {
	      
	      if(_use_xyyx){
		f->vc = _buf_states[input][i]->FindAvailable( f->type ,f->x_then_y);
	      } else {
		f->vc = _buf_states[input][i]->FindAvailable( f->type );
	      }
	    }
	    
	    if ( f->vc != -1 ) {
	      _buf_states[input][i]->TakeBuffer( f->vc );
	    }
	  }
	  
	  if ( ( f->vc != -1 ) &&
	       ( !_buf_states[input][i]->IsFullFor( f->vc ) ) ) {
	    
	    _partial_packets[input][c][i].pop_front( );
	    _buf_states[input][i]->SendingFlit( f );
	    
	    if(_pri_type == network_age_based) {
	      f->pri = numeric_limits<int>::max() - _time;
	      assert(f->pri >= 0);
	    }
	    
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | "
			 << "node" << input << " | "
			 << "Injecting flit " << f->id
			 << " at time " << _time
			 << " with priority " << f->pri
			 << "." << endl;
	    }
	    
	    // Pass VC "back"
	    if ( !_partial_packets[input][c][i].empty( ) && !f->tail ) {
	      Flit * nf = _partial_packets[input][c][i].front( );
	      nf->vc = f->vc;
	    }
	    
	    ++_injected_flow[input];

	    break;

	  } else {
	    f = NULL;
	  }
	}
      }
      _net[i]->WriteFlit( f, input );
      if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	for(int c = 0; c < _classes; ++c) {
	  _sent_flits[c][input]->AddSample((f && (f->cl == c)) ? 1 : 0);
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
  /*
  if(flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)){
    _deadlock_timer = 0;
    cout << "WARNING: Possible network deadlock.\n";
  }
  */

  SSInject();

  //advance networks
  for (int i = 0; i < _duplicate_networks; ++i) {
    _net[i]->Evaluate( );
  }

  for (int i = 0; i < _duplicate_networks; ++i) {
    _net[i]->Update( );
  }
  


  for (int i = 0; i < _duplicate_networks; ++i) {
    // Eject traffic and send credits
    for ( unsigned int output = 0; output < _dests; ++output ) {
      Flit * f = _net[i]->ReadFlit( output );

      if ( f ) {
	++_ejected_flow[output];
	f->atime = _time;
        if ( f->watch ) {
	  *gWatchOut << GetSimTime() << " | "
		      << "node" << output << " | "
		      << "Ejecting flit " << f->id
		      << " (packet " << f->pid << ")"
		      << " from VC " << f->vc
		      << "." << endl;
	  *gWatchOut << GetSimTime() << " | "
		      << "node" << output << " | "
		      << "Injecting credit for VC " << f->vc << "." << endl;
        }
      
        Credit * cred = Credit::New(1);
        cred->vc[0] = f->vc;
        cred->vc_cnt = 1;
	cred->dest_router = f->from_router;
        _net[i]->WriteCredit( cred, output );
      
        if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	  for(int c = 0; c < _classes; ++c) {
	    _accepted_flits[c][output]->AddSample( (f && (f->cl == c)) ? 1 : 0 );
	  }

        _RetireFlit( f, output );

      } else {
        _net[i]->WriteCredit( 0, output );
        if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	  for(int c = 0; c < _classes; ++c) {
	    _accepted_flits[c][output]->AddSample( 0 );
	  }
      }
    }

    for(unsigned int j = 0; j < _routers; ++j) {
      _received_flow[i*_routers+j] += _router_map[i][j]->GetReceivedFlits();
      _sent_flow[i*_routers+j] += _router_map[i][j]->GetSentFlits();
      _router_map[i][j]->ResetFlitStats();
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
