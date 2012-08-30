// $Id$

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this 
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "booksim.hpp"
#include <sstream>
#include <math.h>
#include <pthread.h>
#include <fstream>
#include "trafficmanager.hpp"
#include "random_utils.hpp" 

//batched time-mode, know what you are doing
bool timed_mode = false;

TrafficManager::TrafficManager( const Configuration &config, Network **net )
  : Module( 0, "traffic_manager" )
{
  
pthread_barrier_t barr;


  int s;
  ostringstream tmp_name;
  string sim_type, priority;
  
  _net    = net;
  _cur_id = 0;

  _sources = _net[0]->NumSources( );
  _dests   = _net[0]->NumDests( );
  
  //nodes higher than limit do not produce or receive packets
  //for default limit = sources

  _limit = config.GetInt( "limit" );
  if(_limit == 0){
    _limit = _sources;
  }
  assert(_limit<=_sources);
 
  duplicate_networks = config.GetInt("physical_subnetworks");
 
  // ============ Message priorities ============ 

  config.GetStr( "priority", priority );

  _classes = 1;

  if ( priority == "class" ) {
    _classes  = 2;
    _pri_type = class_based;
  } else if ( priority == "age" ) {
    _pri_type = age_based;
  } else if ( priority == "none" ) {
    _pri_type = none;
  } else {
    Error( "Unknown priority " + priority );
  }

  // ============ Injection VC states  ============ 

  _buf_states = new BufferState ** [_sources];

  for ( s = 0; s < _sources; ++s ) {
    tmp_name << "terminal_buf_state_" << s;
    _buf_states[s] = new BufferState * [duplicate_networks];
    for (int a = 0; a < duplicate_networks; ++a) {
      _buf_states[s][a] = new BufferState( config, this, tmp_name.str( ) );
    }
    tmp_name.seekp( 0, ios::beg );
  }


  // ============ Injection queues ============ 

  _time               = 0;
  _warmup_time        = -1;
  _drain_time         = -1;
  _empty_network      = false;

  _measured_in_flight = 0;
  _total_in_flight    = 0;

  _qtime              = new int * [_sources];
  _qdrained           = new bool * [_sources];
  _partial_packets    = new list<Flit *> ** [_sources];

  for ( s = 0; s < _sources; ++s ) {
    _qtime[s]           = new int [_classes];
    _qdrained[s]        = new bool [_classes];
    _partial_packets[s] = new list<Flit *> * [_classes];
    for (int a = 0; a < _classes; ++a)
      _partial_packets[s][a] = new list<Flit *> [duplicate_networks];
  }

  _voqing = config.GetInt( "voq" );
  if ( _voqing ) {
    _use_lagging = false;
  } else {
    _use_lagging = true;
  }

  _read_request_size = config.GetInt("read_request_size");
  _read_reply_size = config.GetInt("read_reply_size");
  _write_request_size = config.GetInt("write_request_size");
  _write_reply_size = config.GetInt("write_reply_size");
  if(_use_read_write) {
    _packet_size = (_read_request_size + _read_reply_size +
		    _write_request_size + _write_reply_size) / 2;
  }
  else
    _packet_size = config.GetInt( "const_flits_per_packet" );

  _packets_sent = new int [_sources];
  _batch_size = config.GetInt( "batch_size" );
  _repliesPending = new list<int> [_sources];
  _requestsOutstanding = new int [_sources];
  _maxOutstanding = config.GetInt ("max_outstanding_requests");  

  if ( _voqing ) {
    _voq         = new list<Flit *> * [_sources];
    _active_list = new list<int> [_sources];
    _active_vc   = new bool * [_sources];
  }

  for ( s = 0; s < _sources; ++s ) {
    if ( _voqing ) {
      _voq[s]       = new list<Flit *> [_dests];
      _active_vc[s] = new bool [_dests];
    }
  }

  // ============ Statistics ============ 

  _latency_stats   = new Stats * [_classes];
  _overall_latency = new Stats * [_classes];

  for ( int c = 0; c < _classes; ++c ) {
    tmp_name << "latency_stat_" << c;
    _latency_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    tmp_name.seekp( 0, ios::beg );

    tmp_name << "overall_latency_stat_" << c;
    _overall_latency[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    tmp_name.seekp( 0, ios::beg );  
  }

  _pair_latency = new Stats * [_dests];
  _hop_stats    = new Stats( this, "hop_stats", 1.0, 20 );;

  _accepted_packets = new Stats * [_dests];

  _overall_accepted     = new Stats( this, "overall_acceptance" );
  _overall_accepted_min = new Stats( this, "overall_min_acceptance" );

  for ( int i = 0; i < _dests; ++i ) {
    tmp_name << "pair_stat_" << i;
    _pair_latency[i] = new Stats( this, tmp_name.str( ), 1.0, 250 );
    tmp_name.seekp( 0, ios::beg );

    tmp_name << "accepted_stat_" << i;
    _accepted_packets[i] = new Stats( this, tmp_name.str( ) );
    tmp_name.seekp( 0, ios::beg );    
  }

  deadlock_counter = 1;

  // ============ Simulation parameters ============ 

  if(config.GetInt( "injection_rate_uses_flits" )) {
    _flit_rate = config.GetFloat( "injection_rate" ); 
    _load = _flit_rate / _packet_size;
  } else {
    _load = config.GetFloat( "injection_rate" ); 
    _flit_rate = _load * _packet_size;
  }

  _total_sims = config.GetInt( "sim_count" );

  _internal_speedup = config.GetFloat( "internal_speedup" );
  _partial_internal_cycles = new float[duplicate_networks];



  _traffic_function  = GetTrafficFunction( config );
  _routing_function  = GetRoutingFunction( config );
  _injection_process = GetInjectionProcess( config );

  config.GetStr( "sim_type", sim_type );

  if ( sim_type == "latency" ) {
    _sim_mode = latency;
  } else if ( sim_type == "throughput" ) {
    _sim_mode = throughput;
  }  else if ( sim_type == "batch" ) {
    _sim_mode = batch;
  }  else if (sim_type == "timed_batch"){
    _sim_mode = batch;
    timed_mode = true;
  }
  else {
    Error( "Unknown sim_type " + sim_type );
  }

  _sample_period = config.GetInt( "sample_period" );
  _max_samples    = config.GetInt( "max_samples" );
  _warmup_periods = config.GetInt( "warmup_periods" );
  _latency_thres = config.GetFloat( "latency_thres" );
  _include_queuing = config.GetInt( "include_queuing" );

  _print_csv_results = config.GetInt( "print_csv_results" );
  config.GetStr( "traffic", _traffic ) ;
  _drain_measured_only = config.GetInt( "drain_measured_only" );
  _LoadWatchList();
}

TrafficManager::~TrafficManager( )
{
  for ( int s = 0; s < _sources; ++s ) {
    delete [] _qtime[s];
    delete [] _qdrained[s];
    for (int a = 0; a < duplicate_networks; ++a) {
      delete _buf_states[s][a];
    }
    if ( _voqing ) {
      delete [] _voq[s];
      delete [] _active_vc[s];
    }
    for (int a = 0; a < _classes; ++a) {
      delete [] _partial_packets[s][a];
    }
    delete [] _partial_packets[s];
    delete [] _buf_states[s];
  }
  if ( _voqing ) {
    delete [] _voq;
    delete [] _active_vc;
  }
  delete [] _buf_states;
  delete [] _qtime;
  delete [] _qdrained;
  delete [] _partial_packets;

  for ( int c = 0; c < _classes; ++c ) {
    delete _latency_stats[c];
    delete _overall_latency[c];
  }


  delete [] _latency_stats;
  delete [] _overall_latency;

  delete _hop_stats;
  delete _overall_accepted;
  delete _overall_accepted_min;

  for ( int i = 0; i < _dests; ++i ) {
    delete _accepted_packets[i];
    delete _pair_latency[i];
  }

  delete [] _accepted_packets;
  delete [] _pair_latency;
  delete [] _partial_internal_cycles;
}


// Decides which subnetwork the flit should go to.
// Is now called once per packet.
// This should change according to number of duplicate networks
int TrafficManager::DivisionAlgorithm (int packet_type) {

  if(packet_type == Flit::ANY_TYPE) {
    static unsigned short counter = 0;
    return (counter++)%duplicate_networks; // Even distribution.
  } else {
    switch(duplicate_networks) {
    case 1:
      return 0;
    case 2:
      switch(packet_type) {
      case Flit::WRITE_REQUEST:
      case Flit::READ_REQUEST:
        return 1;
      case Flit::WRITE_REPLY:
      case Flit::READ_REPLY:
        return 0;
      default:
	assert(false);
	return -1;
      }
    case 4:
      switch(packet_type) {
      case Flit::WRITE_REQUEST:
        return 0;
      case Flit::READ_REQUEST:
        return 1;
      case Flit::WRITE_REPLY:
        return 2;
      case Flit::READ_REPLY:
        return 3;
      default:
	assert(false);
	return -1;
      }
    default:
      assert(false);
      return -1;
    }
  }
}

Flit *TrafficManager::_NewFlit( )
{
  Flit *f;
  //the constructor should initialize everything
  f = new Flit();
  f->id    = _cur_id;
  _in_flight[_cur_id] = true;
  if(flits_to_watch.size()!=0   && 
     flits_to_watch.find(_cur_id)!=flits_to_watch.end()){
    f->watch =true;
    map<int, bool>::iterator match;
    match = flits_to_watch.find( f->id );
    flits_to_watch.erase(match);
  }
  ++_cur_id;
  return f;
}

void TrafficManager::_RetireFlit( Flit *f, int dest )
{
  static int sample_num = 0;
  deadlock_counter = 1;

  map<int, bool>::iterator match;

  match = _in_flight.find( f->id );

  if ( match != _in_flight.end( ) ) {
    if ( f->watch ) {
      cout << "Matched flit ID = " << f->id << endl;
    }
    _in_flight.erase( match );
  } else {
    cout << "Unmatched flit! ID = " << f->id << endl;
    Error( "" );
  }
  
  if ( f->watch ) { 
    cout << "Ejecting flit " << f->id 
	 << ",  lat = " << _time - f->time
	 << ", src = " << f->src 
	 << ", dest = " << f->dest << endl;
  }

  if ( f->head && ( f->dest != dest ) ) {
    cout << "At output " << dest << endl;
    cout << *f;
    Error( "Flit arrived at incorrect output" );
  }

  if ( f->tail ) {
    _total_in_flight--;
    if ( _total_in_flight < 0 ) {
      Error( "Total in flight count dropped below zero!" );
    }
    
    //code the source of request, look carefully, its tricky ;)
    if (f->type == Flit::READ_REQUEST) {
      Packet_Reply* temp = new Packet_Reply;
      temp->source = f->src;
      temp->time = _time;
      temp->type = f->type;
      _repliesDetails[f->id] = temp;
      _repliesPending[dest].push_back(f->id);
    } else if (f->type == Flit::WRITE_REQUEST) {
      Packet_Reply* temp = new Packet_Reply;
      temp->source = f->src;
      temp->time = _time;
      temp->type = f->type;
      _repliesDetails[f->id] = temp;
      _repliesPending[dest].push_back(f->id);
    } else if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
      //received a reply
      _requestsOutstanding[dest]--;
    } else if(f->type == Flit::ANY_TYPE && _sim_mode == batch  ){
      //received a reply
      _requestsOutstanding[f->src]--;
    }


    // Only record statistics once per packet (at tail)
    // and based on the simulation state1
    if ( ( _sim_state == warming_up ) || f->record ) {
      
      _hop_stats->AddSample( f->hops );

      switch( _pri_type ) {
      case class_based:
	_latency_stats[f->pri]->AddSample( _time - f->time );
	break;
      case age_based: // fall through
      case none:
	_latency_stats[0]->AddSample( _time - f->time);
	break;
      }
      ++sample_num;
   
      if ( f->src == 0 ) {
	_pair_latency[dest]->AddSample( _time - f->time);
      }
      
      if ( f->record ) {
	_measured_in_flight--;
	if ( _measured_in_flight < 0 ){ 
	  Error( "Measured in flight count dropped below zero!" );
	}
      }
    }
  }
  delete f;
}

int TrafficManager::_IssuePacket( int source, int cl ) const
{
  int result;
  int pending_time = INT_MAX; //reset to maxtime+1
  if(_sim_mode == batch){ //batch mode
    if(_use_read_write){ //read write packets
      //check queue for waiting replies.
      //check to make sure it is on time yet
      if (!_repliesPending[source].empty()) {
	result = _repliesPending[source].front();
	pending_time = (_repliesDetails.find(result)->second)->time;
      }
      if (pending_time<=_qtime[source][0]) {
	result = _repliesPending[source].front();
	_repliesPending[source].pop_front();
	
      } else if ((_packets_sent[source] >= _batch_size && !timed_mode) || 
		 (_requestsOutstanding[source] >= _maxOutstanding)) {
	result = 0;
      } else {
	
	//coin toss to determine request type.
	result = -1;
	
	if (RandomFloat() < 0.5) {
	  result = -2;
	}
	
	_packets_sent[source]++;
	_requestsOutstanding[source]++;
      } 
    } else { //normal
      if ((_packets_sent[source] >= _batch_size && !timed_mode) || 
		 (_requestsOutstanding[source] >= _maxOutstanding)) {
	result = 0;
      } else {
	result = gConstPacketSize;
	_packets_sent[source]++;
	//here is means, how many flits can be waiting in the queue
	_requestsOutstanding[source]++;
      } 
    } 
  } else { //injection rate mode
    if(_use_read_write){ //use read and write
      //check queue for waiting replies.
      //check to make sure it is on time yet
      if (!_repliesPending[source].empty()) {
	result = _repliesPending[source].front();
	pending_time = (_repliesDetails.find(result)->second)->time;
      }
      if (pending_time<=_qtime[source][0]) {
	result = _repliesPending[source].front();
	_repliesPending[source].pop_front();
      } else {
	result = _injection_process( source, _load );
	//produce a packet
	if(result){
	  //coin toss to determine request type.
	  result = -1;
	
	  if (RandomFloat() < 0.5) {
	    result = -2;
	  }
	}
      } 
    } else { //normal mode
      return _injection_process( source, _load );
    } 
  }
  return result;
}

void TrafficManager::_GeneratePacket( int source, int stype, 
				      int cl, int time )
{
  assert(stype!=0);
  Flit *f;
  bool record;
  Flit::FlitType packet_type;
  int size; //input size 
  int packet_destination = -1;

  //refusing to generate packets for nodes greater than limit
  if(source >=_limit){
    return ;
  }

  if(_use_read_write){
    if (stype ==-1) {
      packet_type = Flit::READ_REQUEST;
      size = _read_request_size;
      packet_destination = _traffic_function( source, _limit );
    }
    else if (stype == -2) {
      packet_type = Flit::WRITE_REQUEST;
      size = _write_request_size;
      packet_destination = _traffic_function( source, _limit );
    }
    else  {
      Packet_Reply* temp = _repliesDetails.find(stype)->second;
      
      if (temp->type == Flit::READ_REQUEST) {//read reply
	size = _read_reply_size;
	packet_destination = temp->source;
	packet_type = Flit::READ_REPLY;
	time = temp->time;
      } else {  //write reply
	size = _write_reply_size;
	packet_destination = temp->source;
	packet_type = Flit::WRITE_REPLY;
	time = temp->time;
      }

      _repliesDetails.erase(_repliesDetails.find(stype));
      delete temp;
    }
  } else {
    //use uniform packet size
    packet_type = Flit::ANY_TYPE;
    size =  gConstPacketSize;
    packet_destination = _traffic_function( source, _limit );
  }



  if ((packet_destination <0) || (packet_destination >= _net[0]->NumDests())) {
    cout << "Packet destination " << packet_destination  << " for stype " <<packet_type << endl;
    Error("Incorrect destination");
  }

  if ( ( _sim_state == running ) ||
       ( ( _sim_state == draining ) && ( time < _drain_time ) ) ) {
    ++_measured_in_flight;
    record = true;
  } else {
    record = false;
  }
  ++_total_in_flight;

  sub_network = DivisionAlgorithm(packet_type);

  for ( int i = 0; i < size; ++i ) {
    f = _NewFlit( );
    f->subnetwork = sub_network;
    f->src    = source;
    f->time   = time;
    f->record = record;
    
    if(_trace || f->watch){
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
      f->pri = cl; break;
    case age_based://fall through
    case none:
      f->pri = 0; break;
    }

    if ( i == ( size - 1 ) ) { // Tail flit
      f->tail = true;
    } else {
      f->tail = false;
    }
    
    f->vc  = -1;

    if ( f->watch ) { 
      cout << "Generating flit at time " << time << endl;
      cout << *f;
    }

    _partial_packets[source][cl][sub_network].push_back( f );
  }
}





void TrafficManager::_FirstStep( )
{  
  // Ensure that all outputs are defined before starting simulation
  for (int i = 0; i < duplicate_networks; ++i) { 
    _net[i]->WriteOutputs( );
  
    for ( int output = 0; output < _net[i]->NumDests( ); ++output ) {
      _net[i]->WriteCredit( 0, output );
    }
  }
}

void TrafficManager::_BatchInject(){

  short ** class_array;
  class_array = new short* [duplicate_networks];
  for (int i=0; i < duplicate_networks; ++i) {
    class_array[i] = new short [_classes];
    memset(class_array[i], 0, sizeof(short)*_classes);
  }
  Flit   *f, *nf;
  Credit *cred;
  int    psize;
  // Receive credits and inject new traffic
  for ( int input = 0; input < _net[0]->NumSources( ); ++input ) {
    for (int i = 0; i < duplicate_networks; ++i) {
      cred = _net[i]->ReadCredit( input );
      if ( cred ) {
        _buf_states[input][i]->ProcessCredit( cred );
        delete cred;
      }
    }
    
    bool write_flit    = false;
    int  highest_class = 0;
    bool generated;

    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (input,class)
      // that is currently empty
      if ( (duplicate_networks > 1) || _partial_packets[input][c][0].empty() ) {
	// For multiple networks, always try. Hard to check for empty queue - many subnetworks potentially.
	generated = false;
	  
	if ( !_empty_network ) {
	  while( !generated && ( _qtime[input][c] <= _time ) ) {
	    psize = _IssuePacket( input, c );

	    if ( psize ) {
	      _GeneratePacket( input, psize, c, 
			       _include_queuing==1 ? 
			       _qtime[input][c] : _time );
	      generated = true;
	    }
	    ++_qtime[input][c];
	  }
	  
	  if ( ( _sim_state == draining ) && 
	       ( _qtime[input][c] > _drain_time ) ) {
	    _qdrained[input][c] = true;
	  }
	}
	if ( generated ) {
	  //highest_class = c;
	  class_array[sub_network][c]++; // One more packet for this class.
	}
      }
    }

    // Now, check partially issued packets to
    // see if they can be issued
    for (int i = 0; i < duplicate_networks; ++i) {
      write_flit = false;
      highest_class = 0;
     // Now just find which is the highest_class.
      for (short c = _classes - 1; c >= 0; --c) {
        if (class_array[i][c]) {
          highest_class = c;
          break;
        }
      } 
      if ( !_partial_packets[input][highest_class][i].empty( ) ) {
        f = _partial_packets[input][highest_class][i].front( );
        if ( f->head && f->vc == -1) { // Find first available VC

	  f->vc = _buf_states[input][i]->FindAvailable( );
	  if ( f->vc != -1 ) {
	    _buf_states[input][i]->TakeBuffer( f->vc );
	  }
        }

        if ( ( f->vc != -1 ) &&
	     ( !_buf_states[input][i]->IsFullFor( f->vc ) ) ) {

	  _partial_packets[input][highest_class][i].pop_front( );
	  _buf_states[input][i]->SendingFlit( f );
	  write_flit = true;

	  // Pass VC "back"
	  if ( !_partial_packets[input][highest_class][i].empty( ) && !f->tail ) {
	    nf = _partial_packets[input][highest_class][i].front( );
	    nf->vc = f->vc;
	  }
        }
      }
      _net[i]->WriteFlit( write_flit ? f : 0, input );
      if (write_flit) {
        if (f->tail) // If a tail flit, reduce the number of packets of this class.
          class_array[i][highest_class]--;
      }
    }
  }
  for (int i=0; i < duplicate_networks; ++i) {
    delete [] class_array[i];
  }
  delete [] class_array;
}


void TrafficManager::_NormalInject(){
  short ** class_array;
  class_array = new short* [duplicate_networks];
  for (int i=0; i < duplicate_networks; ++i) {
    class_array[i] = new short [_classes];
    memset(class_array[i], 0, sizeof(short)*_classes);
  }
  Flit   *f, *nf;
  Credit *cred;
  int    psize;
  // Receive credits and inject new traffic
  for ( int input = 0; input < _net[0]->NumSources( ); ++input ) {
    for (int i = 0; i < duplicate_networks; ++i) {
      cred = _net[i]->ReadCredit( input );
      if ( cred ) {
        _buf_states[input][i]->ProcessCredit( cred );
        delete cred;
      }
    }
    
    bool write_flit    = false;
    int  highest_class = 0;
    bool generated;

    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (input,class)
      // that is currently empty
      if ( (duplicate_networks > 1) || _partial_packets[input][c][0].empty() ) {
      // For multiple networks, always flip coin because now you have multiple send buffers so you can't choose one only to check.
	generated = false;
	  
	if ( !_empty_network ) {
	  while( !generated && ( _qtime[input][c] <= _time ) ) {
	    psize = _IssuePacket( input, c );

	    if ( psize ) { //generate a packet
	      _GeneratePacket( input, psize, c, 
			       _include_queuing==1 ? 
			       _qtime[input][c] : _time );
	      generated = true;
	    }
	    //this is not a request packet
	    //don't advance time
	    if(_use_read_write && psize>0){
	      
	    } else {
	      ++_qtime[input][c];
	    }
	  }
	  
	  if ( ( _sim_state == draining ) && 
	       ( _qtime[input][c] > _drain_time ) ) {
	    _qdrained[input][c] = true;
	  }
	}
	if ( generated ) {
	  //highest_class = c;
	  class_array[sub_network][c]++; // One more packet for this class.
	}
      } //else {
	//highest_class = c;
      //} This is not necessary with class_array because it stays.
    }

    // Now, check partially issued packets to
    // see if they can be issued
    for (int i = 0; i < duplicate_networks; ++i) {
      write_flit = false;
      highest_class = 0;
      // Now just find which is the highest_class.
      for (short a = _classes - 1; a >= 0; --a) {
	if (class_array[i][a]) {
	  highest_class = a;
	  break;
	}
      }
      if ( !_partial_packets[input][highest_class][i].empty( ) ) {
        f = _partial_packets[input][highest_class][i].front( );
        if ( f->head && f->vc == -1) { // Find first available VC

	  if ( _voqing ) {
	    if ( _buf_states[input][i]->IsAvailableFor( f->dest ) ) {
	      f->vc = f->dest;
  	    }
	  } else {
	    f->vc = _buf_states[input][i]->FindAvailable( );
	  }
	  
	  if ( f->vc != -1 ) {
	    _buf_states[input][i]->TakeBuffer( f->vc );
	  }
        }

        if ( ( f->vc != -1 ) &&
	     ( !_buf_states[input][i]->IsFullFor( f->vc ) ) ) {

	  _partial_packets[input][highest_class][i].pop_front( );
	  _buf_states[input][i]->SendingFlit( f );
	  write_flit = true;

	  // Pass VC "back"
	  if ( !_partial_packets[input][highest_class][i].empty( ) && !f->tail ) {
	    nf = _partial_packets[input][highest_class][i].front( );
	    nf->vc = f->vc;
	  }
        }
      }
      _net[i]->WriteFlit( write_flit ? f : 0, input );
      if (write_flit && f->tail) // If a tail flit, reduce the number of packets of this class.
	class_array[i][highest_class]--;
    }
  }
  for (int i=0; i < duplicate_networks; ++i) {
    delete [] class_array[i];
  }
  delete [] class_array;
}

void TrafficManager::_Step( )
{
  if(deadlock_counter++ == 0){
    cout << "WARNING: Possible network deadlock.\n";
  }

  Flit   *f;
  Credit *cred;

  if(_sim_mode == batch){
    _BatchInject();
  } else {
    _NormalInject();
  }

  //advance networks
  for (int i = 0; i < duplicate_networks; ++i) {
    _net[i]->ReadInputs( );
    _partial_internal_cycles[i] += _internal_speedup;
    while( _partial_internal_cycles[i] >= 1.0 ) {
      _net[i]->InternalStep( );
      _partial_internal_cycles[i] -= 1.0;
    }
  }

  for (int a = 0; a < duplicate_networks; ++a) {
    _net[a]->WriteOutputs( );
  }
  
  ++_time;
  if(_trace){
    cout<<"TIME "<<_time<<endl;
  }


  for (int i = 0; i < duplicate_networks; ++i) {
    // Eject traffic and send credits
    for ( int output = 0; output < _net[0]->NumDests( ); ++output ) {
      f = _net[i]->ReadFlit( output );

      if ( f ) {
        if ( f->watch ) {
	  cout << "ejected flit " << f->id << " at output " << output << endl;
	  cout << "sending credit for " << f->vc << endl;
        }
      
        cred = new Credit( 1 );
        cred->vc[0] = f->vc;
        cred->vc_cnt = 1;
	cred->dest_router = f->from_router;
        _net[i]->WriteCredit( cred, output );
        _RetireFlit( f, output );
      
        _accepted_packets[output]->AddSample( 1 );
      } else {
        _net[i]->WriteCredit( 0, output );
        _accepted_packets[output]->AddSample( 0 );
      }
    }
  }
}
  
bool TrafficManager::_PacketsOutstanding( ) const
{
  bool outstanding;

  if ( _measured_in_flight == 0 ) {
    outstanding = false;

    for ( int c = 0; c < _classes; ++c ) {
      for ( int s = 0; s < _sources; ++s ) {
	if ( !_qdrained[s][c] ) {
#ifdef DEBUG_DRAIN
	  cout << "waiting on queue " << s << " class " << c;
	  cout << ", time = " << _time << " qtime = " << _qtime[s][c] << endl;
#endif
	  outstanding = true;
	  break;
	}
      }
      if ( outstanding ) { break; }
    }
  } else {
#ifdef DEBUG_DRAIN
    cout << "in flight = " << _measured_in_flight << endl;
#endif
    outstanding = true;
  }

  return outstanding;
}

void TrafficManager::_ClearStats( )
{
  for ( int c = 0; c < _classes; ++c ) {
    _latency_stats[c]->Clear( );
  }
  
  for ( int i = 0; i < _dests; ++i ) {
    _accepted_packets[i]->Clear( );
    _pair_latency[i]->Clear( );
  }
}

int TrafficManager::_ComputeAccepted( double *avg, double *min ) const 
{
  int dmin;

  *min = 1.0;
  *avg = 0.0;

  for ( int d = 0; d < _dests; ++d ) {
    if ( _accepted_packets[d]->Average( ) < *min ) {
      *min = _accepted_packets[d]->Average( );
      dmin = d;
    }
    *avg += _accepted_packets[d]->Average( );
  }

  *avg /= (double)_dests;

  return dmin;
}

void TrafficManager::_DisplayRemaining( ) const 
{
  cout << "Remaining flits (" << _measured_in_flight << " measurement packets, "
       << _total_in_flight << " total) : ";

  map<int, bool>::const_iterator iter;
  int i;
  for ( iter = _in_flight.begin( ), i = 0;
	( iter != _in_flight.end( ) ) && ( i < 10 );
	iter++, i++ ) {
    cout << iter->first << " ";
  }

  i = _in_flight.size();
  if(i > 10)
    cout << "[...] (" << i << " flits total)";

  cout << endl;

}

bool TrafficManager::_SingleSim( )
{
  int  iter;
  int  total_phases;
  int  converged;
  int  empty_steps;
  
  double cur_latency;
  double prev_latency;

  double cur_accepted;
  double prev_accepted;

  double warmup_threshold;
  double stopping_threshold;
  double acc_stopping_threshold;

  double min, avg;

  bool   clear_last;

  _time = 0;
  //remove any pending request from the previous simulations
  for (int i=0;i<_sources;i++) {
    _packets_sent[i] = 0;
    _requestsOutstanding[i] = 0;

    while (!_repliesPending[i].empty()) {
      _repliesPending[i].pop_front();
    }
  }

  //reset queuetime for all sources
  for ( int s = 0; s < _sources; ++s ) {
    for ( int c = 0; c < _classes; ++c  ) {
      _qtime[s][c]    = 0;
      _qdrained[s][c] = false;
    }
  }

  stopping_threshold     = 0.05;
  acc_stopping_threshold = 0.05;
  warmup_threshold       = 0.05;
  iter            = 0;
  converged       = 0;
  total_phases    = 0;

  // warm-up ...
  // reset stats, all packets after warmup_time marked
  // converge
  // draing, wait until all packets finish
  _sim_state    = warming_up;
  total_phases  = 0;
  prev_latency  = 0;
  prev_accepted = 0;

  _ClearStats( );
  clear_last    = false;

  if (_sim_mode == batch && timed_mode){
    while(_time<_sample_period){
      _Step();
      if ( _time % 10000 == 0 ) {
	cout <<_sim_state<< "%=================================" << endl;
	int dmin;
	cur_latency = _latency_stats[0]->Average( );
	dmin = _ComputeAccepted( &avg, &min );
	cur_accepted = avg;
	
	cout << "% Average latency = " << cur_latency << endl;
	cout << "% Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
	cout << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl;
	_latency_stats[0]->Display();
      } 
    }
    cout<<"Total inflight "<<_total_in_flight<<endl;
    converged = 1;

  } else if(_sim_mode == batch && !timed_mode){//batch mode   
    while(_packets_sent[0] < _batch_size){
      _Step();
      if ( _time % 1000 == 0 ) {
	cout <<_sim_state<< "%=================================" << endl;
	int dmin;
	cur_latency = _latency_stats[0]->Average( );
	dmin = _ComputeAccepted( &avg, &min );
	cur_accepted = avg;
	
	cout << "% Average latency = " << cur_latency << endl;
	cout << "% Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
	cout << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl;
	_latency_stats[0]->Display();
      }
    }
    cout << "batch size of "<<_batch_size  <<  " sent. Time used is " << _time << " cycles" <<endl;
    cout<< "Draining the Network...................\n";
    empty_steps = 0;
    while( (_drain_measured_only ? _measured_in_flight : _total_in_flight) > 0 ) { 
      _Step( ); 
      ++empty_steps;
      
      if ( empty_steps % 1000 == 0 ) {
	_DisplayRemaining( ); 
	cout << ".";
      }
    }
    cout << endl;
    cout << "batch size of "<<_batch_size  <<  " received. Time used is " << _time << " cycles" <<endl;
    converged = 1;
  } else { 
    //once warmed up, we require 3 converging runs
    //to end the simulation 
    while( ( total_phases < _max_samples ) && 
	   ( ( _sim_state != running ) || 
	     ( converged < 3 ) ) ) {

      if ( clear_last || (( ( _sim_state == warming_up ) && ( total_phases & 0x1 == 0 ) )) ) {
	clear_last = false;
	_ClearStats( );
      }
      
      
      for ( iter = 0; iter < _sample_period; ++iter ) { _Step( ); } 
      
      cout <<_sim_state<< "%=================================" << endl;
      int dmin;
      cur_latency = _latency_stats[0]->Average( );
      dmin = _ComputeAccepted( &avg, &min );
      cur_accepted = avg;
      cout << "% Average latency = " << cur_latency << endl;
      cout << "% Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
      cout << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl;
      _latency_stats[0]->Display();
      cout << "thru(" << total_phases + 1 << ",:) = [ ";
      for ( int d = 0; d < _dests; ++d ) {
	cout << _accepted_packets[d]->Average( ) << " ";
      }
      cout << "];" << endl;

      // Fail safe for latency mode, throughput will ust continue
      if ( ( _sim_mode == latency ) && ( cur_latency >_latency_thres ) ) {
	cout << "Average latency is getting huge" << endl;
	converged = 0; 
	_sim_state = warming_up;
	break;
      }

      cout << "% latency change    = " << fabs( ( cur_latency - prev_latency ) / cur_latency ) << endl;
      cout << "% throughput change = " << fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) << endl;

      if ( _sim_state == warming_up ) {
	if ( _warmup_periods == 0 ) {
	  if ( _sim_mode == latency ) {
	    if ( ( fabs( ( cur_latency - prev_latency ) / cur_latency ) < warmup_threshold ) &&
		 ( fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) < warmup_threshold ) ) {
	      cout << "% Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
	      clear_last = true;
	      _sim_state = running;
	    }
	  } else {
	    if ( fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) < warmup_threshold ) {
	      cout << "% Warmed up ..." << "Time used is " << _time << " cycles" << endl;
	      clear_last = true;
	      _sim_state = running;
	    }
	  }
	} else {
	  if ( total_phases + 1 >= _warmup_periods ) {
	    cout << "% Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
	    clear_last = true;
	    _sim_state = running;
	  }
	}
      } else if ( _sim_state == running ) {
	if ( _sim_mode == latency ) {
	  if ( ( fabs( ( cur_latency - prev_latency ) / cur_latency ) < stopping_threshold ) &&
	       ( fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) < acc_stopping_threshold ) ) {
	    ++converged;
	  } else {
	    converged = 0;
	  }
	} else {
	  if ( fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) < acc_stopping_threshold ) {
	    ++converged;
	  } else {
	    converged = 0;
	  }
	} 
      }
      prev_latency  = cur_latency;
      prev_accepted = cur_accepted;
      ++total_phases;
    }
  
    if ( _sim_state == running ) {
      ++converged;

      if ( _sim_mode == latency ) {
	cout << "% Draining all recorded packets ..." << endl;
	_sim_state  = draining;
	_drain_time = _time;
	empty_steps = 0;
	while( _PacketsOutstanding( ) ) { 
	  _Step( ); 
	  ++empty_steps;
	
	  if ( empty_steps % 1000 == 0 ) {
	    _DisplayRemaining( ); 
	  }
	}
      }
    } else {
      cout << "Too many sample periods needed to converge" << endl;
    }

    // Empty any remaining packets
    cout << "% Draining remaining packets ..." << endl;
    _empty_network = true;
    empty_steps = 0;
    while( (_drain_measured_only ? _measured_in_flight : _total_in_flight) > 0 ) { 
      _Step( ); 
      ++empty_steps;

      if ( empty_steps % 1000 == 0 ) {
	_DisplayRemaining( ); 
      }
    }
    _empty_network = false;
  }

  return ( converged > 0 );
}

bool TrafficManager::Run( )
{
  double min, avg;
  int total_packets = 0;
  _FirstStep( );
  
  for ( int sim = 0; sim < _total_sims; ++sim ) {
    if ( !_SingleSim( ) ) {
      cout << "Simulation unstable, ending ..." << endl;
      return false;
    }
    //for the love of god don't ever say "Time taken" anywhere else
    //the power script depend on it
    cout << "Time taken is " << _time << " cycles" <<endl; 
    for ( int c = 0; c < _classes; ++c ) {
      _overall_latency[c]->AddSample( _latency_stats[c]->Average( ) );
    }
    
    _ComputeAccepted( &avg, &min );
    _overall_accepted->AddSample( avg );
    _overall_accepted_min->AddSample( min );
  }
  
  DisplayStats();
  return true;
}

void TrafficManager::DisplayStats() {
  for ( int c = 0; c < _classes; ++c ) {

    if(_print_csv_results) {
      cout << "results:"
	   << c
	   << "," << _traffic
	   << "," << _packet_size
	   << "," << _load
	   << "," << _flit_rate
	   << "," << _overall_latency[c]->Average( )
	   << "," << _overall_accepted_min->Average( )
	   << "," << _overall_accepted_min->Average( )
	   << "," << _hop_stats->Average( )
	   << endl;
    }

    cout << "====== Traffic class " << c << " ======" << endl;
    
    cout << "Overall average latency = " << _overall_latency[c]->Average( )
	 << " (" << _overall_latency[c]->NumSamples( ) << " samples)" << endl;
    
    cout << "Overall average accepted rate = " << _overall_accepted->Average( )
	 << " (" << _overall_accepted->NumSamples( ) << " samples)" << endl;
    
    cout << "Overall min accepted rate = " << _overall_accepted_min->Average( )
	 << " (" << _overall_accepted_min->NumSamples( ) << " samples)" << endl;
    
  }

  cout << "Average hops = " << _hop_stats->Average( )
       << " (" << _hop_stats->NumSamples( ) << " samples)" << endl;
  
}

//read the watchlist
void TrafficManager::_LoadWatchList(){
  ifstream watch_list;
  string line;
  string delimiter = ",";
  watch_list.open(watch_file.c_str());

  if(watch_list.is_open()){
    while(!watch_list.eof()){
      getline(watch_list,line);
      if(line!=""){
	flits_to_watch[atoi(line.c_str())]= true;
      }
    }

  } else {
    cout<<"Unable to open flit watch file, continuing with simulation\n";
  }
}
