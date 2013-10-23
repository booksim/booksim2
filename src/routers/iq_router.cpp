// $Id$

/*
  Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this 
  list of conditions and the following disclaimer.
  Redistributions in binary form must reproduce the above copyright notice, this
  list of conditionWs and the following disclaimer in the documentation and/or
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
extern map<int, vector<int> > gDropInStats;
extern map<int, vector<int> > gDropOutStats;
extern map<int, vector<int> > gChanDropStats;
extern Stats* gStatDropLateness;

extern bool RESERVATION_QUEUING_DROP;
extern bool VC_ALLOC_DROP;
extern int ECN_BUFFER_THRESHOLD;
extern int ECN_CONGEST_THRESHOLD;
extern int ECN_BUFFER_HYSTERESIS;
extern int ECN_CREDIT_HYSTERESIS;

extern int PB_THRESHOLD;
extern bool RESERVATION_BUFFER_SIZE_DROP;
extern bool RESERVATION_ADAPT_SPEC_KILL;

bool ADAPTIVE_PERFECT_PB= true;

//improves large simulation performance
#define USE_LARGE_ARB
//#define ENABLE_ROUTER_STATS

#define MAX(X,Y) ((X)>(Y)?(X):(Y))
#define MIN(X,Y) ((X)<(Y)?(X):(Y))
int router_global_index(int rid, int output){
  //this does not work for dragonfull
  int router_grp_position = rid % gA;
  int channel_router_position = (output-(gK+gA-1));
  assert(channel_router_position >=0);
  return router_grp_position*gK+
    channel_router_position;
}


IQRouter::IQRouter( Configuration const & config, Module *parent, 
		    string const & name, int id, int inputs, int outputs )
  : Router( config, parent, name, id, inputs, outputs ), _active(false)
{

  ADAPTIVE_PERFECT_PB=(config.GetInt("adaptive_perfect_pb")==1);


  /////////////////////
  //VC setup parameters
  _cut_through = (config.GetInt("cut_through")==1);

  assert(_cut_through); //all the output buffer changes requires this
  assert(!_cut_through || (config.GetInt("vc_busy_when_full")==1));

  _ecn_use_voq_size=(config.GetInt("use_voq_size")==1);

  _voq = (config.GetInt("voq") ==1);
  _spec_voq = config.GetInt("reservation_spec_voq");
  _num_vcs=config.GetInt( "num_vcs" );

  if(_voq){
    if(gReservation){
      _ctrl_vcs = gSpecVCStart;
      _spec_vcs = gNSpecVCStart-gSpecVCStart;
      if(_spec_voq){
	_special_vcs =  _ctrl_vcs; //spec gets included in data vc
      } else {
	_special_vcs = gNSpecVCStart;;
      }
    } else if(gECN){
      _ctrl_vcs=gNSpecVCStart;;
      _special_vcs=_ctrl_vcs;
    } else {
      _ctrl_vcs= 0;
      _special_vcs = 0;
    }
    _data_vcs = _num_vcs -  _special_vcs;

    _vcs = _special_vcs + outputs*_data_vcs;

    _voq_pid.resize(inputs*(_num_vcs));
    _res_voq_drop.resize(inputs*_vcs, false);

  } else {
    assert(false);
    //this is not complete, too lazy to copy from above
    _vcs         = config.GetInt( "num_vcs" );
  }
  
  ////////////////////////////
  //Adaptive routing parameters
  _remove_credit_rtt = (config.GetInt("remove_credit_rtt")==1);
  _track_routing_commitment = (config.GetInt("track_routing_commitment")==1);

  if(gPB){
    _global_table = new bool[gG-1];
    for(int i = 0; i<gG-1; i++){
      _global_table[i] = false;
    }
    if(ADAPTIVE_PERFECT_PB){
      _global_array= new int[gA];
      for(int i = 0; i<gA; i++){
	_global_array[i] = 0;
      }
    }
  }

  if(gCRT){
    debug_delay_map=new long [inputs];
    for(int i = 0; i<inputs; i++) debug_delay_map[i] = 0;
    _crt_delay =  new int[outputs*_num_vcs];
    _crt =  new int[outputs*_num_vcs];
    for(int i = 0; i<outputs*_num_vcs; i++){
      _crt_delay[i] = 0;
      _crt[i] = 0;
    }
    _credit_delay_queue.resize(outputs);
  } else {
    _credit_buffer.resize(_inputs); 
  }


  if( _track_routing_commitment){
    _current_bandwidth_commitment = new int[outputs*_num_vcs];
    _next_bandwidth_commitment= new int[outputs*_num_vcs];
    for(int i = 0; i<outputs*_num_vcs; i++){
      _current_bandwidth_commitment[i] = 0;
      _next_bandwidth_commitment[i] = 0;
    }
  }



  _holds = 0;
  _hold_cancels = 0;
  
  _classes     = config.GetInt( "classes" );

  /////////////////////
  //ECN parameters  
  _vc_ecn.resize(_vcs*outputs,false);
  _output_hysteresis.resize(outputs,false);
  _credit_hysteresis.resize(_vcs*outputs,false);
  _ECN_activated.resize((_num_vcs)*outputs,0);

  //converting flits to nacks does nto support speculation yet
  //need to modifity the sw_alloc_vcs queue

  _speculative = (config.GetInt("speculative") > 0);
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


  ////////////
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
      _vc_allocator = NULL;_VOQArbs = new LargeRoundRobinArbiter*[_num_vcs*_outputs];
      for(int i = 0; i<_num_vcs*_outputs; i++){
	_VOQArbs[i] = new LargeRoundRobinArbiter("inject_arb",
						 _vcs*_inputs);
      }
#else
      _VOQArbs = NULL;_vc_allocator = Allocator::NewAllocator( this, "vc_allocator", alloc_type,_vcs*_inputs, _num_vcs*_outputs, iters, arb_type );
#endif	     
      
    } else {
      _vc_allocator = Allocator::NewAllocator( this, "vc_allocator", 
					       alloc_type,
					       _vcs*_inputs, 
					       _num_vcs*_outputs,
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
  _output_buffer.resize(_outputs); 
  for(int o = 0; o<_outputs; o++){
    _output_buffer[o] = new OutputBuffer(config, this, "output_buffer");
  }
  if(gReservation){
    for(int i = _ctrl_vcs; i< (_ctrl_vcs+_spec_vcs); i++){
      OutputBuffer::SetSpecVC(i, config.GetInt("spec_output_buffer_size"));
    }
  }

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
  
  _input_head_time.resize(_inputs, 0);

  dropped_pid = new int*[_inputs];
  for(int i = 0; i<_inputs; i++){
    dropped_pid[i] = new int[_vcs];
    for(int j = 0; j<_vcs; j++)
      dropped_pid[i][j] = -1;
  }

#ifdef  ENABLE_ROUTER_STATS
  gDropInStats.insert(pair<int,  vector<int> >(_id, vector<int>() ));
  gDropInStats[id].resize(inputs,0);
  gDropOutStats.insert(pair<int,  vector<int> >(_id, vector<int>() ));
  gDropOutStats[id].resize(inputs,0);
  gChanDropStats.insert(pair<int,  vector<int> >(_id, vector<int>() ));
  gChanDropStats[id].resize(inputs,0);  
#endif

#ifdef ENABLE_ROUTER_STATS
  _vc_activity.resize(inputs*_vcs,0);
  _port_congestness.resize(_vcs*outputs,0.0);
  _input_request.resize(inputs,0);
  _input_grant.resize(outputs, 0);
#endif
}

IQRouter::~IQRouter( )
{
  if(false && gStatsOut && gCRT){
    
    *gStatsOut<<"debug_delay("<<GetID()+1<<",:)=[";
    for(int ii=0; ii<_inputs; ii++){
      *gStatsOut<<debug_delay_map[ii]<<" ";
    }
    *gStatsOut<<"];\n";

    *gStatsOut<<"crt("<<GetID()+1<<",:)=[";
    for(int ii=0; ii<_outputs*_num_vcs; ii++){
      *gStatsOut<<_crt[ii]<<" ";
    }
    *gStatsOut<<"];\n";
  
    *gStatsOut<<"crt_delay("<<GetID()+1<<",:)=[";
    for(int ii=0; ii<_outputs*_num_vcs; ii++){
      *gStatsOut<<_crt_delay[ii]<<" ";
    }
    *gStatsOut<<"];\n";
  }

  if(gCRT){
    delete[] _crt;
    delete[] _crt_delay;
  }

  if(gPrintActivity) {
    cout << Name() << ".bufferMonitor:" << endl ; 
    cout << *_bufferMonitor << endl ;
    
    cout << Name() << ".switchMonitor:" << endl ; 
    cout << "Inputs=" << _inputs ;
    cout << "Outputs=" << _outputs ;
    cout << *_switchMonitor << endl ;
  }

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
    for(int i = 0; i<_num_vcs*_outputs; i++){
      delete _VOQArbs[i];
    }
    delete [] _VOQArbs;
  }
  delete _sw_allocator;
  if(_spec_sw_allocator)
    delete _spec_sw_allocator;

  delete _bufferMonitor;
  delete _switchMonitor;
  VCTag::FreeAll();
}
  
void IQRouter::ReadInputs( )
{
  if(  _track_routing_commitment){
    memcpy(_current_bandwidth_commitment, 
	   _next_bandwidth_commitment, 
	   sizeof(int)*_outputs*_num_vcs);
  }
  if(gPB && ADAPTIVE_PERFECT_PB){
    int sum = 0; 
    for(int i = 0; i<gK; i++){
      int index = gK+gA-1+i;
      assert(_output_channels[index]->GetGlobal());
      sum += GetCredit(index);
    }
    _global_average = sum/gK;
  }
 
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
      for(int i = 0; i<_num_vcs*_outputs; i++){
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
#ifdef ENABLE_ROUTER_STATS
	  gChanDropStats[_id][input]++;
#endif
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
#ifdef ENABLE_ROUTER_STATS
	  gChanDropStats[_id][input]++;
#endif
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
    Flit * f;
    f = _input_channels[input]->Receive();
    if(gPB && f && f->pb!=NULL){ 
      assert(_input_channels[input]->GetSource()!=-1);
      assert(!_input_channels[input]->GetGlobal());
      for(int i = 0; i<gK; i++){
	int index = router_global_index(_input_channels[input]->GetSource(),
					gK+gA-1+i);
	_global_table[index] = f->pb->_data[i];
      }
      
      f->pb->Free();
      f->pb = NULL;
      if(f->id==-666){ //this is fake
	if(ADAPTIVE_PERFECT_PB){
	  _global_array[_input_channels[input]->GetSource()%gA] =f->payload;
	}
	f->Free();
	continue;
      }
    }

    if(gReservation){
      f = _ExpirationCheck(f, input);
    }
    if(f){
      _in_queue_flits.insert(make_pair(input, f));
      activity = true;
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Received flit " << f->id
		   << " from channel at input " << input
		 << "." << endl;
      }
      ++_received_flits[input];
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
      if(gCRT && c->cr_time !=-1){
	assert(!c->vc.empty());
	assert(c->vc.size()==1);
	int vc = c->vc[0];
	_crt[output*_num_vcs+vc] = GetSimTime()-c->cr_time;
      }
      _proc_credits.push_back(make_pair(GetSimTime() + _credit_delay, 
					make_pair(c, output)));
      activity = true;
    }
  }

  if(gCRT && activity){
    int l_crt_min = 999999;
    int g_crt_min = 999999;
    for(int o = gK; o<_outputs; o++){
      int port_crt_max = 0;
      bool global = _output_credits[o]->GetGlobal();
      //port max
      for(int v = 0; v<_num_vcs; v++){
	if(_crt[o*_num_vcs+v]>port_crt_max)
	  port_crt_max = _crt[o*_num_vcs+v];
      }
      if(global && port_crt_max<g_crt_min){
	  g_crt_min = port_crt_max;
      } else if(!global && port_crt_max<l_crt_min){
	l_crt_min = port_crt_max;
      }
    }
    g_crt_min =  (g_crt_min==0)?63: g_crt_min;
    l_crt_min =  (l_crt_min==0)?12: l_crt_min;
    assert(l_crt_min >=12 && g_crt_min >=62);

    for(int o = gK; o<_outputs; o++){
      int min = _output_credits[o]->GetGlobal()?g_crt_min:l_crt_min;
      for(int v = 0; v<_num_vcs; v++){
	_crt_delay[o*_num_vcs+v]=MAX(_crt[o*_num_vcs+v]-min,0);
      }
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
    assert((vc >= 0) && (vc < _num_vcs));

    Buffer * const cur_buf = _buf[input];
    OutputSet * o = NULL;
    if(f->head){
#ifdef FLIT_HOP_LATENCY 
      f->arrival_stamp=GetSimTime();
#endif 
      o = OutputSet::New();
    }
    if(_voq && vc2voq(vc)==-1){
      if(f->head){
	int old_vc = f->vc;
	_rf(this, f, input, o, false);
	const OutputSet::sSetElement* iset = o->GetSet();
	int  out_port = iset->output_port;
	f->vc = old_vc;
	vc = vc2voq(f->vc, out_port);
	_UpdateCommitment(input, vc, f, o);
	_voq_pid[input*_num_vcs+f->vc]=pair<int, int>(f->pid, vc);
      } else {
	assert(_voq_pid[input*_num_vcs+f->vc].first==f->pid);
	vc = _voq_pid[input*_num_vcs+f->vc].second;
      }
    }
#ifdef ENABLE_ROUTER_STATS
    _vc_activity[input*_vcs+vc]++;
#endif
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
    if(!cur_buf->AddFlit(vc, f,o)) {
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
	VCTag* tag = VCTag::New(-1, input, vc, -1);
	_route_vcs.push_back(tag);
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
	  _UpdateCommitment(input, vc, f, cur_buf->GetRouteSet(vc));
	}
	cur_buf->SetState(vc, VC::vc_alloc);
	if(_speculative) {
	  VCTag* tag = VCTag::New(-1, input, vc, -1);
	  _sw_alloc_vcs.push_back(tag);
	}
	if(_vc_allocator ||  _VOQArbs  ) {
	  VCTag* tag = VCTag::New(-1, input, vc, -1);
	  _vc_alloc_vcs.push_back(tag);
	}
      }
    } else if((cur_buf->GetState(vc) == VC::active) &&
	      (cur_buf->FrontFlit(vc) == f)) {
      if(_switch_hold_vc[input*_input_speedup + vc%_input_speedup] == vc) {
	VCTag* tag = VCTag::New(-1, input, vc, -1);
	_sw_hold_vcs.push_back(tag);
      } else {
	VCTag* tag = VCTag::New(-1, input, vc, -1);
	_sw_alloc_vcs.push_back(tag);
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
  for(deque<VCTag*>::iterator iter = _route_vcs.begin();
      iter != _route_vcs.end();
      ++iter) {
    VCTag* tag = *iter;
    int const & time = tag->_time;
    if(time >= 0) {
      break;
    }
    tag->_time = GetSimTime() + _routing_delay - 1;
    
    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
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
   VCTag * tag = _route_vcs.front();



   int const & time = tag->_time;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
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
      _UpdateCommitment(input, vc, f, cur_buf->GetRouteSet(vc));
    }
    cur_buf->SetState(vc, VC::vc_alloc);
    if(_speculative) {
      VCTag* ntag = VCTag::New(tag);
      ntag->ResetTO();
      _sw_alloc_vcs.push_back(ntag);
    }
    if(_vc_allocator ||  _VOQArbs  ) {
      VCTag* ntag = VCTag::New(tag);
      ntag->ResetTO();
      _vc_alloc_vcs.push_back(ntag);
    }
    tag->Free();
    _route_vcs.pop_front();
  }
}


//------------------------------------------------------------------------------
// VC allocation
//------------------------------------------------------------------------------

void IQRouter::_VCAllocEvaluate( )
{
  bool watched = false;

  for(deque<VCTag*>::const_iterator iter = _vc_alloc_vcs.begin();
      iter != _vc_alloc_vcs.end();
      ++iter) {
    VCTag* tag = *iter;

    int const & time = tag->_time;
    if(time >= 0) {
      break;
    }

    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
    assert((vc >= 0) && (vc < _vcs));

    Buffer *cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::vc_alloc);

    Flit const * const f = cur_buf->FrontFlit(vc);
    
    assert(f);
    assert(f->head);
    
    //check for routing drop
    if(gReservation && 
       RESERVATION_ADAPT_SPEC_KILL && 
       f->res_type == RES_TYPE_SPEC&&
       f->payload == -666){
      cur_buf->SetDrop(vc);
      continue;
    }
    //check for expiration
    if(gReservation && VC_ALLOC_DROP && f->res_type == RES_TYPE_SPEC){
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
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		 << "Beginning VC allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    const OutputSet *route_set = cur_buf->GetRouteSet(vc);
    assert(route_set);

    int const out_priority = cur_buf->GetPriority(vc);
    const OutputSet::sSetElement*  iset = route_set->GetSet();

    int const & out_port = iset->output_port;
    assert((out_port >= 0) && (out_port < _outputs));

    BufferState *dest_buf = _next_buf[out_port];

    for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc) {
      assert((out_vc >= 0) && (out_vc < _vcs));
      int const & in_priority = iset->pri;

      //speculative buffer check
      if(gReservation &&
	 RESERVATION_BUFFER_SIZE_DROP && 
	 VC_ALLOC_DROP && f->res_type == RES_TYPE_SPEC){
	int next_size = dest_buf->Size(out_vc)-_output_channels[out_port]->GetLatency()*2;
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
	  break;
	}
      }

      // On the input input side, a VC might request several output VCs. 
      // These VCs can be prioritized by the routing function, and this is 
      // reflected in "in_priority". On the output side, if multiple VCs are 
      // requesting the same output VC, the priority of VCs is based on the 
      // actual packet priorities, which is reflected in "out_priority".

      if(_cut_through && 
	 dest_buf->IsAvailableFor(out_vc,f->packet_size) &&
	 !_output_buffer[out_port]->Full(out_vc)){
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
	  _vc_allocator->AddRequest(input*_vcs + vc, out_port*_num_vcs + out_vc, 0, in_priority, out_priority);
	} else {
#ifdef USE_LARGE_ARB
	  _VOQArbs[out_port*_num_vcs + out_vc]->AddRequest(input*_vcs + vc,0,out_priority); 
#else
	  _vc_allocator->AddRequest(input*_vcs + vc, out_port*_num_vcs + out_vc, 0, in_priority, out_priority);
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

  if( _vc_allocator && watched) {
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

  if( _vc_allocator && watched) {
    *gWatchOut << GetSimTime() << " | " << _vc_allocator->FullName() << " | ";
    _vc_allocator->PrintGrants( gWatchOut );
  }

  for(deque<VCTag*>::iterator iter = _vc_alloc_vcs.begin();
      iter != _vc_alloc_vcs.end();
      ++iter) {
    VCTag* tag = *iter;

    int const & time = tag->_time;
    if(time >= 0) {
      break;
    }
    tag->_time = GetSimTime() + _vc_alloc_delay - 1;

    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
    assert((vc >= 0) && (vc < _vcs));
    int & output_and_vc = tag->_output;

    if(!_voq){
      output_and_vc = _vc_allocator->OutputAssigned(input * _vcs + vc);
    } else {
#ifdef USE_LARGE_ARB      
      Buffer * const cur_buf = _buf[input];
      const OutputSet *route_set = cur_buf->GetRouteSet(vc);
      assert(route_set);
      const OutputSet::sSetElement*  iset = route_set->GetSet();
      int out_port = iset->output_port;
      int vc_start = iset->vc_start;
      int vc_end = iset->vc_end; 
      int vc_count = vc_end-vc_start+1;
      int rand_start=0;
      if(vc_count>1){
	rand_start= RandomInt(vc_count-1);
      }
      //Especially for mutliple data vc, start at a vc at random, then scan until found
      for(int i = 0; i<vc_count; i++){
	int vc_index = (rand_start+i)%vc_count;
	int out_vc = vc_start+vc_index; //actual vc
	if(_VOQArbs[_num_vcs*out_port+out_vc]->Arbitrate()!=input*_vcs+vc){
	  output_and_vc = -1;
	} else {
	  output_and_vc =  _num_vcs*out_port+out_vc;
	  _VOQArbs[_num_vcs*out_port+ out_vc]->Claim();//ensures ptr update
	  break;
	}
      }
#else
      output_and_vc = _vc_allocator->OutputAssigned(input * _vcs + vc);
#endif
    }
  }

  if(_vc_alloc_delay <= 1) {
    return;
  }
  for(deque<VCTag*>::iterator iter = _vc_alloc_vcs.begin();
      iter != _vc_alloc_vcs.end();
      ++iter) {
    VCTag* tag = *iter;
     
    int const & time = tag->_time;
    assert(time >= 0);
    if(GetSimTime() < time) {
      break;
    }
    int const & output_and_vc = tag->_output;
    
    if(output_and_vc >= 0) {
      int const match_output = output_and_vc / _num_vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _num_vcs;
      assert((match_vc >= 0) && (match_vc < _num_vcs));
      
      BufferState const * const dest_buf = _next_buf[match_output];
      if(!dest_buf->IsAvailableFor(match_vc)) {
	assert(!_cut_through);
	int const & input = tag->_input;
	assert((input >= 0) && (input < _inputs));
	int const & vc = tag->_vc;
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
	tag->_output = -1;
      }
    }
  }
}

void IQRouter::_VCAllocUpdate( )
{
  while(!_vc_alloc_vcs.empty()) {

   VCTag* tag = _vc_alloc_vcs.front();
    int const & time =  tag->_time;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const & input =  tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc =  tag->_vc;
    assert((vc >= 0) && (vc < _vcs));
    
    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::vc_alloc);
    
    Flit *f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->head);
   
    //SRP vc allocator drop processing
    if(gReservation && 
       cur_buf->GetDrop(vc)){
      if(f->watch){
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "drop flit " << f->id
		   << " from channel at input " << input
		   << "." << endl;
      }

      //send dropped credit since the packet is removed from the buffer
      Flit* drop_f = NULL;
      int out = (cur_buf->GetRouteSet(vc)->GetSet()->output_port);
#ifdef ENABLE_ROUTER_STATS
      gDropInStats[_id][input]++;
      gDropOutStats[_id][out]++;
#endif
      if(_track_routing_commitment)
	_next_bandwidth_commitment[out*_num_vcs+
				   cur_buf->GetRouteSet(vc)->GetSet()->vc_start]-=f->packet_size;
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
	cur_buf->RemoveFlit(vc);
	f->Free();
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
      _UpdateCommitment(input, vc, drop_f, cur_buf->GetRouteSet(vc));
      cur_buf->ResetExpected(vc);
      VCTag* ntag = VCTag::New(tag);
      ntag->ResetTO();
      _vc_alloc_vcs.push_back(ntag);
      tag->Free();
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
    
    int const & output_and_vc =  tag->_output;
    if(output_and_vc >= 0) {
      
      int const match_output = output_and_vc / _num_vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _num_vcs;
      assert((match_vc >= 0) && (match_vc < _num_vcs));

      
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
      //packet allocation of the output buffer
      if(!is_control_vc(match_vc)){
	_output_buffer[match_output]->Take(match_vc);
      }

      cur_buf->SetOutput(vc, match_output, match_vc);
      cur_buf->SetState(vc, VC::active);
      if(!_speculative) {
	VCTag* ntag = VCTag::New(tag);
	ntag->ResetTO();
	_sw_alloc_vcs.push_back(ntag);
      }
    } else {
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  No output VC allocated." << endl;
      }
     VCTag* ntag = VCTag::New(tag);
      ntag->ResetTO();
      _vc_alloc_vcs.push_back(ntag);
    }
    tag->Free();
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

  for(deque<VCTag*>::iterator iter = _sw_hold_vcs.begin();
      iter != _sw_hold_vcs.end();
      ++iter) {
    VCTag* tag = *iter;

    int const & time = tag->_time;
    if(time >= 0) {
      break;
    }
    tag->_time = GetSimTime();
    
    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
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
    assert((match_vc >= 0) && (match_vc < _num_vcs));
    
    int const expanded_output = match_port*_output_speedup + input%_output_speedup;
    assert(_switch_hold_in[expanded_input] == expanded_output);
    assert(!_switch_hold_in_skip[expanded_input]);

    BufferState const * const dest_buf = _next_buf[match_port];
    
#ifdef ENABLE_ROUTER_STATS
    _input_request[expanded_input]++;
#endif

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
      tag->_output = -1;
    } else {
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Reusing held connection from input " << input
		   << "." << (expanded_input % _input_speedup)
		   << " to output " << match_port
		   << "." << (expanded_output % _output_speedup)
		   << "." << endl;
      }
      tag->_output = expanded_output;
    }
  }
}

void IQRouter::_SWHoldUpdate( )
{
  while(!_sw_hold_vcs.empty()) {
      VCTag* tag =  _sw_hold_vcs.front();

    
      int const & time = tag->_time;
    if(time < 0) {
      break;
    }
    assert(GetSimTime() == time);
    
    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
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
    int const & expanded_output = tag->_output;
    if(expanded_output >= 0  && _switch_hold_out_skip[expanded_output]){
      skip = true;
    }    
    if(skip){   
      _switch_hold_in_skip[expanded_input]=false;
      _switch_hold_out_skip[expanded_output]=false;
      VCTag* ntag = VCTag::New(tag);
      ntag->ResetTO();
      _sw_hold_vcs.push_back(ntag);
      tag->Free();
      _sw_hold_vcs.pop_front();
      continue;
    }

    if(expanded_output >= 0) {
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
      if(_track_routing_commitment)
	_next_bandwidth_commitment[output*_num_vcs+f->vc]--;
#ifdef ENABLE_ROUTER_STATS
      _input_grant[expanded_input]++;
#endif
      _crossbar_flits.push_back(make_pair(-1, make_pair(f, make_pair(expanded_input, expanded_output))));
      Credit* c =NULL;
      if(_out_queue_credits.count(input) == 0) {
	_out_queue_credits.insert(make_pair(input,Credit::New()));
      }
      c=_out_queue_credits.find(input)->second;
      if(gCRT){  //crt
	c->cr_time = f->atime;//overload 
	c->delay = (expanded_output>=_outputs-gK)?
	  _crt_delay[expanded_output*_num_vcs+match_vc]:0;
	f->atime = GetSimTime();//overload
      }
      if(_voq){
	int rvc = vc;
	int vvc = voq2vc(rvc,_outputs);
	c->id=777;
	c->vc.push_back(vvc);
      } else {
	c->vc.push_back(vc);
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
	    VCTag* ntag = VCTag::New(tag);
	    ntag->ResetTO();
	    _route_vcs.push_back(ntag);
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
	      _UpdateCommitment(input, vc, nf, cur_buf->GetRouteSet(vc));
	    }
	    cur_buf->SetState(vc, VC::vc_alloc);
	    if(_speculative) {
	      VCTag* ntag = VCTag::New(tag);
	      ntag->ResetTO();
	      _sw_alloc_vcs.push_back(ntag);
	    }
	    if(_vc_allocator || _VOQArbs) {
	      VCTag* ntag = VCTag::New(tag);
	      ntag->ResetTO();
	      _vc_alloc_vcs.push_back(ntag);
	    }
	  }
	} else {
	  VCTag* ntag = VCTag::New(tag);
	  ntag->ResetTO();
	  _sw_hold_vcs.push_back(ntag);
	}
      }
    } else {      
      //this cancel can be caused by output buffer, output buffer frag
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
   VCTag* ntag = VCTag::New(tag);
      ntag->ResetTO();
      _sw_alloc_vcs.push_back(ntag);

    }    tag->Free();
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

  for(deque<VCTag* >::const_iterator iter = _sw_alloc_vcs.begin();
      iter != _sw_alloc_vcs.end();
      ++iter) {
    VCTag* tag = *iter;

    int const & time = tag->_time;
    if(time >= 0) {
      break;
    }

    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
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
#ifdef ENABLE_ROUTER_STATS
      _input_request[input]++;
#endif

      int const dest_output = cur_buf->GetOutputPort(vc);
      assert((dest_output >= 0) && (dest_output < _outputs));
      int const dest_vc = cur_buf->GetOutputVC(vc);
      assert((dest_vc >= 0) && (dest_vc < _num_vcs));
      
      BufferState const * const dest_buf = _next_buf[dest_output];
  
      
      if( (gReservation||gECN) &&
	  is_control_vc(dest_vc) && 
	  (!dest_buf->HasCreditFor(dest_vc) ||_output_buffer[dest_output]->ControlFull())){
	
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  VC control" << dest_vc 
		     << " at output " << dest_output 
		     << " is full." << endl;
	}
	continue;
      } else if(!dest_buf->HasCreditFor(dest_vc)) {
	assert(!_cut_through);
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

    route_set = cur_buf->GetRouteSet(vc);
    assert(route_set);
    
    const OutputSet::sSetElement*  iset = route_set->GetSet();
  
      
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
	assert((dest_vc >= 0) && (dest_vc < _num_vcs));
	if( (gReservation||gECN) &&
	    is_control_vc(dest_vc) && 
	    (dest_buf->IsAvailableFor(dest_vc) && 
	     !_output_buffer[dest_output]->ControlFull())){

	  do_request = true;
	  break;
	} else if(dest_buf->IsAvailableFor(dest_vc) && 
		  (!f->head || !_output_buffer[dest_output]->Full(dest_vc))) {
	  assert(false);
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
  
  for(deque<VCTag* >::iterator iter = _sw_alloc_vcs.begin();
      iter != _sw_alloc_vcs.end();
      ++iter) {
    VCTag* tag = *iter;
    int const & time = tag->_time;
    if(time >= 0) {
      break;
    }
    tag->_time = GetSimTime() + _sw_alloc_delay - 1;

    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
    assert((vc >= 0) && (vc < _vcs));

    Buffer const * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert((cur_buf->GetState(vc) == VC::active) || 
	   (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));
    
    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    
    int const expanded_input = input * _input_speedup + vc % _input_speedup;

    int & expanded_output = tag->_output;
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

  for(deque<VCTag*>::iterator iter = _sw_alloc_vcs.begin();
      iter != _sw_alloc_vcs.end();
      ++iter) {
    VCTag* tag = *iter;
    int const & time = tag->_time;
    assert(time >= 0);
    if(GetSimTime() < time) {
      break;
    }

    int const & expanded_output = tag->_output;
    
    if(expanded_output >= 0) {
      
      int const output = expanded_output / _output_speedup;
      assert((output >= 0) && (output < _outputs));
      
      BufferState const * const dest_buf = _next_buf[output];
      
      int const & input =tag->_input;
      assert((input >= 0) && (input < _inputs));
      assert((input % _output_speedup) == (expanded_output % _output_speedup));
      int const & vc = tag->_vc;
      assert((vc >= 0) && (vc < _num_vcs));
      
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
	tag->_output = -1;

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
	tag->_output = -1;
	  } else if((output_and_vc / _num_vcs) != output) {
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Discarding grant from input " << input
			 << "." << (vc % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << " due to port mismatch between VC and switch allocator." << endl;
	    }
	tag->_output = -1;

	  } else if(!dest_buf->HasCreditFor((output_and_vc % _num_vcs))) {
	    assert(!_cut_through);
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Discarding grant from input " << input
			 << "." << (vc % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << " due to lack of credit." << endl;
	    }
	    tag->_output = -1;

	  }

	} else { // VC allocation is piggybacked onto switch allocation

	  const OutputSet * route_set;

	  route_set = cur_buf->GetRouteSet(vc);
	  assert(route_set);

	  const OutputSet::sSetElement* iset = route_set ->GetSet();

	  bool found_vc = false;
	  if(iset->output_port == output) {
	    for(int out_vc = iset->vc_start; 
		out_vc <= iset->vc_end; 
		++out_vc) {
	      assert((out_vc >= 0) && (out_vc < _num_vcs));
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
	  

	  if(!found_vc) {
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Discarding grant from input " << input
			 << "." << (vc % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << " because no suitable output VC for piggyback allocation is available." << endl;
	    }
   tag->_output = -1;

	  }

	}

      } else {
	assert(cur_buf->GetOutputPort(vc) == output);
	
	int const match_vc = cur_buf->GetOutputVC(vc);
	assert((match_vc >= 0) && (match_vc < _num_vcs));

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
   tag->_output = -1;
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
   VCTag* tag = _sw_alloc_vcs.front();
   

   int const & time = tag->_time;
    if((time < 0) || (GetSimTime() < time)) {
      break;
    }
    assert(GetSimTime() == time);

    int const & input = tag->_input;
    assert((input >= 0) && (input < _inputs));
    int const & vc = tag->_vc;
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
    
    int const & expanded_output = tag->_output;
    
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
	route_set = cur_buf->GetRouteSet(vc);
	assert(route_set);
	const OutputSet::sSetElement* iset = route_set->GetSet();


	if(iset->output_port == output) {
	  for(int out_vc = iset->vc_start; 
	      out_vc <= iset->vc_end; 
	      ++out_vc) {
	    assert((out_vc >= 0) && (out_vc < _num_vcs));
	      
	    // FIXME: This check should probably be performed in Evaluate(), 
	    // not Update(), as the latter can cause the outcome to depend on 
	    // the order of evaluation!
	    if(dest_buf->IsAvailableFor(out_vc) && 
	       dest_buf->HasCreditFor(out_vc) &&
	       ((match_vc < 0) || 
		RoundRobinArbiter::Supersedes(out_vc, iset->pri, 
					      match_vc, match_prio, 
					      vc_offset, _num_vcs))) {
	      match_vc = out_vc;
	      match_prio = iset->pri;
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
	_vc_rr_offset[output*_classes+cl] = (match_vc + 1) % _num_vcs;

      } else {

	assert(cur_buf->GetOutputPort(vc) == output);

	match_vc = cur_buf->GetOutputVC(vc);
	assert(!dest_buf->IsFullFor(match_vc));

      }
      assert((match_vc >= 0) && (match_vc < _num_vcs));

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
      if(_track_routing_commitment)
	_next_bandwidth_commitment[output*_num_vcs+f->vc]--;
#ifdef ENABLE_ROUTER_STATS
      _input_grant[expanded_input]++;
#endif

      _crossbar_flits.push_back(make_pair(-1, make_pair(f, make_pair(expanded_input, expanded_output))));
      Credit* c= NULL;
      if(_out_queue_credits.count(input) == 0) {
	_out_queue_credits.insert(make_pair(input, Credit::New()));
      }
      c = _out_queue_credits.find(input)->second;
      if(gCRT){  //crt
	c->cr_time = f->atime;
	c->delay = (expanded_output>=_outputs-gK)?
	  _crt_delay[expanded_output*_num_vcs+match_vc]:0;
	f->atime = GetSimTime();
      }

      if(_voq){
	int rvc = vc;
	int vvc = voq2vc(rvc,_outputs);
	c->id=777;
	c->vc.push_back(vvc);
      } else {
	c->vc.push_back(vc);
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
	    VCTag* ntag = VCTag::New(tag);
	    ntag->ResetTO();
	    _route_vcs.push_back(ntag);
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
	      _UpdateCommitment(input, vc, nf, cur_buf->GetRouteSet(vc));
	    }
	    cur_buf->SetState(vc, VC::vc_alloc);
	    if(_speculative) {
	      VCTag* ntag = VCTag::New(tag);
	      ntag->ResetTO();
	      _sw_alloc_vcs.push_back(ntag);
	    }
	    VCTag* ntag = VCTag::New(tag);
	    ntag->ResetTO();
	    _vc_alloc_vcs.push_back(ntag);
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
	    VCTag* ntag = VCTag::New(tag);
	    ntag->ResetTO();
	    _sw_hold_vcs.push_back(ntag);
	  } else {
	    VCTag* ntag = VCTag::New(tag);
	    ntag->ResetTO();
	    _sw_alloc_vcs.push_back(ntag);
	  }
	}
      }
    } else {
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  No output port allocated." << endl;
      }
      VCTag* ntag = VCTag::New(tag);
      ntag->ResetTO();
      _sw_alloc_vcs.push_back(ntag);
    }
    tag->Free();
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
      _output_buffer[output]->QueueControlFlit(f);
    } else {
      _output_buffer[output]->QueueFlit(f->vc, f);
    }
    _crossbar_flits.pop_front();
  }
}


//------------------------------------------------------------------------------
// output queuing
//------------------------------------------------------------------------------

void IQRouter::_OutputQueuing( )
{
  if(gCRT){//delay must be carried per vc, so merging credit due to speed up cannot occur
    for(map<int, Credit *>::const_iterator iter = _out_queue_credits.begin();
	iter != _out_queue_credits.end();
	++iter) {

      int const & input = iter->first;
      assert((input >= 0) && (input < _inputs));

      Credit * const & c = iter->second;
      assert(c);
      assert(!c->vc.empty());
      if(gCRT){
	assert(GetSimTime()+c->delay>=GetSimTime());
	int delay=0;
	if(input>=gK && input <_inputs-gK){//only local channel credit feel the delay
	  delay = c->delay;
	}
	debug_delay_map[input]+=delay;
	_credit_delay_queue[input].insert(pair<int, Credit*>(GetSimTime()+delay*4,c));
      } else {
	_credit_buffer[input].push(c);
      }
    }
    _out_queue_credits.clear();
  }
}



void IQRouter::_SendFlits( )
{
  int group_threshold = 0;
  if(gPB && ADAPTIVE_PERFECT_PB){
    for(int i = 0; i<gA; i++){
      //cout<< _global_array[i]<<"\t";//deug
      group_threshold += 	_global_array[i];
    }
    //cout<<endl<<group_threshold<<"\t";//debug
    group_threshold= group_threshold/(gA-1);//value for this router *should always be zero
    //cout<<group_threshold<<endl;//debug
  }

  for(int i = 0; i<_outputs; i++){
    if(gPB && _output_channels[i]->GetGlobal()){
      int grp_global_index = router_global_index(GetID(),i);
      assert(grp_global_index<gG-1);
      int output_level =GetCredit(i);
      if(!ADAPTIVE_PERFECT_PB){
	if(output_level >=PB_THRESHOLD){
	  _global_table[grp_global_index]=true;
	} else {
	  _global_table[grp_global_index]=false;
	}
      } else {
	if(output_level >=2*group_threshold && output_level >=PB_THRESHOLD){
	  //cout<<grp_global_index<<"\t"<<output_level<<" "<<group_threshold<<endl;//debug
	  _global_table[grp_global_index]=true;
	} else {
	  _global_table[grp_global_index]=false;
	}
      }
    }

    if(gECN){
      if(_voq && _ecn_use_voq_size){
	int voq_size =0;
	for(int j = 0;  j<_inputs; j++){
	  for(int v=_special_vcs; v<_num_vcs; v++){
	    voq_size+=_buf[j]->GetSize(vc2voq(v,i));
	  }
	}	
	if(_output_hysteresis[i]){
	  _output_hysteresis[i] = (voq_size >=((size_t)ECN_CONGEST_THRESHOLD-ECN_BUFFER_HYSTERESIS));
	} else {
	  _output_hysteresis[i] = (voq_size >=((size_t)ECN_CONGEST_THRESHOLD+ECN_BUFFER_HYSTERESIS));
	}
      } else {
	assert(_voq);
      }
  
      for(int j = 0; j<_num_vcs; j++){
	BufferState * const dest_buf = _next_buf[i];
	if(_credit_hysteresis[i*_num_vcs+j]){
	  _credit_hysteresis[i*_num_vcs+j] = ( (dest_buf->Size(j)-_output_buffer[i]->Size(j)) < ECN_BUFFER_THRESHOLD+ECN_CREDIT_HYSTERESIS);
	} else {
	  _credit_hysteresis[i*_num_vcs+j] = ( (dest_buf->Size(j)-_output_buffer[i]->Size(j)) < ECN_BUFFER_THRESHOLD-ECN_CREDIT_HYSTERESIS);
	}
      }
    }
  }

  for ( int output = 0; output < _outputs; ++output ) {
    Flit * f = _output_buffer[output]->SendFlit();
    if(f){
#ifdef FLIT_HOP_LATENCY 
      if(f->head){
	f->hop_lat.push(GetSimTime()-f-> arrival_stamp);
      }
#endif
     ++_sent_flits[output];
      if(gECN && f->head && f->res_type==RES_TYPE_NORM && !f->fecn ){	
	if(_output_hysteresis[output] &&
	   _credit_hysteresis[output*_num_vcs+f->vc]){
	  _ECN_activated[output*_num_vcs+f->vc]++;
	  f->fecn= true; 
	}
      }
      if(gPB && f->head){
	if(output>=gK && output<_outputs-gK){
	  assert(f->pb==NULL);
	  f->pb=PiggyPack::New();
	  for(int i = 0; i<gK; i++){
	    int index = router_global_index(GetID(),
					    gK+gA-1+i);
	    f->pb->_data[i] = _global_table[index];
	  }
	}
      }
      _output_channels[output]->Send( f );
    } else{
      if(gPB){
	if(output>=gK && output<_outputs-gK){
	  f = Flit::New();
	  f->id=-666;
	  f->pb=PiggyPack::New();
	  for(int i = 0; i<gK; i++){
	    int index = router_global_index(GetID(),
					    gK+gA-1+i);
	    f->pb->_data[i] = _global_table[index];
	  }
	  if(ADAPTIVE_PERFECT_PB){
	    f->payload=_global_average;
	  }
	  _output_channels[output]->Send( f );
	}
      }
    }
  }
}

void IQRouter::_SendCredits( )
{
  if(!gCRT){
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

  for ( int input = 0; input < _inputs; ++input ) {
    if(gCRT){
      if(!_credit_delay_queue[input].empty( )){
	multimap<int, Credit *>::iterator iter = _credit_delay_queue[input].begin();	
	if(iter->first<=GetSimTime()){
	  Credit * const c = iter->second;
	  assert(c);
	  _input_credits[input]->Send( c );
	  _credit_delay_queue[input].erase(iter);
	}
      }
    } else {
      if ( !_credit_buffer[input].empty( ) ) {
	Credit * const c = _credit_buffer[input].front( );
	assert(c);
	_credit_buffer[input].pop( );
	_input_credits[input]->Send( c );
      }
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
  assert(vc_begin < _num_vcs);
  assert(vc_end <_num_vcs);
  assert(vc_end >= vc_begin || vc_end==-1);

  BufferState const * const dest_buf = _next_buf[out];
  
  int const start = (vc_begin >= 0) ? vc_begin : 0;
  int const end = (vc_end >= 0) ? vc_end : ((vc_begin >= 0)?start:(_num_vcs - 1));

  int size = 0;
  for (int v = start; v <= end; v++)  {
    size+= dest_buf->Size(v);
  }
  
  if(_remove_credit_rtt && vc_begin== -1 && vc_end==-1){
    
    size-=_output_buffer[out]->Total(); //new  deduct output buffer
    size-=_output_channels[out]->GetLatency()*2;//deduct RTT
    size = size<0?0:size;
    size+=_output_buffer[out]->Total(); //readd output buffer
  }
  //
  if(_track_routing_commitment){
    if(vc_begin== -1 && vc_end==-1){
      for(int v = 0;v<_num_vcs;v++){
	size+=_current_bandwidth_commitment[out*_num_vcs+v];
      }
    } else {
      for(int v = start; v<end; v++){
	size+=_current_bandwidth_commitment[out*_num_vcs+v];
      }
    }
  }
  assert(size>=0);
  return size;
}
int IQRouter::GetCreditArray(int out, int* vcs, int vc_count, bool rtt, bool commit) const
{
  assert((out >= 0) && (out < _outputs));

  BufferState const * const dest_buf = _next_buf[out];
  int size = 0;
  for(int i = 0; i<vc_count; i++){
    assert(vcs[i]>=0 && vcs[i]<_num_vcs);
    size+= dest_buf->Size(vcs[i]);
  }

  //this really dont' make senes for indidual vcs, but whatever
  if(rtt){
    size-=_output_channels[out]->GetLatency()*2;
    size = size<0?0:size;
  }
  //new vc specific commitment
  if(commit){
    for(int i = 0; i<vc_count; i++){
      size+= _current_bandwidth_commitment[out*_num_vcs+vcs[i]];
    }
  }
  assert(!(size<0));
  return size;
}
int IQRouter::GetCommit(int out, int vc) const{
  if(vc==-1){
    int size = 0;
    for(int v = 0;v<_num_vcs;v++){
      size+=_current_bandwidth_commitment[out*_num_vcs+v];
    }
    return size;
  } else {
    return _current_bandwidth_commitment[out*_num_vcs+vc];
  } 
}



int IQRouter::GetBuffer(int i) const {
  assert(i < _inputs);

  int size = 0;
  int const i_start = (i >= 0) ? i : 0;
  int const i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input) {
    for(int vc = 0; vc < _num_vcs; ++vc) {
      size += _buf[input]->GetSize(vc);
    }
  }
  return size;
}

vector<int> IQRouter::GetBuffers(int i) const {
  assert(i < _inputs);

  vector<int> sizes(_num_vcs);
  int const i_start = (i >= 0) ? i : 0;
  int const i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input) {
    for(int vc = 0; vc < _num_vcs; ++vc) {
      sizes[vc] += _buf[input]->GetSize(vc);
    }
  }
  return sizes;
}


void IQRouter::_UpdateCommitment(int input, int vc, const Flit* f, const OutputSet * route_set){
  if(!_track_routing_commitment)
    return;
  assert(f->head);
  int output =  route_set->GetSet()->output_port;
  //if a packet has an range of out put vcs, the commitment is put on the first vc
  //truthfully, this doesn't work with multiple vc per route set, because commit increase
  //is put on the first vc, but deduction need also be from the first vc, which is not
  //implemented right now, 
  _next_bandwidth_commitment[output*_num_vcs+
			     route_set->GetSet()->vc_start]+=f->packet_size;
}








//voq label to real vc
int IQRouter::voq2vc(int vvc, int outputs){
  assert(_voq);
  assert(outputs!=-1);
  if(vvc<_special_vcs){
    return vvc;
  } else { //data
    return (vvc-_special_vcs)/outputs+_special_vcs;
  }
}

bool IQRouter::is_control_vc(int vc){
  assert(_voq);
  if(vc<_ctrl_vcs){
    return true;
  }
  return false;
}
bool IQRouter::is_voq_vc(int vc){
  assert(_voq);

  if(vc<_special_vcs){
    return false;
  } else { //data
    return true;
  }
}

int IQRouter::vc2voq(int vc, int output){
  assert(_voq);

  if(vc<_special_vcs){
    return vc;
  } else { //data
    if(output==-1) //?
      return -1;
    else  //deduct ctrl vc, scale by #voq, add output port + add ctrl vc back
      return (vc-_special_vcs)*_outputs+output+_special_vcs;
  }

}



stack<VCTag *> VCTag::_all;
stack<VCTag *> VCTag::_free;
VCTag::VCTag(){
  Reset();
  _use = false;
}
void VCTag::Free() {
  _free.push(this);
  assert(_use);
  _use = false;
}
void VCTag::FreeAll(){
  if(!_all.empty()){
    cout<<_all.size()<<" maximum VCTag evenets"<<endl;
    while(!_all.empty()){
      delete _all.top();
      _all.pop();
    }
  }
}
VCTag * VCTag::New(VCTag* old) {
  return New(old->_time,
	     old->_input,
	     old->_vc,
	     old->_output);
}
VCTag * VCTag::New() {
  return New(-1, -1, -1, -1);
}
VCTag * VCTag::New(int t, int i, int v, int o) {
  VCTag * tag;
  if(_free.empty()) {
    tag = new VCTag;
    _all.push(tag);
  } else {
    tag = _free.top();
    _free.pop();
  }

  assert(!tag->_use);
  tag->_use = true;
  tag->Set(t,i,v,o);
  return tag;
}
void VCTag::Set(int t, int i, int v, int o){
  _time = t;
  _input = i;
  _vc = v;
  _output = o;
}
void VCTag::Reset(){
  _time = -1;
  _input= -1;
  _vc   = -1;
  _output=-1;
}
void VCTag::ResetTO(){
  _time = -1;
  _output=-1;
}




