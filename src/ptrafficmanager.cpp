// $Id: trafficmanager.cpp 1087 2009-02-10 23:53:08Z qtedq $

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
#include <fstream>
#include "ptrafficmanager.hpp"
#include "random_utils.hpp" 
#include <time.h>
#include <sys/time.h>

//batched time-mode, know what you are doing
extern bool timed_mode;

double total_time; /* Amount of time we've run */
struct timeval start_time, end_time; /* Time before/after user code */

pthread_cond_t  *thread_restart;
pthread_mutex_t *thread_restart_lock;
pthread_cond_t master_restart;
pthread_mutex_t master_lock;
bool thread_stop = false;

PTrafficManager::PTrafficManager( const Configuration &config, Network **net )
  : TrafficManager ( config, net) 
{
  thread_restart_lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t)*_threads);
  thread_restart = (pthread_cond_t*)malloc(sizeof(pthread_cond_t)*_threads);
  for(int i = 0; i<_threads; i++){
    pthread_mutex_init(&thread_restart_lock[i], 0);
    pthread_cond_init(&thread_restart[i], 0);
  }
  pthread_cond_init(&master_restart,0);
  pthread_mutex_init(&master_lock,0);
  //pthread_cond_init(&thread_restart,0);
  pthread_barrier_init(&thread_bar, 0, (unsigned int)_threads);
  

  thread_fid = (int *)malloc(_threads*sizeof(int));
  thread_hop_stats = (Stats**)malloc(_threads*sizeof(Stats*));
  thread_latency_stats = (Stats***)malloc(_threads*sizeof(Stats**));
  thread_partial_internal_cycles = (float**)malloc(_threads*sizeof(float*));

  thread_time = (int*)malloc(_threads*sizeof(int));
  for(int i = 0; i<_threads; i++){
    thread_time[i] = 0;
    thread_partial_internal_cycles[i] = new float[duplicate_networks];
    for(int j = 0; j<duplicate_networks; j++){
      thread_partial_internal_cycles[i][j] = 0.0; 
    }
    
    thread_fid[i] = i;
    thread_hop_stats[i] = new Stats( this, "hop_stats", 1.0, 20 );

    thread_latency_stats[i] = (Stats**)malloc(sizeof(Stats*)*_classes);
    for ( int c = 0; c < _classes; ++c ){
      thread_latency_stats[i][c] = new Stats( this, "thread latency stats", 1.0, 1000 );
    }
  }
  net[0]->GetNodes(&node_list, &node_count);
  
  map<int,bool> lol;
  for(int i = 0; i<_threads; i++){
    for(int j = 0; j<node_count[i]; j++){
      if(lol[node_list[i][j]]){
	cout<<"Node assignment to the ptraffic managers are wrong, dup found\n";
	exit(-1);
      }
      lol[node_list[i][j]] = true;
    }
  }
  cout<<"Assigned "<<lol.size()<<" routers to threads"<<endl;
  total_time = 0.0;
}

PTrafficManager::~PTrafficManager( )
{
  cout<<"LOL "<<thread_fid[0]<<" "<<thread_fid[1]<<endl;
  cout<<"bar time "<<total_time<<endl;
  pthread_barrier_destroy(&thread_bar);
  delete thread_fid;
  for(int i = 0; i<_threads; i++){
    delete thread_hop_stats[i];
  }
  delete[] thread_hop_stats;
  delete []thread_partial_internal_cycles;
}




Flit *PTrafficManager::_NewFlitP( int t)
{
  Flit *f;
  //the constructor should initialize everything
  f = new Flit();
  f->id    = thread_fid[t];
  //  f->watch = true;
  //  _in_flight[f->id] = true;
  //no automatic flit watching
  thread_fid[t]+=_threads;
  return f;
}

void PTrafficManager::_RetireFlitP( Flit *f, int dest, int tid)
{


  map<int, bool>::iterator match;

//   match = _in_flight.find( f->id );

//   if ( match != _in_flight.end( ) ) {
//     if ( f->watch ) {
//       cout << "Matched flit ID = " << f->id << endl;
//     }
//     _in_flight.erase( match );
//   } else {
//     cout << "Unmatched flit! ID = " << f->id << endl;
//     Error( "" );
//   }
  
  if ( f->watch ) { 
    cout << "Ejecting flit " << f->id 
	 << ",  lat = " << thread_time[tid] - f->time
	 << ", src = " << f->src 
	 << ", dest = " << f->dest << endl;
  }

  if ( f->head && ( f->dest != dest ) ) {
    cout << "At output " << dest << endl;
    cout << *f;
    Error( "Flit arrived at incorrect output" );
  }

  if ( f->tail ) {
//     _total_in_flight--;
//     if ( _total_in_flight < 0 ) {
//       Error( "Total in flight count dropped below zero!" );
//     }
    
    //code the source of request, look carefully, its tricky ;)

    if (f->type == Flit::READ_REQUEST) {
      Packet_Reply* temp = new Packet_Reply;
      temp->source = f->src;
      temp->time = thread_time[tid];
      temp->type = f->type;
      _repliesDetails[f->id] = temp;
      _repliesPending[dest].push_back(f->id);
      cout<<"retire flit: parallel data structure not setup for this sim mode\n";
      assert(false);
    } else if (f->type == Flit::WRITE_REQUEST) {
      Packet_Reply* temp = new Packet_Reply;
      temp->source = f->src;
      temp->time = thread_time[tid];
      temp->type = f->type;
      _repliesDetails[f->id] = temp;
      _repliesPending[dest].push_back(f->id);

      cout<<"retire flit: parallel data structure not setup for this sim mode\n";
      assert(false);
    } else if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
      //received a reply
      _requestsOutstanding[dest]--;
      cout<<"retire flit: parallel data structure not setup for this sim mode\n";
      assert(false);
    } else if(f->type == Flit::ANY_TYPE && _sim_mode == batch  ){
      //received a reply
      _requestsOutstanding[f->src]--;
      cout<<"retire flit: parallel data structure not setup for this sim mode\n";
      assert(false);
    }


    // Only record statistics once per packet (at tail)
    // and based on the simulation state1
    if ( ( _sim_state == warming_up ) || f->record ) {
      
      thread_hop_stats[tid]->AddSample( f->hops );

      switch( _pri_type ) {
      case class_based:
	thread_latency_stats[tid][f->pri]->AddSample( thread_time[tid] - f->time );
	break;
      case age_based: // fall through
      case none:
	thread_latency_stats[tid][0]->AddSample( thread_time[tid] - f->time);
	break;
      }
   
 
      
 //      if ( f->record ) {
// 	_measured_in_flight--;
// 	if ( _measured_in_flight < 0 ){ 
// 	  Error( "Measured in flight count dropped below zero!" );
// 	}
//       }
    }
  }
  delete f;
}



void PTrafficManager::_GeneratePacketP( int source, int stype, 
				      int cl, int time ,int tid)
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
      cout<<"generate packet: parallel data structure not setup for this sim mode\n";
      assert(false);
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
    //++_measured_in_flight;
    record = true;
  } else {
    record = false;
  }
  //  ++_total_in_flight;

  //  sub_network = DivisionAlgorithm(packet_type);
  sub_network = 0;

  for ( int i = 0; i < size; ++i ) {
    f = _NewFlitP( tid);
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



void PTrafficManager::_NormalInjectP(int tid){

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
  for ( int input_pos = 0; input_pos < node_count[tid]; ++input_pos ) {
    int input = node_list[tid][input_pos];
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
	  while( !generated && ( _qtime[input][c] <= thread_time[tid] ) ) {
	    psize = _IssuePacket( input, c );

	    if ( psize ) { //generate a packet
	      _GeneratePacketP( input, psize, c, 
			       _include_queuing==1 ? 
			       _qtime[input][c] : thread_time[tid], tid );
	      generated = true;
	    }
	    //this is not a request packet
	    //don't advance time
	    if(_use_read_write && psize>0){
	      
	      assert(false);
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
//       // Now just find which is the highest_class.
//       for (short a = _classes - 1; a >= 0; --a) {
// 	if (class_array[i][a]) {
// 	  highest_class = a;
// 	  break;
// 	}
//       }
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

void PTrafficManager::_StepP( int tid)
{

  Flit   *f;
  Credit *cred;

  if(_sim_mode == batch){
    assert(false);
    _BatchInject();
  } else {
    _NormalInjectP(tid);
  }
  //advance networks
  for (int i = 0; i < duplicate_networks; ++i) {
    _net[i]->DOALL(tid);
  }
//     _net[i]->ReadInputs(tid);
//     thread_partial_internal_cycles[tid][i] += _internal_speedup;
//     while( thread_partial_internal_cycles[tid][i] >= 1.0 ) {
//       _net[i]->InternalStep(tid);
//       thread_partial_internal_cycles[tid][i] -= 1.0;
//     }
//   }
//   for (int a = 0; a < duplicate_networks; ++a) {
//     _net[a]->WriteOutputs(tid);
//   }
  
 


  //if(thread_time[tid]%1 == 0){    
  // pthread_barrier_wait(&thread_bar);
    //}

  thread_time[tid]++;
  
  if(tid == 0){
    cout<<"Heart beat "<<_time<<endl;
    ++_time;
    if(_time%_sample_period == 0){
      pthread_mutex_lock(&master_lock);
      pthread_cond_signal(&master_restart);
      pthread_mutex_unlock(&master_lock);
    }
  }


  for (int i = 0; i < duplicate_networks; ++i) {
    // Eject traffic and send credits
    for ( int output_pos = 0; output_pos < node_count[tid]; ++output_pos ) {
      int output = node_list[tid][output_pos];
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
        _RetireFlitP( f, output , tid);
      
        _accepted_packets[output]->AddSample( 1 );
      } else {
        _net[i]->WriteCredit( 0, output );
        _accepted_packets[output]->AddSample( 0 );
      }
    }
  }
}
  

struct Thread_job{
  PTrafficManager *pt;
  int tid; 
};


void PTrafficManager::runthread(int tid){
  
  cout<<"I am thread "<<tid<<endl;
  for (int iter = 0; iter < _sample_period; ++iter ) { _StepP(tid); } 
  
}


void* PTrafficManager::launchthread(void* arg){
  Thread_job *job = (Thread_job *)arg;
  PTrafficManager *curr = job->pt;
  int tid = job->tid;
  pthread_mutex_lock(&thread_restart_lock[tid]);
  
  while(!thread_stop){
 
    curr->runthread(tid);
    cout<<"thread "<<tid<<" waiting"<<endl;
    pthread_cond_wait(&thread_restart[tid], &thread_restart_lock[tid]);
  }
  
  pthread_mutex_unlock(&thread_restart_lock[tid]);
}

bool PTrafficManager::_SingleSim( )
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

  for(int i = 0; i<_threads;i++){
    thread_time[i] = 0;
  }
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
    assert(false);
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
    //    cout<<"Total inflight "<<_total_in_flight<<endl;
    converged = 1;

  } else if(_sim_mode == batch && !timed_mode){//batch mode   
    assert(false);
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
    pthread_t threads[_threads];
    
    Thread_job job[_threads];
    
    pthread_mutex_lock(&master_lock);
    for(int i = 0; i<_threads; i++){
      job[i].tid = i;
      job[i].pt = this;
      pthread_create(&threads[i], NULL,PTrafficManager::launchthread,(void *)(&job[i]));
    }
    


    while( ( total_phases < _max_samples ) && 
	   ( ( _sim_state != running ) || 
	     ( converged < 3 ) ) ) {

      if ( clear_last || (( ( _sim_state == warming_up ) && ( total_phases & 0x1 == 0 ) )) ) {
	clear_last = false;
	_ClearStats( );
      }
      cout<<"master waiting "<<endl;
      pthread_cond_wait(&master_restart, &master_lock);
      cout<<"master woken up "<<endl;

      for(int i = 0; i<_threads; i++){
	pthread_mutex_lock(&thread_restart_lock[i]);
      }

      
      void* status;
      for(int i = 0; i<_threads; i++){
	for(int j = 0; j<_classes;j++){
	  _latency_stats[j]->MergeStats(thread_latency_stats[i][j]);     
	}
	  _hop_stats->MergeStats(thread_hop_stats[i]);
      }

      if(!(( total_phases+1 < _max_samples ) && 
	   ( ( _sim_state != running ) || 
	     ( converged+1 < 3 ) ))){
	thread_stop = true;
	cout<<"oh fuck\n";
      }


      for(int i = 0; i<_threads; i++){
	pthread_cond_signal(&thread_restart[i]);
	pthread_mutex_unlock(&thread_restart_lock[i]);
      }

      cout <<_sim_state<< "%=================================" << endl;
      int dmin;
      cur_latency = _latency_stats[0]->Average( );
      dmin = _ComputeAccepted( &avg, &min );
      cur_accepted = avg;
      cout << "% Average latency = " << cur_latency << endl;
      cout << "% Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
      cout << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl;
      _latency_stats[0]->Display();
//       cout << "thru(" << total_phases + 1 << ",:) = [ ";
//       for ( int d = 0; d < _dests; ++d ) {
// 	cout << _accepted_packets[d]->Average( ) << " ";
//       }
//       cout << "];" << endl;

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
    pthread_mutex_unlock(&master_lock);



  
    if ( _sim_state == running ) {
      ++converged;

      if ( _sim_mode == latency ) {
	cout << "% Draining all recorded packets ..." << endl;
	_sim_state  = draining;
	_drain_time = _time;
	empty_steps = 0;
// 	while( _PacketsOutstanding( ) ) { 
// 	  _Step( ); 
// 	  ++empty_steps;
	
// 	  if ( empty_steps % 1000 == 0 ) {
// 	    _DisplayRemaining( ); 
// 	  }
// 	}
      }
    } else {
      cout << "Too many sample periods needed to converge" << endl;
    }

    // Empty any remaining packets
    cout << "% Draining remaining packets ..." << endl;
    _empty_network = true;
    empty_steps = 0;
//     while( (_drain_measured_only ? _measured_in_flight : _total_in_flight) > 0 ) { 
//       _Step( ); 
//       ++empty_steps;

//       if ( empty_steps % 1000 == 0 ) {
// 	_DisplayRemaining( ); 
//       }
//     }
    _empty_network = false;
  }

  return ( converged > 0 );
}



