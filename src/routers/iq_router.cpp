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

#include "iq_router.hpp"

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cassert>
#include <limits>

#include "network.hpp"
#include "globals.hpp"
#include "random_utils.hpp"
#include "vc.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer.hpp"
#include "voq_buffer.hpp"
#include "buffer_state.hpp"
#include "roundrobin_arb.hpp"
#include "allocator.hpp"
#include "switch_monitor.hpp"
#include "buffer_monitor.hpp"
#include "reservation.hpp"
#include "trafficmanager.hpp"
#include "stats.hpp"

extern vector< Network * > net;
extern TrafficManager * trafficManager;
extern map<int, vector<int> > gDropStats;
extern map<int, vector<int> > gChanDropStats;
extern Stats* gStatDropLateness;

extern bool RESERVATION_QUEUING_DROP;
extern bool VC_ALLOC_DROP;
extern int ECN_BUFFER_THRESHOLD;
extern int ECN_CONGEST_THRESHOLD;
extern int ECN_BUFFER_HYSTERESIS;
extern int ECN_CREDIT_HYSTERESIS;

extern bool RESERVATION_BUFFER_SIZE_DROP;
extern int DEFAULT_CHANNEL_LATENCY;

//improves large simulation performance
#define USE_LARGE_ARB
#define MAX(X,Y) ((X)>(Y)?(X):(Y))
#define MIN(X,Y) ((X)<(Y)?(X):(Y))

//voq label to real vc
int IQRouter::voq2vc(int vvc, int output){
  assert(_voq);
  assert(output!=-1);
  if(gReservation){
    if(_spec_voq){
      if(vvc<RES_RESERVED_VCS){
	return vvc;
      } else if(vvc<RES_RESERVED_VCS+_outputs){ //spec
	return vvc-output;
      } else{ //data
	return vvc-output-(_outputs-1);
      }
    } else {
      if(vvc<RES_RESERVED_VCS+1){
	return vvc;
      } else { //data
	return vvc-output;
      }
    }
  } else if(gECN){
    if(vvc<ECN_RESERVED_VCS){
      return vvc;
    } else {//data
      return vvc-output;
    }
  } else {
    return  0; 
  }
}

bool IQRouter::is_control_vc(int vc){
  assert(_voq);
  if(gReservation){
    if(vc<RES_RESERVED_VCS){
      return true;
    }
  } else if(gECN){
    if(vc<ECN_RESERVED_VCS){
      return true;
    }
  }

  return false;

}
bool IQRouter::is_voq_vc(int vc){
  assert(_voq);
  if(gReservation){
    if(_spec_voq){
      if(vc<RES_RESERVED_VCS){
	return false;
      } else{ //data and spec
	return true;
      }
    } else {
      if(vc<RES_RESERVED_VCS+1){
	return false;
      } else { //data
	return true;
      }
    }
  } else if(gECN){
    if(vc<ECN_RESERVED_VCS){
      return false;
    } else {//data
      return true;
    }
  } else {
    return true;
  }
}
int IQRouter::vc2voq(int vc, int output){
  assert(_voq);
  if(gReservation){
    if(_spec_voq){
      if(vc<RES_RESERVED_VCS){
	return vc;
      } else if(vc<RES_RESERVED_VCS+1){//spec
	if(output==-1)
	  return -1;
	else 
	  return vc+output;
      } else { //data
	if(output==-1)
	  return -1;
	else 
	  return vc+output+(_outputs-1);
      }
    } else {
      if(vc<RES_RESERVED_VCS+1){
	return vc;
      } else { //data
	if(output==-1)
	  return -1;
	else 
	  return vc+output;
      }
    }
  } else if(gECN){
    if(vc<ECN_RESERVED_VCS){
      return vc;
    } else {//data
      if(output==-1)
	return -1;
      else 
	return vc+output;
    }
  } else {
    return  output; 
  }
}
int IQRouter::voqport(int vc){
  assert(_voq);
  if(gReservation){
    if(_spec_voq){
      if(vc<RES_RESERVED_VCS){
	return -1;
      } else if(vc<RES_RESERVED_VCS+_outputs){
	return vc-(RES_RESERVED_VCS);
      } else { 
	return vc-(RES_RESERVED_VCS)-_outputs;
      }
    } else {
      if(vc<RES_RESERVED_VCS+1){
	return -1;
      } else { 
	return vc-(RES_RESERVED_VCS+1);
      }
    }
  } else if(gECN){
    if(vc<ECN_RESERVED_VCS){
      return -1;
    } else {//data
      return vc-ECN_RESERVED_VCS;
    }
  } else {
    return vc;
  }
}



IQRouter::IQRouter( Configuration const & config, Module *parent, 
		    string const & name, int id, int inputs, int outputs )
  : Router( config, parent, name, id, inputs, outputs ), _active(false)
{


  gDropStats.insert(pair<int,  vector<int> >(_id, vector<int>() ));
  gDropStats[id].resize(inputs,0);
  gChanDropStats.insert(pair<int,  vector<int> >(_id, vector<int>() ));
  gChanDropStats[id].resize(inputs,0);  

  _cut_through = (config.GetInt("cut_through")==1);
  assert(!_cut_through || (config.GetInt("vc_busy_when_full")==1));
  _use_voq_size=(config.GetInt("use_voq_size")==1);
  _voq = (config.GetInt("voq") ==1);
  _spec_voq=(config.GetInt("reservation_spec_voq") ==1);
  _real_vcs         = config.GetInt( "num_vcs" );

  if(_voq){
    _vcs = config.GetInt( "num_vcs" );
    //voq currently only works for single vc
    if(gReservation){
      assert(_vcs==RES_RESERVED_VCS+2);
    } else if(gECN){
      assert(_vcs==ECN_RESERVED_VCS+1);
    } else {
      assert(_vcs==1);
    }
    //add the voq vcs
    _vcs+=outputs-1;
    if(gReservation && _spec_voq){
      _vcs+=outputs-1;
    }
    _voq_init_route = new OutputSet();
    _voq_pid.resize(inputs*_real_vcs);
    _voq_route_set.resize(inputs*_vcs);
    _res_voq_drop.resize(inputs*_vcs, false);
    
    for(int i = 0; i<inputs; i++){
      for(int j = 0; j<_vcs; j++){
	int index = i*_vcs+j;
	_voq_route_set[index] = new OutputSet();
	if(is_voq_vc(j)){
	  int port = voqport(j);
	  assert(port!=-1);
	  _voq_route_set[index]->Add(port,voq2vc(j,port));
	}
      }
    }
  } else {
    _vcs         = config.GetInt( "num_vcs" );
  }
  
  _vc_activity.resize(inputs*_vcs,0);
  _holds = 0;
  _hold_cancels = 0;
  
  _classes     = config.GetInt( "classes" );
  _speculative = (config.GetInt("speculative") > 0);

  _port_congestness.resize(_vcs*outputs,0.0);
  _vc_request_buffer_sum.resize(_vcs*outputs,0);
  _vc_request_buffer_num.resize(_vcs*outputs,0);
  _vc_ecn.resize(_vcs*outputs,false);


  _output_hysteresis.resize(outputs,false);
  _credit_hysteresis.resize(_vcs*outputs,false);
  _vc_congested.resize(_vcs*outputs,0);
  _ECN_activated.resize(_real_vcs*outputs,0);
  _input_request.resize(inputs,0);
  _input_grant.resize(outputs, 0);
  _vc_congested_sum.resize(_vcs*outputs,0);

  //converting flits to nacks does nto support speculation yet
  //need to modifity the sw_alloc_vcs queue
  assert((!gReservation && _speculative)||(!_speculative));
  _spec_check_elig = (config.GetInt("spec_check_elig") > 0);
  _spec_mask_by_reqs = (config.GetInt("spec_mask_by_reqs") > 0);

  _routing_delay    = config.GetInt( "routing_delay" );
  _vc_alloc_delay   = config.GetInt( "vc_alloc_delay" );
  if(!_vc_alloc_delay) {
    Error("VC allocator cannot have zero delay.");
  }
  _sw_alloc_delay   = config.GetInt( "sw_alloc_delay" );
  if(!_sw_alloc_delay) {
    Error("Switch allocator cannot have zero delay.");
  }
  
  // Routing
  string const rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
  map<string, tRoutingFunction>::const_iterator rf_iter = gRoutingFunctionMap.find(rf);
  if(rf_iter == gRoutingFunctionMap.end()) {
    Error("Invalid routing function: " + rf);
  }
  _rf = rf_iter->second;

  // Alloc VC's
  _buf.resize(_inputs);
  for ( int i = 0; i < _inputs; ++i ) {
    ostringstream module_name;
    module_name << "buf_" << i;
    if(_voq){
      _buf[i] = new VOQ_Buffer(config, _outputs, this, module_name.str( ) );
    } else {
      _buf[i] = new Buffer(config, _outputs, this, module_name.str( ) );
    }
    module_name.str("");
  }

  // Alloc next VCs' buffer state
  _next_buf.resize(_outputs);
  for (int j = 0; j < _outputs; ++j) {
    ostringstream module_name;
    module_name << "next_vc_o" << j;
    _next_buf[j] = new BufferState( config, this, module_name.str( ) );
    module_name.str("");
  }

  // Alloc allocators
  string alloc_type = config.GetStr( "vc_allocator" );
  string arb_type;
  int iters;
  if(alloc_type == "piggyback") {
    if(!_speculative) {
      Error("Piggyback VC allocation requires speculative switch allocation to be enabled.");
    }
    _vc_allocator = NULL;
    _VOQArbs = NULL;
    _vc_rr_offset.resize(_outputs*_classes, -1);
  } else {
    arb_type = config.GetStr( "vc_alloc_arb_type" );
    iters = config.GetInt( "vc_alloc_iters" );
    if(iters == 0) iters = config.GetInt("alloc_iters");
    if(_voq){
#ifdef USE_LARGE_ARB
      _vc_allocator = NULL;_VOQArbs = new LargeRoundRobinArbiter*[_real_vcs*_outputs];
      for(int i = 0; i<_real_vcs*_outputs; i++){
	_VOQArbs[i] = new LargeRoundRobinArbiter("inject_arb",
						 _vcs*_inputs);
      }
#else
      _VOQArbs = NULL;_vc_allocator = Allocator::NewAllocator( this, "vc_allocator", alloc_type,_vcs*_inputs, _real_vcs*_outputs, iters, arb_type );
#endif	     
      
    } else {
      _vc_allocator = Allocator::NewAllocator( this, "vc_allocator", 
					       alloc_type,
					       _vcs*_inputs, 
					       _real_vcs*_outputs,
					       iters, arb_type );
    }
    if ( !_vc_allocator && !_VOQArbs  ) {
      Error("Unknown vc_allocator type: " + alloc_type);
    }
  }
  
  alloc_type = config.GetStr( "sw_allocator" );
  arb_type = config.GetStr( "sw_alloc_arb_type" );
  iters = config.GetInt("sw_alloc_iters");
  if(iters == 0) iters = config.GetInt("alloc_iters");
  _sw_allocator = Allocator::NewAllocator( this, "sw_allocator",
					   alloc_type,
					   _inputs*_input_speedup, 
					   _outputs*_output_speedup,
					   iters, arb_type );

  if ( !_sw_allocator ) {
    Error("Unknown sw_allocator type: " + alloc_type);
  }
  
  if ( _speculative && ( config.GetInt("spec_use_prio") == 0 ) ) {    
    _spec_sw_allocator = Allocator::NewAllocator( this, "spec_sw_allocator",
						  alloc_type,
						  _inputs*_input_speedup, 
						  _outputs*_output_speedup,
						  iters, arb_type );
    if ( !_spec_sw_allocator ) {
      Error("Unknown spec_sw_allocator type: " + alloc_type);
    }
  } else {
    _spec_sw_allocator = NULL;
  }

  _sw_rr_offset.resize(_inputs*_input_speedup);
  for(int i = 0; i < _inputs*_input_speedup; ++i)
    _sw_rr_offset[i] = i % _input_speedup;
  
  // Output queues
  _output_buffer_size = config.GetInt("output_buffer_size");
  _output_buffer.resize(_outputs); 
  if(gReservation || gECN){
    _output_control_buffer.resize(_outputs);
  }
  _credit_buffer.resize(_inputs); 

  // Switch configuration (when held for multiple cycles)
  _hold_switch_for_packet = (config.GetInt("hold_switch_for_packet") > 0);
  _switch_hold_in_skip.resize(_inputs*_input_speedup, false);
  _switch_hold_out_skip.resize(_outputs*_output_speedup, false);
  _switch_hold_in.resize(_inputs*_input_speedup, -1);
  _switch_hold_out.resize(_outputs*_output_speedup, -1);
  _switch_hold_vc.resize(_inputs*_input_speedup, -1);

  int classes = config.GetInt("classes");
  _bufferMonitor = new BufferMonitor(inputs, classes);
  _switchMonitor = new SwitchMonitor(inputs, outputs, classes);

  _stored_flits.resize(_inputs, 0);
  _active_packets.resize(_inputs, 0);
  
  dropped_pid = new int*[_inputs];
  for(int i = 0; i<_inputs; i++){
    dropped_pid[i] = new int[_vcs];
    for(int j = 0; j<_vcs; j++)
      dropped_pid[i][j] = -1;
  }
}

IQRouter::~IQRouter( )
{

  if(gPrintActivity) {
    cout << Name() << ".bufferMonitor:" << endl ; 
    cout << *_bufferMonitor << endl ;
    
    cout << Name() << ".switchMonitor:" << endl ; 
    cout << "Inputs=" << _inputs ;
    cout << "Outputs=" << _outputs ;
    cout << *_switchMonitor << endl ;
  }
  for(int i = 0; i<_inputs; i++){
    for(int j = 0; j<_vcs; j++){
      int index = i*_vcs+j;
      delete _voq_route_set[index];
    }
  }
  delete _voq_init_route;
  for(int i = 0; i<_inputs; i++){
    delete[] dropped_pid[i];
  }
  delete[] dropped_pid;

  for(int i = 0; i < _inputs; ++i)
    delete _buf[i];
  
  for(int j = 0; j < _outputs; ++j)
    delete _next_buf[j];
  if(!_voq){
    delete _vc_allocator;
  } else {
    for(int i = 0; i<_real_vcs*_outputs; i++){
      delete _VOQArbs[i];
    }
    delete [] _VOQArbs;
  }
  delete _sw_allocator;
  if(_spec_sw_allocator)
    delete _spec_sw_allocator;

  delete _bufferMonitor;
  delete _switchMonitor;
}
  
void IQRouter::ReadInputs( )
{
  bool have_flits = _ReceiveFlits( );
  bool have_credits = _ReceiveCredits( );
  _active = _active || have_flits || have_credits;
}

void IQRouter::_InternalStep( )
{
  if(!_active) {
    return;
  }

  _InputQueuing( );
  bool activity = !_proc_credits.empty();

  if(!_route_vcs.empty())
    _RouteEvaluate( );
  if(_vc_allocator ||_VOQArbs  ) {
    if(!_voq){
      _vc_allocator->Clear();
    } else {
#ifdef USE_LARGE_ARB
      for(int i = 0; i<_real_vcs*_outputs; i++){
	_VOQArbs[i]->UpdateState();
	_VOQArbs[i]->Clear();
      }
#else
      _vc_allocator->Clear();
#endif
    }
    if(!_vc_alloc_vcs.empty())
      _VCAllocEvaluate( );
  }
  if(_hold_switch_for_packet) {
    if(!_sw_hold_vcs.empty())
      _SWHoldEvaluate( );
  }
  _sw_allocator->Clear();
  if(_spec_sw_allocator)
    _spec_sw_allocator->Clear();
  if(!_sw_alloc_vcs.empty())
    _SWAllocEvaluate( );
  if(!_crossbar_flits.empty())
    _SwitchEvaluate( );

  if(!_route_vcs.empty()) {
    _RouteUpdate( );
    activity = activity || !_route_vcs.empty();
  }
  if(!_vc_alloc_vcs.empty()) {
    _VCAllocUpdate( );
    activity = activity || !_vc_alloc_vcs.empty();
  }
  if(_hold_switch_for_packet) {
    if(!_sw_hold_vcs.empty()) {
      _SWHoldUpdate( );
      activity = activity || !_sw_hold_vcs.empty();
    }
  }
  if(!_sw_alloc_vcs.empty()) {
    _SWAllocUpdate( );
    activity = activity || !_sw_alloc_vcs.empty();
  }
  if(!_crossbar_flits.empty()) {
    _SwitchUpdate( );
    activity = activity || !_crossbar_flits.empty();
  }


  _active = activity;

  _OutputQueuing( );

  _bufferMonitor->cycle( );
  _switchMonitor->cycle( );
}

void IQRouter::WriteOutputs( )
{
  _SendFlits( );
  _SendCredits( );
}


//------------------------------------------------------------------------------
// read inputs
//------------------------------------------------------------------------------

Flit* IQRouter::_ExpirationCheck(Flit* f, int input){
  Flit* drop_f = NULL;
  if(f && f->res_type == RES_TYPE_SPEC){
    bool drop = false;
    if(f->head){
      if(RESERVATION_QUEUING_DROP){
       	if(f->exptime<0){
	  //send drop nack
	  if(f->watch){
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "drop flit " << f->id
		       << " from channel at input " << input
		       << "." << endl;
	  }
	  drop_f = trafficManager->DropPacket(input, f);
	  drop_f->vc = f->vc;
	  drop = true;
	  dropped_pid[input][f->vc] = f->pid;
	  gStatDropLateness->AddSample(-f->exptime);
	  gChanDropStats[_id][input]++;
	}
      } else { 
	if(f->exptime<GetSimTime()){
	  //send drop nack
	  if(f->watch){
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "drop flit " << f->id
		       << " from channel at input " << input
		       << "." << endl;
	  }
	  drop_f = trafficManager->DropPacket(input, f);
	  drop_f->vc = f->vc;
	  drop = true;
	  dropped_pid[input][f->vc] = f->pid;
	  gStatDropLateness->AddSample(GetSimTime()-f->exptime);
	  gChanDropStats[_id][input]++;
	}
      }
    } else {
      if(f->pid == dropped_pid[input][f->vc]){//body
	drop = true;
      }
    }
    if(drop){   
      //send dropped credit since the packet is removed from the buffer
      if(drop_f==NULL){
	if(_out_queue_credits.count(input) == 0) {
	  _out_queue_credits.insert(make_pair(input, Credit::New()));
	}
	_out_queue_credits.find(input)->second->id=666;
	_out_queue_credits.find(input)->second->vc.push_back(f->vc);
      }

      f->Free();
      return drop_f;
    }
  }
  return f;
}
bool IQRouter::_ReceiveFlits( )
{
  bool activity = false;
  for(int input = 0; input < _inputs; ++input) { 
    if( _input_channels[input]->GetSource() == -1){
      Flit * f;
      for(int vc = 0; vc< _real_vcs; vc++){
	f = net[0]->GetSpecial( _input_channels[input],vc);
	if(gReservation){
	  f = _ExpirationCheck(f, input);
	}
	if(f){
	  _in_queue_flits.insert(make_pair(input, f));
	  activity = true;
	}
      }
    } else {
      Flit *  f = _input_channels[input]->Receive();
      if(gReservation)
	f = _ExpirationCheck(f, input);
      if(f) {
	++_received_flits[input];
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "Received flit " << f->id
		     << " from channel at input " << input
		     << "." << endl;
	}
	_in_queue_flits.insert(make_pair(input, f));
	activity = true;
      }
    }
  }
  return activity;
}

bool IQRouter::_ReceiveCredits( )
{
  bool activity = false;
  for(int output = 0; output < _outputs; ++output) {  
    Credit * const c = _output_credits[output]->Receive();
    if(c) {
      _proc_credits.push_back(make_pair(GetSimTime() + _credit_delay, 
					make_pair(c, output)));
      activity = true;
    }
  }
  return activity;
}


//------------------------------------------------------------------------------
// input queuing
//------------------------------------------------------------------------------

void IQRouter::_InputQueuing( )
{
  for(map<int, Flit *>::const_iterator iter = _in_queue_flits.begin();
      iter != _in_queue_flits.end();
      ++iter) {

    int const & input = iter->first;
    assert((input >= 0) && (input < _inputs));

    Flit * const & f = iter->second;
    assert(f);

    //voq related, could break things
    //int const & vc = f->vc;
    int vc = f->vc;
    assert((vc >= 0) && (vc < _real_vcs));

    Buffer * const cur_buf = _buf[input];

    if(_voq && vc2voq(vc)==-1){
      if(f->head){
	int old_vc = f->vc;
	
	_rf(this, f, input, _voq_init_route, false);
	set<OutputSet::sSetElement> const setlist = _voq_init_route->GetSet();
	for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
	    iset != setlist.end();
	    ++iset) {
	   
	  int  out_port = iset->output_port;
	  f->vc = old_vc;
	  vc = vc2voq(f->vc, out_port);
	  _voq_pid[input*_real_vcs+f->vc]=pair<int, int>(f->pid, vc);
	}
      } else {
	assert(_voq_pid[input*_real_vcs+f->vc].first==f->pid);
	vc = _voq_pid[input*_real_vcs+f->vc].second;
      }
    }
    
    _vc_activity[input*_vcs+vc]++;
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Adding flit " << f->id
		 << " to VC " << vc
		 << " at input " << input
		 << " (state: " << VC::VCSTATE[cur_buf->GetState(vc)];
      if(cur_buf->Empty(vc)) {
	*gWatchOut << ", empty";
      } else {
	assert(cur_buf->FrontFlit(vc));
	*gWatchOut << ", front: " << cur_buf->FrontFlit(vc)->id;
      }
      *gWatchOut << ")." << endl;
    }
    if(!cur_buf->AddFlit(vc, f)) {
      Error( "VC buffer overflow" );
    }
    ++_stored_flits[input];
    if(f->head) ++_active_packets[input];
    _bufferMonitor->write(input, f) ;

    if(cur_buf->GetState(vc) == VC::idle) {
      assert(cur_buf->FrontFlit(vc) == f);
      assert(f->head);
      assert(_switch_hold_vc[input*_input_speedup + vc%_input_speedup] != vc);
      if(_routing_delay) {
	cur_buf->SetState(vc, VC::routing);
	_route_vcs.push_back(make_pair(-1, make_pair(input, vc)));
      } else {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "Generating lookahead routing information for VC " << vc
		     << " at input " << input
		     << " (front: " << f->id
		     << ")." << endl;
	}
	if(_voq && is_voq_vc(vc)){
	  _res_voq_drop[input*_vcs+vc] = false;
	} else {
	  cur_buf->Route(vc, _rf, this, f, input);
	}
	cur_buf->SetState(vc, VC::vc_alloc);
	if(_speculative) {
	  _sw_alloc_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc),
							  -1)));
	}
	if(_vc_allocator ||  _VOQArbs  ) {
	  _vc_alloc_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc), 
							  -1)));
	}
      }
    } else if((cur_buf->GetState(vc) == VC::active) &&
	      (cur_buf->FrontFlit(vc) == f)) {
      if(_switch_hold_vc[input*_input_speedup + vc%_input_speedup] == vc) {
	_sw_hold_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc),
						       -1)));
      } else {
	_sw_alloc_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc), 
							-1)));
      }
    }
  }
  _in_queue_flits.clear();

  while(!_proc_credits.empty()) {

    pair<int, pair<Credit *, int> > const & item = _proc_credits.front();

    int const & time = item.first;
    if(GetSimTime() < time) {
      break;
    }

    Credit * const & c = item.second.first;
    assert(c);

    int const & output = item.second.second;
    assert((output >= 0) && (output < _outputs));
    
    BufferState * const dest_buf = _next_buf[output];
    
    dest_buf->ProcessCredit(c);
    c->Free();
    _proc_credits.pop_front();
  }
}


//------------------------------------------------------------------------------
// routing
//------------------------------------------------------------------------------

void IQRouter::_RouteEvaluate( )
{
  for(deque<pair<int, pair<int, int> > >::iterator iter = _route_vcs.begin();
      iter != _route_vcs.end();
      ++iter) {
    
    int const & time = iter->first;
    if(time >= 0) {
      break;
    }
    iter->first = GetSimTime() + _routing_delay - 1;
    
    int const & input = iter->second.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = iter->second.second;
    assert((vc >= 0) && (vc < _vcs));

    Buffer const * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::routing);

    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->head);

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Beginning routing for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
  }    
}

void IQRouter::_RouteUpdate( )
{
  while(!_route_vcs.empty()) {

    pair<int, pair<int, int> > const & item = _route_vcs.front();

    int const & time = item.first;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const & input = item.second.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = item.second.second;
    assert((vc >= 0) && (vc < _vcs));
    
    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::routing);

    Flit * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->head);

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Completed routing for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    if(_voq && is_voq_vc(vc)){
      _res_voq_drop[input*_vcs+vc] = false;
    } else {
      cur_buf->Route(vc, _rf, this, f, input);
    }
    cur_buf->SetState(vc, VC::vc_alloc);
    if(_speculative) {
      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second, -1)));
    }
    if(_vc_allocator ||  _VOQArbs  ) {
      _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second, -1)));
    }

    _route_vcs.pop_front();
  }
}


//------------------------------------------------------------------------------
// VC allocation
//------------------------------------------------------------------------------

void IQRouter::_VCAllocEvaluate( )
{
  bool watched = false;

  for(deque<pair<int, pair<pair<int, int>, int> > >::const_iterator iter = _vc_alloc_vcs.begin();
      iter != _vc_alloc_vcs.end();
      ++iter) {

    int const & time = iter->first;
    if(time >= 0) {
      break;
    }

    int const & input = iter->second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = iter->second.first.second;
    assert((vc >= 0) && (vc < _vcs));

    Buffer *cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::vc_alloc);

    Flit const * const f = cur_buf->FrontFlit(vc);

    //check for expiration
    if(VC_ALLOC_DROP && f && f->res_type == RES_TYPE_SPEC){
      if(( RESERVATION_QUEUING_DROP && f->exptime<GetSimTime()-cur_buf->TimeStamp(vc)) ||
	 (!RESERVATION_QUEUING_DROP && f->exptime<GetSimTime())){
	if(f->watch){
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "about to drop flit " << f->id
		     << " from input " << input <<" vc "<<vc
		     << "." << endl;
	}
	cur_buf->SetDrop(vc);
	continue;
      }
    }
    
    assert(f);
    assert(f->head);
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		 << "Beginning VC allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    const OutputSet *              route_set;
    if(_voq && is_voq_vc(vc)){
      if(_res_voq_drop[input*_vcs+vc]){
	route_set = cur_buf->GetRouteSet(vc);
      } else {
	route_set = _voq_route_set[input*_vcs+vc];
      }
    } else {
      route_set = cur_buf->GetRouteSet(vc);
    }
    assert(route_set);

    int const out_priority = cur_buf->GetPriority(vc);
    set<OutputSet::sSetElement> const setlist = route_set->GetSet();

    for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
	iset != setlist.end();
	++iset) {

      int const & out_port = iset->output_port;
      assert((out_port >= 0) && (out_port < _outputs));

      BufferState *dest_buf = _next_buf[out_port];

      for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc) {
	assert((out_vc >= 0) && (out_vc < _vcs));
	assert( iset->vc_end-iset->vc_start==0 || !_voq);
	int const & in_priority = iset->pri;


	//speculative buffer check
	if( RESERVATION_BUFFER_SIZE_DROP && VC_ALLOC_DROP && f && f->res_type == RES_TYPE_SPEC){
	  int next_size = dest_buf->Size(out_vc)-DEFAULT_CHANNEL_LATENCY*2;
	  next_size=MAX(next_size,0);

	  if(( RESERVATION_QUEUING_DROP && f->exptime-next_size<GetSimTime()-cur_buf->TimeStamp(vc)) ||
	     (!RESERVATION_QUEUING_DROP && f->exptime<GetSimTime()+next_size)){
	    if(f->watch){
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "about to drop flit " << f->id
			 << " from input " << input <<" vc "<<vc
			 << "." << endl;
	    }
	    
	    cur_buf->SetDrop(vc);
	    continue;
	  }
	}

	// On the input input side, a VC might request several output VCs. 
	// These VCs can be prioritized by the routing function, and this is 
	// reflected in "in_priority". On the output side, if multiple VCs are 
	// requesting the same output VC, the priority of VCs is based on the 
	// actual packet priorities, which is reflected in "out_priority".


	if(( _cut_through && dest_buf->IsAvailableFor(out_vc,cur_buf->FrontFlit(vc)->packet_size))||
	   (!_cut_through && dest_buf->IsAvailableFor(out_vc))) {
	  if(f->watch){
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "  Requesting VC " << out_vc
		       << " at output " << out_port 
		       << " (in_pri: " << in_priority
		       << ", out_pri: " << out_priority
		       << ")." << endl;
	    watched = true;
	  }
	  if(!_voq){
	    _vc_allocator->AddRequest(input*_vcs + vc, out_port*_real_vcs + out_vc, 0, in_priority, out_priority);
	  } else {
#ifdef USE_LARGE_ARB
	    _VOQArbs[out_port*_real_vcs + out_vc]->AddRequest(input*_vcs + vc,0,out_priority); 
#else
	    _vc_allocator->AddRequest(input*_vcs + vc, out_port*_real_vcs + out_vc, 0, in_priority, out_priority);
#endif
	  }
	} else {
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "  VC " << out_vc 
		       << " at output " << out_port 
		       << " is busy." << endl;
	}
      }
    }
  }

  if(watched) {
    *gWatchOut << GetSimTime() << " | " << _vc_allocator->FullName() << " | ";
    _vc_allocator->PrintRequests( gWatchOut );
  }
  if(!_voq){
    _vc_allocator->Allocate();
  } else {
#ifndef USE_LARGE_ARB
    _vc_allocator->Allocate();
#endif
  }

  if(watched) {
    *gWatchOut << GetSimTime() << " | " << _vc_allocator->FullName() << " | ";
    _vc_allocator->PrintGrants( gWatchOut );
  }

  for(deque<pair<int, pair<pair<int, int>, int> > >::iterator iter = _vc_alloc_vcs.begin();
      iter != _vc_alloc_vcs.end();
      ++iter) {

    int const & time = iter->first;
    if(time >= 0) {
      break;
    }
    iter->first = GetSimTime() + _vc_alloc_delay - 1;

    int const & input = iter->second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = iter->second.first.second;
    assert((vc >= 0) && (vc < _vcs));

    int & output_and_vc = iter->second.second;
    if(!_voq){
      output_and_vc = _vc_allocator->OutputAssigned(input * _vcs + vc);
    } else {
#ifdef USE_LARGE_ARB      
      Buffer * const cur_buf = _buf[input];
      const OutputSet *route_set;
      if(_voq && is_voq_vc(vc)){
	if(_res_voq_drop[input*_vcs+vc]){
	  route_set = cur_buf->GetRouteSet(vc);
	} else {
	  route_set = _voq_route_set[input*_vcs+vc];
	}
      } else {
	route_set = cur_buf->GetRouteSet(vc);
      }
      set<OutputSet::sSetElement> const setlist = route_set->GetSet();
      set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
      int out_port = iset->output_port;
      int out_vc = iset->vc_start;
      if(_VOQArbs[ _real_vcs*out_port+ out_vc]->Arbitrate()!=input * _vcs + vc){
	output_and_vc = -1;
      } else {
	output_and_vc =  _real_vcs*out_port+out_vc;
      }
#else
      output_and_vc = _vc_allocator->OutputAssigned(input * _vcs + vc);
#endif
    }
 
    if(output_and_vc >= 0) {

   
      int const match_output = output_and_vc / _real_vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _real_vcs;
      assert((match_vc >= 0) && (match_vc < _real_vcs));

      Buffer const * const cur_buf = _buf[input];
      assert(!cur_buf->Empty(vc));
      assert(cur_buf->GetState(vc) == VC::vc_alloc);

      Flit const * const f = cur_buf->FrontFlit(vc);
      assert(f);
      assert(f->head);
      


      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Assigning VC " << match_vc
		   << " at output " << match_output 
		   << " to VC " << vc
		   << " at input " << input
		   << "." << endl;
      }
    }
  }

  if(_vc_alloc_delay <= 1) {
    return;
  }

  for(deque<pair<int, pair<pair<int, int>, int> > >::iterator iter = _vc_alloc_vcs.begin();
      iter != _vc_alloc_vcs.end();
      ++iter) {
    
    int const & time = iter->first;
    assert(time >= 0);
    if(GetSimTime() < time) {
      break;
    }
    
    int const & output_and_vc = iter->second.second;
    
    if(output_and_vc >= 0) {
      
      int const match_output = output_and_vc / _real_vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _real_vcs;
      assert((match_vc >= 0) && (match_vc < _real_vcs));
      
      BufferState const * const dest_buf = _next_buf[match_output];
      
      if(!dest_buf->IsAvailableFor(match_vc)) {
	assert(!_cut_through);
	int const & input = iter->second.first.first;
	assert((input >= 0) && (input < _inputs));
	int const & vc = iter->second.first.second;
	assert((vc >= 0) && (vc < _vcs));
	
	Buffer const * const cur_buf = _buf[input];
	assert(!cur_buf->Empty(vc));
	assert(cur_buf->GetState(vc) == VC::vc_alloc);
	
	Flit const * const f = cur_buf->FrontFlit(vc);
	assert(f);
	assert(f->head);


	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  Discarding previously generated grant for VC " << vc
		     << " at input " << input
		     << ": VC " << match_vc
		     << " at output " << match_output
		     << " is no longer available." << endl;
	}
	iter->second.second = -1;
      }
    }
  }
}

void IQRouter::_VCAllocUpdate( )
{
  while(!_vc_alloc_vcs.empty()) {

    pair<int, pair<pair<int, int>, int> > const & item = _vc_alloc_vcs.front();

    int const & time = item.first;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const & input = item.second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = item.second.first.second;
    assert((vc >= 0) && (vc < _vcs));
    
    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::vc_alloc);
    
    Flit *f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->head);
   
    if(cur_buf->GetDrop(vc)){
      if(f->watch){
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "drop flit " << f->id
		   << " from channel at input " << input
		   << "." << endl;
      }

      //send dropped credit since the packet is removed from the buffer
    
      Flit* drop_f = NULL;
      gDropStats[_id][input]++;
      drop_f = trafficManager->DropPacket(input, f);
      drop_f->vc = f->vc;

      assert(f->head);
      if(RESERVATION_QUEUING_DROP){
	gStatDropLateness->AddSample((GetSimTime()-cur_buf->TimeStamp(vc))
				     -f->exptime);
      } else {
	gStatDropLateness->AddSample(GetSimTime()-f->exptime);
      }

      int pid_drop = f->pid;
      bool tail_dropped = false;
   

      while(!cur_buf->Empty(vc)){
	f = cur_buf->FrontFlit(vc);
	tail_dropped = f->tail;
	if(!f->head){
	  if(_out_queue_credits.count(input) == 0) {
	    _out_queue_credits.insert(make_pair(input, Credit::New()));
	  }
	  _out_queue_credits.find(input)->second->id=888;
	  _out_queue_credits.find(input)->second->vc.push_back(f->vc);
	}
	f->Free();
	cur_buf->RemoveFlit(vc);
	if(tail_dropped){
	  break;
	}
      }

   
      if(!tail_dropped){ //mark the vc entrace to drop futher flits
	dropped_pid[input][f->vc]=pid_drop;
      }
   
      //send drop nack
      cur_buf->SubstituteFrontFlit(vc,drop_f);
      if(_voq){
	_res_voq_drop[input*_vcs+vc] = true;
      }
      cur_buf->Route(vc, _rf, this, drop_f, input);
      cur_buf->ResetExpected(vc);
      _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
      _vc_alloc_vcs.pop_front();
      continue;
    }
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Completed VC allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    int const & output_and_vc = item.second.second;
    
    if(output_and_vc >= 0) {
      
      int const match_output = output_and_vc / _real_vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _real_vcs;
      assert((match_vc >= 0) && (match_vc < _real_vcs));
      
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Acquiring assigned VC " << match_vc
		   << " at output " << match_output
		   << "." << endl;
      }
      
      BufferState * const dest_buf = _next_buf[match_output];
      assert(( _cut_through && dest_buf->IsAvailableFor(match_vc,cur_buf->FrontFlit(vc)->packet_size))||
	     (!_cut_through && dest_buf->IsAvailableFor(match_vc)));
      
      dest_buf->TakeBuffer(match_vc);
      
      cur_buf->SetOutput(vc, match_output, match_vc);
      cur_buf->SetState(vc, VC::active);
      if(!_speculative) {
	_sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
      }
    } else {
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  No output VC allocated." << endl;
      }
      _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
    }
    _vc_alloc_vcs.pop_front();
  }
}


//------------------------------------------------------------------------------
// switch holding
//------------------------------------------------------------------------------

void IQRouter::_SWHoldEvaluate( )
{
  if(!_hold_switch_for_packet) {
    return;
  }

  for(deque<pair<int, pair<pair<int, int>, int> > >::iterator iter = _sw_hold_vcs.begin();
      iter != _sw_hold_vcs.end();
      ++iter) {
    
    int const & time = iter->first;
    if(time >= 0) {
      break;
    }
    iter->first = GetSimTime();
    
    int const & input = iter->second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = iter->second.first.second;
    assert((vc >= 0) && (vc < _vcs));
    
    Buffer const * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::active);
    
    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		 << "Beginning held switch allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    int const expanded_input = input * _input_speedup + vc % _input_speedup;
    assert(_switch_hold_vc[expanded_input] == vc);
    
    int const match_port = cur_buf->GetOutputPort(vc);
    assert((match_port >= 0) && (match_port < _outputs));
    int const match_vc = cur_buf->GetOutputVC(vc);
    assert((match_vc >= 0) && (match_vc < _real_vcs));
    
    int const expanded_output = match_port*_output_speedup + input%_output_speedup;
    assert(_switch_hold_in[expanded_input] == expanded_output);
    assert(!_switch_hold_in_skip[expanded_input]);

    BufferState const * const dest_buf = _next_buf[match_port];
    

    _input_request[expanded_input]++;

    if(!dest_buf->HasCreditFor(match_vc)) {

      assert(!_cut_through);
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Unable to reuse held connection from input " << input
		   << "." << (expanded_input % _input_speedup)
		   << " to output " << match_port
		   << "." << (expanded_output % _output_speedup)
		   << ": No credit available." << endl;
      }
      iter->second.second = -1;
    } else {
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Reusing held connection from input " << input
		   << "." << (expanded_input % _input_speedup)
		   << " to output " << match_port
		   << "." << (expanded_output % _output_speedup)
		   << "." << endl;
      }
      iter->second.second = expanded_output;
    }
  }
}

void IQRouter::_SWHoldUpdate( )
{
  while(!_sw_hold_vcs.empty()) {
    
    pair<int, pair<pair<int, int>, int> > const & item = _sw_hold_vcs.front();
    
    int const & time = item.first;
    if(time < 0) {
      break;
    }
    assert(GetSimTime() == time);
    
    int const & input = item.second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = item.second.first.second;
    assert((vc >= 0) && (vc < _vcs));
    
    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::active);
    
    Flit * const f = cur_buf->FrontFlit(vc);
    assert(f);
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Completed held switch allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    int const expanded_input = input * _input_speedup + vc % _input_speedup;
    assert(_switch_hold_vc[expanded_input] == vc);
    
    bool skip = false;
    if(_switch_hold_in_skip[expanded_input]){
      skip = true;
    }
    int const & expanded_output = item.second.second;
    if(expanded_output >= 0  && _switch_hold_out_skip[expanded_output]){
     skip = true;
    }    
   if(skip){   
     _switch_hold_in_skip[expanded_input]=false;
     _switch_hold_out_skip[expanded_output]=false;
     _sw_hold_vcs.push_back(make_pair(-1, make_pair(item.second.first,
						    -1)));
     _sw_hold_vcs.pop_front();
     continue;
   }

    if(expanded_output >= 0  && ( _output_buffer_size==-1 || _output_buffer[expanded_output].size()<size_t(_output_buffer_size))) {

      assert(_switch_hold_in[expanded_input] == expanded_output);
      assert(_switch_hold_out[expanded_output] == expanded_input);
      
      int const output = expanded_output / _output_speedup;
      assert((output >= 0) && (output < _outputs));
      assert(cur_buf->GetOutputPort(vc) == output);
      
      int const match_vc = cur_buf->GetOutputVC(vc);
      assert((match_vc >= 0) && (match_vc < _vcs));
      
      BufferState * const dest_buf = _next_buf[output];
      assert(!dest_buf->IsFullFor(match_vc));
      
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Scheduling switch connection from input " << input
		   << "." << (vc % _input_speedup)
		   << " to output " << output
		   << "." << (expanded_output % _output_speedup)
		   << "." << endl;
      }
      
      if(RESERVATION_QUEUING_DROP && f->res_type==RES_TYPE_SPEC && f->head){
	f->exptime-=(GetSimTime()-cur_buf->TimeStamp(vc));
      }
      cur_buf->RemoveFlit(vc);
      --_stored_flits[input];

      if(f->tail) --_active_packets[input];
      _bufferMonitor->read(input, f) ;
      
      f->hops++;
      f->vc = match_vc;
      
      dest_buf->SendingFlit(f);
      _input_grant[expanded_input]++;

      _crossbar_flits.push_back(make_pair(-1, make_pair(f, make_pair(expanded_input, expanded_output))));
      
      if(_out_queue_credits.count(input) == 0) {
	_out_queue_credits.insert(make_pair(input,Credit::New()));
      }
      if(_voq){
	int rvc = vc;
	int o = output;
	int vvc = voq2vc(rvc,o);
	_out_queue_credits.find(input)->second->id=777;
	_out_queue_credits.find(input)->second->vc.push_back(vvc);
      } else {
	_out_queue_credits.find(input)->second->vc.push_back(vc);
      }
      if(cur_buf->Empty(vc)) {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  Cancelling held connection from input " << input
		     << "." << (expanded_input % _input_speedup)
		     << " to " << output
		     << "." << (expanded_output % _output_speedup)
		     << ": No more flits." << endl;
	}
	assert(!_switch_hold_in_skip[expanded_input]);
	assert(!_switch_hold_out_skip[expanded_output]);
	_switch_hold_vc[expanded_input] = -1;
	_switch_hold_in[expanded_input] = -1;
	_switch_hold_out[expanded_output] = -1;
	_hold_cancels++;
	if(f->tail) {
	  cur_buf->SetState(vc, VC::idle);
	}
      } else {
	Flit const * const nf = cur_buf->FrontFlit(vc);
	assert(nf);
	if(f->tail) {
	  assert(nf->head);
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "  Cancelling held connection from input " << input
		       << "." << (expanded_input % _input_speedup)
		       << " to " << output
		       << "." << (expanded_output % _output_speedup)
		       << ": End of packet." << endl;
	  }
	  assert(!_switch_hold_in_skip[expanded_input]);
	  assert(!_switch_hold_out_skip[expanded_output]);

	  _switch_hold_vc[expanded_input] = -1;
	  _switch_hold_in[expanded_input] = -1;
	  _switch_hold_out[expanded_output] = -1;
	  if(_routing_delay) {
	    cur_buf->SetState(vc, VC::routing);
	    _route_vcs.push_back(make_pair(-1, item.second.first));
	  } else {
	    if(nf->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Generating lookahead routing information for VC " << vc
			 << " at input " << input
			 << " (front: " << nf->id
			 << ")." << endl;
	    }
	    if(_voq && is_voq_vc(vc)){
	      _res_voq_drop[input*_vcs+vc] = false;
	    } else {
	      cur_buf->Route(vc, _rf, this, nf, input);
	    }
	    cur_buf->SetState(vc, VC::vc_alloc);
	    if(_speculative) {
	      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							      -1)));
	    }
	    if(_vc_allocator || _VOQArbs) {
	      _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							      -1)));
	    }
	  }
	} else {
	  _sw_hold_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							 -1)));
	}
      }
    } else {
      
      int const held_expanded_output = _switch_hold_in[expanded_input];
      assert(held_expanded_output >= 0);
      
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Cancelling held connection from input " << input
		   << "." << (expanded_input % _input_speedup)
		   << " to " << (held_expanded_output / _output_speedup)
		   << "." << (held_expanded_output % _output_speedup)
		   << ": Flit not sent." << endl;
      }
      assert(!_switch_hold_in_skip[expanded_input]);
      assert(!_switch_hold_out_skip[held_expanded_output]);

      _hold_cancels++;
      _switch_hold_vc[expanded_input] = -1;
      _switch_hold_in[expanded_input] = -1;
      _switch_hold_out[held_expanded_output] = -1;
      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
						      -1)));
    }
    _sw_hold_vcs.pop_front();
  }
}


//------------------------------------------------------------------------------
// switch allocation
//------------------------------------------------------------------------------

bool IQRouter::_SWAllocAddReq(int input, int vc, int output)
{

  // When input_speedup > 1, the virtual channel buffers are interleaved to 
  // create multiple input ports to the switch. Similarily, the output ports 
  // are interleaved based on their originating input when output_speedup > 1.
  
  int const expanded_input = input * _input_speedup + vc % _input_speedup;
  int const expanded_output = output * _output_speedup + input % _output_speedup;
  
  Buffer const * const cur_buf = _buf[input];
  assert(!cur_buf->Empty(vc));
  assert((cur_buf->GetState(vc) == VC::active) || 
	 (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));
  
  Flit const * const f = cur_buf->FrontFlit(vc);
  assert(f);
  
  if(is_control_vc(vc) || 
     ((_switch_hold_in[expanded_input] < 0) && 
      (_switch_hold_out[expanded_output] < 0))) {
    
    Allocator * allocator = _sw_allocator;
    int prio = cur_buf->GetPriority(vc);
    
    if(_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)) {
      if(_spec_sw_allocator) {
	allocator = _spec_sw_allocator;
      } else {
	prio += numeric_limits<int>::min();
      }
    }

    Allocator::sRequest req;
    
    if(allocator->ReadRequest(req, expanded_input, expanded_output)) {
      if(RoundRobinArbiter::Supersedes(vc, prio, req.label, req.in_pri, 
				       _sw_rr_offset[expanded_input], _vcs)) {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  Replacing earlier request from VC " << req.label
		     << " for output " << output 
		     << "." << (expanded_output % _output_speedup)
		     << " with priority " << req.in_pri
		     << " (" << ((cur_buf->GetState(vc) == VC::active) ? 
				 "non-spec" : 
				 "spec")
		     << ", pri: " << prio
		     << ")." << endl;
	}
	allocator->RemoveRequest(expanded_input, expanded_output, req.label);
	allocator->AddRequest(expanded_input, expanded_output, vc, prio, prio);
	return true;
      }
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Output " << output
		   << "." << (expanded_output % _output_speedup)
		   << " was already requested by VC " << req.label
		   << " with priority " << req.in_pri
		   << " (pri: " << prio
		   << ")." << endl;
      }
      return false;
    }
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "  Requesting output " << output
		 << "." << (expanded_output % _output_speedup)
		 << " (" << ((cur_buf->GetState(vc) == VC::active) ? 
			     "non-spec" : 
			     "spec")
		 << ", pri: " << prio
		 << ")." << endl;
    }
    allocator->AddRequest(expanded_input, expanded_output, vc, prio, prio);
    return true;
  }
  if(f->watch) {
    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
	       << "  Ignoring output " << output
	       << "." << (expanded_output % _output_speedup)
	       << " due to switch hold (";
    if(_switch_hold_in[expanded_input] >= 0) {
      *gWatchOut << "input: " << input
		 << "." << (expanded_input % _input_speedup);
      if(_switch_hold_out[expanded_output] >= 0) {
	*gWatchOut << ", ";
      }
    }
    if(_switch_hold_out[expanded_output] >= 0) {
      *gWatchOut << "output: " << output
		 << "." << (expanded_output % _output_speedup);
    }
    *gWatchOut << ")." << endl;
  }
  return false;
}

void IQRouter::_SWAllocEvaluate( )
{
  bool watched = false;

  for(deque<pair<int, pair<pair<int, int>, int> > >::const_iterator iter = _sw_alloc_vcs.begin();
      iter != _sw_alloc_vcs.end();
      ++iter) {

    int const & time = iter->first;
    if(time >= 0) {
      break;
    }

    int const & input = iter->second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = iter->second.first.second;
    assert((vc >= 0) && (vc < _vcs));
    
    assert(_switch_hold_vc[input * _input_speedup + vc % _input_speedup] != vc);

    Buffer const * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert((cur_buf->GetState(vc) == VC::active) || 
	   (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));
    
    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		 << "Beginning switch allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    if(cur_buf->GetState(vc) == VC::active) {
      _input_request[input]++;
      
      int const dest_output = cur_buf->GetOutputPort(vc);
      assert((dest_output >= 0) && (dest_output < _outputs));
      int const dest_vc = cur_buf->GetOutputVC(vc);
      assert((dest_vc >= 0) && (dest_vc < _real_vcs));
      
      BufferState const * const dest_buf = _next_buf[dest_output];
  

      if( (gReservation||gECN) &&
	  is_control_vc(dest_vc) && 
	  (!dest_buf->HasCreditFor(dest_vc) || ( _output_buffer_size!=-1  && _output_control_buffer[dest_output].size()>=(size_t)(_output_buffer_size)))){

	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  VC control" << dest_vc 
		     << " at output " << dest_output 
		     << " is full." << endl;
	}
	continue;
      }
      else if(!dest_buf->HasCreditFor(dest_vc) || ( _output_buffer_size!=-1  && _output_buffer[dest_output].size()>=(size_t)(_output_buffer_size))) {

	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  VC " << dest_vc 
		     << " at output " << dest_output 
		     << " is full." << endl;
	}
	continue;
      }
      bool const requested = _SWAllocAddReq(input, vc, dest_output);
      watched |= requested && f->watch;
      continue;
    }
    assert(_speculative && (cur_buf->GetState(vc) == VC::vc_alloc));
    assert(f->head);
      
    // The following models the speculative VC allocation aspects of the 
    // pipeline. An input VC with a request in for an egress virtual channel 
    // will also speculatively bid for the switch regardless of whether the VC  
    // allocation succeeds.
    
    const OutputSet *route_set;
    if(_voq && is_voq_vc(vc)){
      if(_res_voq_drop[input*_vcs+vc]){
	route_set = cur_buf->GetRouteSet(vc);
      } else {
	route_set = _voq_route_set[input*_vcs+vc];
      }
    } else {
      route_set = cur_buf->GetRouteSet(vc);
    }
    assert(route_set);
    
    set<OutputSet::sSetElement> const setlist = route_set->GetSet();
    
    for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
	iset != setlist.end();
	++iset) {
      
      int const & dest_output = iset->output_port;
      assert((dest_output >= 0) && (dest_output < _outputs));
      
      // for lower levels of speculation, ignore credit availability and always 
      // issue requests for all output ports in route set
      
      bool do_request;
      
      if(_spec_check_elig) {
	assert(false);
	do_request = false;	
	// for higher levels of speculation, check if at least one suitable VC 
	// is available at the current output
	
	BufferState const * const dest_buf = _next_buf[dest_output];
	
	for(int dest_vc = iset->vc_start; dest_vc <= iset->vc_end; ++dest_vc) {
	  assert((dest_vc >= 0) && (dest_vc < _real_vcs));
	  if( (gReservation||gECN) &&
	      is_control_vc(dest_vc) && 
	      (dest_buf->IsAvailableFor(dest_vc) && ( _output_buffer_size==-1 || _output_control_buffer[dest_output].size()<(size_t)(_output_buffer_size))) ){

	    do_request = true;
	    break;
	  } else if(dest_buf->IsAvailableFor(dest_vc) && ( _output_buffer_size==-1 || _output_buffer[dest_output].size()<(size_t)(_output_buffer_size))) {
	    do_request = true;
	    break;
	  }
	}
      } else {
	do_request = true;
      }
      
      if(do_request) { 
	bool const requested = _SWAllocAddReq(input, vc, dest_output);
	watched |= requested && f->watch;
      } else {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  Output " << dest_output 
		     << " has no suitable VCs available." << endl;
	}
      }
    }
  }
  
  if(watched) {
    *gWatchOut << GetSimTime() << " | " << _sw_allocator->FullName() << " | ";
    _sw_allocator->PrintRequests(gWatchOut);
    if(_spec_sw_allocator) {
      *gWatchOut << GetSimTime() << " | " << _spec_sw_allocator->FullName() << " | ";
      _spec_sw_allocator->PrintRequests(gWatchOut);
    }
  }
  
  _sw_allocator->Allocate();
  if(_spec_sw_allocator)
    _spec_sw_allocator->Allocate();
  
  if(watched) {
    *gWatchOut << GetSimTime() << " | " << _sw_allocator->FullName() << " | ";
    _sw_allocator->PrintGrants(gWatchOut);
    if(_spec_sw_allocator) {
      *gWatchOut << GetSimTime() << " | " << _spec_sw_allocator->FullName() << " | ";
      _spec_sw_allocator->PrintGrants(gWatchOut);
    }
  }
  
  for(deque<pair<int, pair<pair<int, int>, int> > >::iterator iter = _sw_alloc_vcs.begin();
      iter != _sw_alloc_vcs.end();
      ++iter) {

    int const & time = iter->first;
    if(time >= 0) {
      break;
    }
    iter->first = GetSimTime() + _sw_alloc_delay - 1;

    int const & input = iter->second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = iter->second.first.second;
    assert((vc >= 0) && (vc < _vcs));

    Buffer const * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert((cur_buf->GetState(vc) == VC::active) || 
	   (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));
    
    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    
    int const expanded_input = input * _input_speedup + vc % _input_speedup;

    int & expanded_output = iter->second.second;
    expanded_output = _sw_allocator->OutputAssigned(expanded_input);

    if(expanded_output >= 0) {
      assert((expanded_output % _output_speedup) == (input % _output_speedup));
      if(_sw_allocator->ReadRequest(expanded_input, expanded_output) == vc) {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "Assigning output " << (expanded_output / _output_speedup)
		     << "." << (expanded_output % _output_speedup)
		     << " to VC " << vc
		     << " at input " << input
		     << "." << (vc % _input_speedup)
		     << "." << endl;
	}
	_sw_rr_offset[expanded_input] = (vc + _input_speedup) % _vcs;
      } else {
	expanded_output = -1;
      }
    } else if(_spec_sw_allocator) {
      expanded_output = _spec_sw_allocator->OutputAssigned(expanded_input);
      if(expanded_output >= 0) {
	assert((expanded_output % _output_speedup) == (input % _output_speedup));
	if(_spec_mask_by_reqs && 
	   _sw_allocator->OutputHasRequests(expanded_output)) {
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Discarding speculative grant for VC " << vc
		       << " at input " << input
		       << "." << (vc % _input_speedup)
		       << " because output " << (expanded_output / _output_speedup)
		       << "." << (expanded_output % _output_speedup)
		       << " has non-speculative requests." << endl;
	  }
	  expanded_output = -1;
	} else if(!_spec_mask_by_reqs &&
		  (_sw_allocator->InputAssigned(expanded_output) >= 0)) {
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Discarding speculative grant for VC " << vc
		       << " at input " << input
		       << "." << (vc % _input_speedup)
		       << " because output " << (expanded_output / _output_speedup)
		       << "." << (expanded_output % _output_speedup)
		       << " has a non-speculative grant." << endl;
	  }
	  expanded_output = -1;
	} else if(_spec_sw_allocator->ReadRequest(expanded_input, 
						  expanded_output) == vc) {
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Assigning output " << (expanded_output / _output_speedup)
		       << "." << (expanded_output % _output_speedup)
		       << " to VC " << vc
		       << " at input " << input
		       << "." << (vc % _input_speedup)
		       << "." << endl;
	  }
	  _sw_rr_offset[expanded_input] = (vc + _input_speedup) % _vcs;
	} else {
	  expanded_output = -1;
	}
      }
    }
  }
  
  if(!_speculative && (_sw_alloc_delay <= 1)) {
    return;
  }

  for(deque<pair<int, pair<pair<int, int>, int> > >::iterator iter = _sw_alloc_vcs.begin();
      iter != _sw_alloc_vcs.end();
      ++iter) {

    int const & time = iter->first;
    assert(time >= 0);
    if(GetSimTime() < time) {
      break;
    }

    int const & expanded_output = iter->second.second;
    
    if(expanded_output >= 0) {
      
      int const output = expanded_output / _output_speedup;
      assert((output >= 0) && (output < _outputs));
      
      BufferState const * const dest_buf = _next_buf[output];
      
      int const & input = iter->second.first.first;
      assert((input >= 0) && (input < _inputs));
      assert((input % _output_speedup) == (expanded_output % _output_speedup));
      int const & vc = iter->second.first.second;
      assert((vc >= 0) && (vc < _real_vcs));
      
      int const expanded_input = input * _input_speedup + vc % _input_speedup;
      assert(_switch_hold_vc[expanded_input] != vc);
      
      Buffer const * const cur_buf = _buf[input];
      assert(!cur_buf->Empty(vc));
      assert((cur_buf->GetState(vc) == VC::active) ||
	     (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));
      
      Flit const * const f = cur_buf->FrontFlit(vc);
      assert(f);
      
      if((!is_control_vc(vc)) &&
	 ((_switch_hold_in[expanded_input] >= 0) ||
	  (_switch_hold_out[expanded_output] >= 0))) {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "Discarding grant from input " << input
		     << "." << (vc % _input_speedup)
		     << " to output " << output
		     << "." << (expanded_output % _output_speedup)
		     << " due to conflict with held connection at ";
	  if(_switch_hold_in[expanded_input] >= 0) {
	    *gWatchOut << "input";
	  }
	  if((_switch_hold_in[expanded_input] >= 0) && 
	     (_switch_hold_out[expanded_output] >= 0)) {
	    *gWatchOut << " and ";
	  }
	  if(_switch_hold_out[expanded_output] >= 0) {
	    *gWatchOut << "output";
	  }
	  *gWatchOut << "." << endl;
	}
	iter->second.second = -1;
      } else if(_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)) {
	assert(false);
	assert(f->head);

	if(_vc_allocator || _VOQArbs) { // separate VC and switch allocators
	  assert(false);
	  int const output_and_vc = _vc_allocator->OutputAssigned(input*_vcs+vc);

	  if(output_and_vc < 0) {
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Discarding grant from input " << input
			 << "." << (vc % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << " due to misspeculation." << endl;
	    }
	    iter->second.second = -1;
	  } else if((output_and_vc / _real_vcs) != output) {
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Discarding grant from input " << input
			 << "." << (vc % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << " due to port mismatch between VC and switch allocator." << endl;
	    }
	    iter->second.second = -1;
	  } else if(!dest_buf->HasCreditFor((output_and_vc % _real_vcs))) {
	    assert(!_cut_through);
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Discarding grant from input " << input
			 << "." << (vc % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << " due to lack of credit." << endl;
	    }
	    iter->second.second = -1;
	  }

	} else { // VC allocation is piggybacked onto switch allocation

	  const OutputSet * route_set;
	  if(_voq && is_voq_vc(vc)){
	    if(_res_voq_drop[input*_vcs+vc]){
	      route_set = cur_buf->GetRouteSet(vc);
	    } else {
	      route_set = _voq_route_set[input*_vcs+vc];
	    }
	  } else {
	    route_set = cur_buf->GetRouteSet(vc);
	  }

	  assert(route_set);

	  set<OutputSet::sSetElement> const setlist = route_set ->GetSet();

	  bool found_vc = false;

	  for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
	      iset != setlist.end();
	      ++iset) {
	    if(iset->output_port == output) {
	      for(int out_vc = iset->vc_start; 
		  out_vc <= iset->vc_end; 
		  ++out_vc) {
		assert((out_vc >= 0) && (out_vc < _real_vcs));
		if(dest_buf->IsAvailableFor(out_vc) && 
		   dest_buf->HasCreditFor(out_vc)) {
		  found_vc = true;
		  break;
		}
		if(found_vc) {
		  break;
		}
	      }
	    }
	  }

	  if(!found_vc) {
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Discarding grant from input " << input
			 << "." << (vc % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << " because no suitable output VC for piggyback allocation is available." << endl;
	    }
	    iter->second.second = -1;
	  }

	}

      } else {
	assert(cur_buf->GetOutputPort(vc) == output);
	
	int const match_vc = cur_buf->GetOutputVC(vc);
	assert((match_vc >= 0) && (match_vc < _real_vcs));

	if(!dest_buf->HasCreditFor(match_vc)) {
	  assert(!_cut_through);
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "  Discarding grant from input " << input
		       << "." << (vc % _input_speedup)
		       << " to output " << output
		       << "." << (expanded_output % _output_speedup)
		       << " due to lack of credit." << endl;
	  }
	  iter->second.second = -1;
	} else {
	  if(_hold_switch_for_packet && is_control_vc(vc)){
	    if(_switch_hold_in[expanded_input]>=0)
	      _switch_hold_in_skip[expanded_input]=true;
	    if(_switch_hold_out[expanded_output]>=0)
	      _switch_hold_out_skip[expanded_output]=true;
	  }
	}
      }
    }
  }
}

void IQRouter::_SWAllocUpdate( )
{
  while(!_sw_alloc_vcs.empty()) {

    pair<int, pair<pair<int, int>, int> > const & item = _sw_alloc_vcs.front();

    int const & time = item.first;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const & input = item.second.first.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = item.second.first.second;
    assert((vc >= 0) && (vc < _vcs));
    
    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert((cur_buf->GetState(vc) == VC::active) ||
	   (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));
    
    Flit * const f = cur_buf->FrontFlit(vc);
    assert(f);
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Completed switch allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    int const & expanded_output = item.second.second;
    
    if(expanded_output >= 0) {
      
      int const expanded_input = input * _input_speedup + vc % _input_speedup;
      assert(_switch_hold_vc[expanded_input] < 0 ||is_control_vc(vc));
      assert(_switch_hold_in[expanded_input] < 0||is_control_vc(vc));
      assert(_switch_hold_out[expanded_output] < 0||is_control_vc(vc));

      int const output = expanded_output / _output_speedup;
      assert((output >= 0) && (output < _outputs));

      BufferState * const dest_buf = _next_buf[output];

      int match_vc;

      if((!_vc_allocator && !_VOQArbs) && (cur_buf->GetState(vc) == VC::vc_alloc)) {

	int const & cl = f->cl;
	assert((cl >= 0) && (cl < _classes));

	int const & vc_offset = _vc_rr_offset[output*_classes+cl];

	match_vc = -1;
	int match_prio = numeric_limits<int>::min();

	const OutputSet *route_set;
	if(_voq && is_voq_vc(vc)){
	  if(_res_voq_drop[input*_vcs+vc]){
	    route_set = cur_buf->GetRouteSet(vc);
	  } else {
	    route_set = _voq_route_set[input*_vcs+vc];
	  }
	} else {
	  route_set = cur_buf->GetRouteSet(vc);
	}

	set<OutputSet::sSetElement> const setlist = route_set->GetSet();

	for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
	    iset != setlist.end();
	    ++iset) {
	  if(iset->output_port == output) {
	    for(int out_vc = iset->vc_start; 
		out_vc <= iset->vc_end; 
		++out_vc) {
	      assert((out_vc >= 0) && (out_vc < _real_vcs));
	      
	      // FIXME: This check should probably be performed in Evaluate(), 
	      // not Update(), as the latter can cause the outcome to depend on 
	      // the order of evaluation!
	      if(dest_buf->IsAvailableFor(out_vc) && 
		 dest_buf->HasCreditFor(out_vc) &&
		 ((match_vc < 0) || 
		  RoundRobinArbiter::Supersedes(out_vc, iset->pri, 
						match_vc, match_prio, 
						vc_offset, _real_vcs))) {
		match_vc = out_vc;
		match_prio = iset->pri;
	      }
	    }	
	  }
	}
	assert(match_vc >= 0);

	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  Allocating VC " << match_vc
		     << " at output " << output
		     << " via piggyback VC allocation." << endl;
	}

	cur_buf->SetState(vc, VC::active);
	cur_buf->SetOutput(vc, output, match_vc);
	dest_buf->TakeBuffer(match_vc);

	_vc_rr_offset[output*_classes+cl] = (match_vc + 1) % _real_vcs;

      } else {

	assert(cur_buf->GetOutputPort(vc) == output);

	match_vc = cur_buf->GetOutputVC(vc);
	assert(!dest_buf->IsFullFor(match_vc));

      }
      assert((match_vc >= 0) && (match_vc < _real_vcs));

      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Scheduling switch connection from input " << input
		   << "." << (vc % _input_speedup)
		   << " to output " << output
		   << "." << (expanded_output % _output_speedup)
		   << "." << endl;
      }

      if(RESERVATION_QUEUING_DROP && f->res_type==RES_TYPE_SPEC && f->head){
	f->exptime-=(GetSimTime()-cur_buf->TimeStamp(vc));
      }
 
      cur_buf->RemoveFlit(vc);
      --_stored_flits[input];
      if(f->tail) --_active_packets[input];
      _bufferMonitor->read(input, f) ;

      f->hops++;
      f->vc = match_vc;

      dest_buf->SendingFlit(f);
 
      _input_grant[expanded_input]++;


      _crossbar_flits.push_back(make_pair(-1, make_pair(f, make_pair(expanded_input, expanded_output))));

      if(_out_queue_credits.count(input) == 0) {
	_out_queue_credits.insert(make_pair(input, Credit::New()));
      }
      if(_voq){
	int rvc = vc;
	int o = output;
	int vvc = voq2vc(rvc,o);
	//is the head flit of a spec is replaced by nack
	if(f->res_type==RES_TYPE_NACK && !is_control_vc(vc)){
	  vvc=RES_RESERVED_VCS;
	}
	_out_queue_credits.find(input)->second->id=777;
	_out_queue_credits.find(input)->second->vc.push_back(vvc);
      } else {
	_out_queue_credits.find(input)->second->vc.push_back(vc);
      }
      if(cur_buf->Empty(vc)) {
	if(f->tail) {
	  cur_buf->SetState(vc, VC::idle);
	}
      } else {
	Flit const * const nf = cur_buf->FrontFlit(vc);
	assert(nf);
	if(f->tail) {
	  assert(nf->head);
	  if(_routing_delay) {
	    cur_buf->SetState(vc, VC::routing);
	    _route_vcs.push_back(make_pair(-1, item.second.first));
	  } else {
	    if(nf->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Generating lookahead routing information for VC " << vc
			 << " at input " << input
			 << " (front: " << nf->id
			 << ")." << endl;
	    }
	    if(_voq && is_voq_vc(vc)){
	      _res_voq_drop[input*_vcs+vc] = false;
	    } else {
	      cur_buf->Route(vc, _rf, this, nf, input);
	    }
	    cur_buf->SetState(vc, VC::vc_alloc);
	    if(_speculative) {
	      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							      -1)));
	    }
	    _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							    -1)));
	  }
	} else {
	  if(_hold_switch_for_packet && !is_control_vc(vc)) {
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Setting up switch hold for VC " << vc
			 << " at input " << input
			 << "." << (expanded_input % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << "." << endl;
	    }
	    _holds++;
	    _switch_hold_vc[expanded_input] = vc;
	    _switch_hold_in[expanded_input] = expanded_output;
	    _switch_hold_out[expanded_output] = expanded_input;
	    _sw_hold_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							   -1)));
	  } else {
	    _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							    -1)));
	  }
	}
      }
    } else {
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  No output port allocated." << endl;
      }
      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first, -1)));
    }
    _sw_alloc_vcs.pop_front();
  }
}


//------------------------------------------------------------------------------
// switch traversal
//------------------------------------------------------------------------------

void IQRouter::_SwitchEvaluate( )
{
  for(deque<pair<int, pair<Flit *, pair<int, int> > > >::iterator iter = _crossbar_flits.begin();
      iter != _crossbar_flits.end();
      ++iter) {
    
    int const & time = iter->first;
    if(time >= 0) {
      break;
    }
    iter->first = GetSimTime() + _crossbar_delay - 1;

    Flit const * const & f = iter->second.first;
    assert(f);

    int const & expanded_input = iter->second.second.first;
    int const & expanded_output = iter->second.second.second;
      
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Beginning crossbar traversal for flit " << f->id
		 << " from input " << (expanded_input / _input_speedup)
		 << "." << (expanded_input % _input_speedup)
		 << " to output " << (expanded_output / _output_speedup)
		 << "." << (expanded_output % _output_speedup)
		 << "." << endl;
    }
  }
}

void IQRouter::_SwitchUpdate( )
{
  while(!_crossbar_flits.empty()) {

    pair<int, pair<Flit *, pair<int, int> > > const & item = _crossbar_flits.front();

    int const & time = item.first;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    Flit *  f = item.second.first;
    assert(f);

    int const & expanded_input = item.second.second.first;
    int const input = expanded_input / _input_speedup;
    assert((input >= 0) && (input < _inputs));
    int const & expanded_output = item.second.second.second;
    int const output = expanded_output / _output_speedup;
    assert((output >= 0) && (output < _outputs));

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Completed crossbar traversal for flit " << f->id
		 << " from input " << input
		 << "." << (expanded_input % _input_speedup)
		 << " to output " << output
		 << "." << (expanded_output % _output_speedup)
		 << "." << endl;
    }
    _switchMonitor->traversal(input, output, f) ;

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Buffering flit " << f->id
		 << " at output " << output
		 << "." << endl;
    }
    if( (gECN || gReservation) &&
	is_control_vc(f->vc)){ 
      _output_control_buffer[output].push(f);
    } else {
      _output_buffer[output].push(f);
    }


    _crossbar_flits.pop_front();
  }
}


//------------------------------------------------------------------------------
// output queuing
//------------------------------------------------------------------------------

void IQRouter::_OutputQueuing( )
{
  for(map<int, Credit *>::const_iterator iter = _out_queue_credits.begin();
      iter != _out_queue_credits.end();
      ++iter) {

    int const & input = iter->first;
    assert((input >= 0) && (input < _inputs));

    Credit * const & c = iter->second;
    assert(c);
    assert(!c->vc.empty());

    _credit_buffer[input].push(c);
  }
  _out_queue_credits.clear();
}

//------------------------------------------------------------------------------
// write outputs
//------------------------------------------------------------------------------

void IQRouter::_SendFlits( )
{

  for(int i = 0; i<_outputs; i++){
    if(gECN){
      if(_voq && _use_voq_size){
	int voq_size =0;
	for(int j = 0;  j<_inputs; j++){
	  voq_size+=_buf[j]->GetSize(vc2voq(ECN_RESERVED_VCS,i));
	}	
	if(_output_hysteresis[i]){
	  _output_hysteresis[i] = (voq_size >=((size_t)ECN_CONGEST_THRESHOLD-ECN_BUFFER_HYSTERESIS));
	} else {
	  _output_hysteresis[i] = (voq_size >=((size_t)ECN_CONGEST_THRESHOLD+ECN_BUFFER_HYSTERESIS));
	}
      } else {
	if(_output_hysteresis[i]){
	  _output_hysteresis[i] = (_output_buffer[i].size() >=((size_t)ECN_CONGEST_THRESHOLD-ECN_BUFFER_HYSTERESIS));
	} else {
	  _output_hysteresis[i] = (_output_buffer[i].size() >=((size_t)ECN_CONGEST_THRESHOLD+ECN_BUFFER_HYSTERESIS));
	}
      }
      for(int j = 0; j<_real_vcs; j++){
	BufferState * const dest_buf = _next_buf[i];
	if(_credit_hysteresis[i*_real_vcs+j]){
	  _credit_hysteresis[i*_real_vcs+j] = ( (dest_buf->Size(j)-_output_buffer[i].size()) < ECN_BUFFER_THRESHOLD+ECN_CREDIT_HYSTERESIS);
	} else {
	  _credit_hysteresis[i*_real_vcs+j] = ( (dest_buf->Size(j)-_output_buffer[i].size()) < ECN_BUFFER_THRESHOLD-ECN_CREDIT_HYSTERESIS);
	}
      }
    }
  }
  for ( int output = 0; output < _outputs; ++output ) {
    if((gECN||gReservation) &&
       !_output_control_buffer[output].empty( )) {
      Flit * const f = _output_control_buffer[output].front( );
      assert(f);     

      _output_control_buffer[output].pop( );
      ++_sent_flits[output];
      if(f->watch)
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Sending control flit " << f->id
		   << " to channel at output " << output
		   << "." << endl;
      if(gTrace) {
	cout << "Outport " << output << endl << "Stop Mark" << endl;
      }
      _output_channels[output]->Send( f );
    } else    if ( !_output_buffer[output].empty( ) ) {
      Flit * const f = _output_buffer[output].front( );

      assert(f);      
      //      BufferState * const dest_buf = _next_buf[output];
      if(gECN && f->head && !f->fecn ){	
	if(_output_hysteresis[output] &&
	   _credit_hysteresis[output*_real_vcs+f->vc]){
	  _ECN_activated[output*_real_vcs+f->vc]++;
	  f->fecn= true; 
	}
      }
	

      _output_buffer[output].pop( );
      ++_sent_flits[output];
      if(f->watch)
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Sending flit " << f->id
		   << " to channel at output " << output
		   << "." << endl;
      if(gTrace) {
	cout << "Outport " << output << endl << "Stop Mark" << endl;
      }
      _output_channels[output]->Send( f );
    }
  }
}

void IQRouter::_SendCredits( )
{
  for ( int input = 0; input < _inputs; ++input ) {
    if ( !_credit_buffer[input].empty( ) ) {
      Credit * const c = _credit_buffer[input].front( );
      assert(c);
      _credit_buffer[input].pop( );
      _input_credits[input]->Send( c );
    }
  }
}


//------------------------------------------------------------------------------
// misc.
//------------------------------------------------------------------------------

void IQRouter::Display( ostream & os ) const
{
  for ( int input = 0; input < _inputs; ++input ) {
    _buf[input]->Display( os );
  }
}

int IQRouter::GetCredit(int out, int vc_begin, int vc_end ) const
{
  assert((out >= 0) && (out < _outputs));
  assert(vc_begin < _vcs);
  assert(vc_end < _vcs);
  assert(vc_end >= vc_begin);

  BufferState const * const dest_buf = _next_buf[out];
  
  int const start = (vc_begin >= 0) ? vc_begin : 0;
  int const end = (vc_begin >= 0) ? vc_end : (_vcs - 1);

  int size = 0;
  for (int v = start; v <= end; v++)  {
    size+= dest_buf->Size(v);
  }
  return size;
}

int IQRouter::GetBuffer(int i) const {
  assert(i < _inputs);

  int size = 0;
  int const i_start = (i >= 0) ? i : 0;
  int const i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input) {
    for(int vc = 0; vc < _vcs; ++vc) {
      size += _buf[input]->GetSize(vc);
    }
  }
  return size;
}

vector<int> IQRouter::GetBuffers(int i) const {
  assert(i < _inputs);

  vector<int> sizes(_vcs);
  int const i_start = (i >= 0) ? i : 0;
  int const i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input) {
    for(int vc = 0; vc < _vcs; ++vc) {
      sizes[vc] += _buf[input]->GetSize(vc);
    }
  }
  return sizes;
}
