// $Id$

/*
Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this 
list of conditions and the following disclaimer in the documentation and/or 
other materials provided with the distribution.
Neither the name of the Stanford University nor the names of its contributors 
may be used to endorse or promote products derived from this software without 
specific prior written permission.

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

#include "globals.hpp"
#include "random_utils.hpp"
#include "vc.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer.hpp"
#include "buffer_state.hpp"
#include "roundrobin_arb.hpp"
#include "allocator.hpp"
#include "HierAllocator.hpp"
#include "switch_monitor.hpp"
#include "buffer_monitor.hpp"

#include "shared_allocator.hpp"
   
#define MAX(X,Y) ((X)>(Y)?(X):(Y))
   
   //forward notification
   bool SIMPLE_NOTE  = false;
bool ENABLE_NOTE = false;
bool DISABLE_SW_INPUT_PRIO = false;
bool DISABLE_SW_OUTPUT_PRIO = false;
bool DISABLE_VC_INPUT_PRIO = false;
bool DISABLE_VC_OUTPUT_PRIO = false;

bool SHARED_ALLOCATOR=false;


IQRouter::IQRouter( Configuration const & config, Module *parent, 
		    string const & name, int id, int inputs, int outputs )
: Router( config, parent, name, id, inputs, outputs ), _active(false)
{
  ENABLE_NOTE = (config.GetStr("priority")=="notification");
  SIMPLE_NOTE = ENABLE_NOTE && (config.GetInt("simple_notification")==1);
    
  DISABLE_SW_INPUT_PRIO =  (config.GetInt("disable_sw_input_prio")==1);
  DISABLE_SW_OUTPUT_PRIO = (config.GetInt("disable_sw_output_prio")==1);
  DISABLE_VC_INPUT_PRIO =  (config.GetInt("disable_vc_input_prio")==1);
  DISABLE_VC_OUTPUT_PRIO = (config.GetInt("disable_vc_output_prio")==1);

  if(ENABLE_NOTE){
    _output_notifications.clear();
    _output_notifications.resize(inputs*outputs, 0);
  }
  if(SIMPLE_NOTE){
    _port_note.clear();
    _port_note.resize(inputs,0);
  }
  

  _vcs         = config.GetInt( "num_vcs" );
  _classes     = config.GetInt( "classes" );
  _speculative = (config.GetInt("speculative") > 0);
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
    _buf[i] = new Buffer(config, _outputs, this, module_name.str( ) );
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
  string in_arb_type;
  string out_arb_type;
  int iters;
  SHARED_ALLOCATOR = (alloc_type == "piggyback");
  if( (alloc_type == "piggyback")) {
    if(!_speculative) {
      Error("Piggyback VC allocation requires speculative switch allocation to be enabled.");
    }
    _vc_allocator = NULL;
    _vc_rr_offset.resize(_outputs*_classes, -1);
  } else {
    iters = config.GetInt( "vc_alloc_iters" );
    if(iters == 0) iters = config.GetInt("alloc_iters");
    
    in_arb_type = (config.GetStr( "vc_alloc_input_arb_type" )=="")?
      config.GetStr( "vc_alloc_arb_type"):
      config.GetStr( "vc_alloc_input_arb_type");
    out_arb_type = (config.GetStr( "vc_alloc_output_arb_type" )=="")?
      config.GetStr( "vc_alloc_arb_type"):
      config.GetStr( "vc_alloc_output_arb_type");

    if(alloc_type == "hier"){
      _vc_allocator = new HierAllocator( this, "vc_allocator", 
					 _vcs,
					 _inputs, 
					 _vcs,
					 _outputs,
					 in_arb_type, 
					 in_arb_type, 
					 out_arb_type, 
					 "weighted");
    } else if(alloc_type == "hier_improved"){
      _vc_allocator = new HierAllocator( this, "vc_allocator", 
					 _vcs,
					 _inputs, 
					 _vcs,
					 _outputs,
					 in_arb_type, 
					 in_arb_type, 
					 out_arb_type, 
					 "weighted_improved");
    } else{
      _vc_allocator = Allocator::NewAllocator( this, "vc_allocator", 
					       alloc_type,
					       _vcs*_inputs, 
					       _vcs*_outputs,
					       iters, in_arb_type, out_arb_type );
    }
    if ( !_vc_allocator ) {
      Error("Unknown vc_allocator type: " + alloc_type);
    }
  }
  
  alloc_type = config.GetStr( "sw_allocator" );
  iters = config.GetInt("sw_alloc_iters");
  if(iters == 0) iters = config.GetInt("alloc_iters");
  in_arb_type = (config.GetStr( "sw_alloc_input_arb_type" )=="")?
    config.GetStr( "sw_alloc_arb_type"):
    config.GetStr( "sw_alloc_input_arb_type");
  out_arb_type = (config.GetStr( "sw_alloc_output_arb_type" )=="")?
    config.GetStr( "sw_alloc_arb_type"):
    config.GetStr( "sw_alloc_output_arb_type");
  
  if(SHARED_ALLOCATOR){
    shared_sw_allocator = new SharedAllocator(this, "shared_allocator",
					      _inputs*_input_speedup, 
					      _outputs*_output_speedup,in_arb_type , 
					      out_arb_type);
    _sw_allocator= NULL;
    _spec_sw_allocator = NULL;
  } else {
    shared_sw_allocator =NULL;
    _sw_allocator = Allocator::NewAllocator( this, "sw_allocator",
					     alloc_type,
					     _inputs*_input_speedup, 
					     _outputs*_output_speedup,
					     iters, in_arb_type , 
					     out_arb_type);
  
    if ( !_sw_allocator ) {
      Error("Unknown sw_allocator type: " + alloc_type);
    }
  
    if ( _speculative && ( config.GetInt("spec_use_prio") == 0 ) ) {    
      _spec_sw_allocator = Allocator::NewAllocator( this, "spec_sw_allocator",
						    alloc_type,
						    _inputs*_input_speedup, 
						    _outputs*_output_speedup,
						    iters, in_arb_type, 
						    out_arb_type );
    
      if ( !_spec_sw_allocator ) {
	Error("Unknown spec_sw_allocator type: " + alloc_type);
      }
    } else {
      _spec_sw_allocator = NULL;
    }
  }

  _sw_rr_offset.resize(_inputs*_input_speedup);
  for(int i = 0; i < _inputs*_input_speedup; ++i)
    _sw_rr_offset[i] = i % _input_speedup;
  
  // Output queues
  _output_buffer.resize(_outputs); 
  _credit_buffer.resize(_inputs); 

  // Switch configuration (when held for multiple cycles)
  _hold_switch_for_packet = (config.GetInt("hold_switch_for_packet") > 0);
  _switch_hold_in.resize(_inputs*_input_speedup, -1);
  _switch_hold_out.resize(_outputs*_output_speedup, -1);
  _switch_hold_vc.resize(_inputs*_input_speedup, -1);

  int classes = config.GetInt("classes");
  _bufferMonitor = new BufferMonitor(inputs, classes);
  _switchMonitor = new SwitchMonitor(inputs, outputs, classes);

  _stored_flits.resize(_inputs, 0);
  _active_packets.resize(_inputs, 0);
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
  for(int i = 0; i < _inputs; ++i)
    delete _buf[i];
  
  for(int j = 0; j < _outputs; ++j)
    delete _next_buf[j];

  if(_vc_allocator)
    delete _vc_allocator;
  if(_sw_allocator)
    delete _sw_allocator;
  _sw_allocator=NULL;
  if(_spec_sw_allocator)
    delete _spec_sw_allocator;
  _spec_sw_allocator=NULL;
  if(shared_sw_allocator)
    delete shared_sw_allocator;

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

  if(ENABLE_NOTE && !SIMPLE_NOTE){
    _output_notifications.clear();
    _output_notifications.resize(_inputs* _outputs, 0);
  }

  _InputQueuing( );
  bool activity = !_proc_credits.empty();

  if(!_route_vcs.empty())
    _RouteEvaluate( );
  if(_vc_allocator) {
    _vc_allocator->Clear();
    if(!_vc_alloc_vcs.empty())
      _VCAllocEvaluate( );
  }
  if(_hold_switch_for_packet) {
    if(!_sw_hold_vcs.empty())
      _SWHoldEvaluate( );
  }
  if(shared_sw_allocator)
    shared_sw_allocator->Clear();
  if(_sw_allocator)
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

bool IQRouter::_ReceiveFlits( )
{
  bool activity = false;
  for(int input = 0; input < _inputs; ++input) { 
    Flit * const f = _input_channels[input]->Receive();
    if(f) {
      if(ENABLE_NOTE && f->head){
	f->notification = f->next_notification;
	if(SIMPLE_NOTE){
	  _port_note[input] = f->notification;
	}
      }
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

    int const & vc = f->vc;
    assert((vc >= 0) && (vc < _vcs));

    Buffer * const cur_buf = _buf[input];

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
    cur_buf->AddFlit(vc, f);
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
	cur_buf->Route(vc, _rf, this, f, input);
	cur_buf->SetState(vc, VC::vc_alloc);
	if(_speculative) {
	  _sw_alloc_vcs.push_back(make_pair(-1, make_pair(make_pair(input, vc),
							  -1)));
	}
	if(_vc_allocator) {
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

    cur_buf->Route(vc, _rf, this, f, input);
    cur_buf->SetState(vc, VC::vc_alloc);
    if(_speculative) {
      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second, -1)));
    }
    if(_vc_allocator) {
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

    Buffer const * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == VC::vc_alloc);

    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->head);
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		 << "Beginning VC allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
    assert(route_set);

    int  out_priority = cur_buf->GetPriority(vc);
    set<OutputSet::sSetElement> const setlist = route_set->GetSet();

    for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
	iset != setlist.end();
	++iset) {

      int const & out_port = iset->output_port;
      assert((out_port >= 0) && (out_port < _outputs));
      //as soon as a vc knows a output port, put up a notificaiton
      if(ENABLE_NOTE){
	if(SIMPLE_NOTE){
	  _output_notifications[input*_outputs+out_port]=_port_note[input];
	} else {
	  _output_notifications[input*_outputs+out_port]=
	    MAX(_output_notifications[input*_outputs+out_port],cur_buf->GetNotification(vc));
	}
      }


      BufferState const * const dest_buf = _next_buf[out_port];

  
 
      for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc) {
	assert((out_vc >= 0) && (out_vc < _vcs));
	int  in_priority = out_priority;

	// On the input input side, a VC might request several output VCs. 
	// These VCs can be prioritized by the routing function, and this is 
	// reflected in "in_priority". On the output side, if multiple VCs are 
	// requesting the same output VC, the priority of VCs is based on the 
	// actual packet priorities, which is reflected in "out_priority".
	
	if(DISABLE_VC_INPUT_PRIO)
	  in_priority = 1;
	if(DISABLE_VC_OUTPUT_PRIO)   
	  out_priority = 1;
	
	if(dest_buf->IsAvailableFor(out_vc)) {
	  if(f->watch){
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "  Requesting VC " << out_vc
		       << " at output " << out_port 
		       << " (in_pri: " << in_priority
		       << ", out_pri: " << out_priority
		       << ")." << endl;
	    watched = true;
	  }

	  _vc_allocator->AddRequest(input*_vcs + vc, out_port*_vcs + out_vc, 0, 
				    in_priority, out_priority);
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

  _vc_allocator->Allocate();

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
    output_and_vc = _vc_allocator->OutputAssigned(input * _vcs + vc);

    if(output_and_vc >= 0) {
      _vc_allocator->UpdateState(input * _vcs + vc,  output_and_vc);
      int const match_output = output_and_vc / _vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _vcs;
      assert((match_vc >= 0) && (match_vc < _vcs));

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
      
      int const match_output = output_and_vc / _vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _vcs;
      assert((match_vc >= 0) && (match_vc < _vcs));
      
      BufferState const * const dest_buf = _next_buf[match_output];
      
      if(!dest_buf->IsAvailableFor(match_vc)) {
	
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
    
    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->head);
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Completed VC allocation for VC " << vc
		 << " at input " << input
		 << " (front: " << f->id
		 << ")." << endl;
    }
    
    int const & output_and_vc = item.second.second;
    
    if(output_and_vc >= 0) {
      
      int const match_output = output_and_vc / _vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _vcs;
      assert((match_vc >= 0) && (match_vc < _vcs));
      
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Acquiring assigned VC " << match_vc
		   << " at output " << match_output
		   << "." << endl;
      }
      
      BufferState * const dest_buf = _next_buf[match_output];
      assert(dest_buf->IsAvailableFor(match_vc));
      
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
    assert((match_vc >= 0) && (match_vc < _vcs));
    
    int const expanded_output = match_port*_output_speedup + input%_output_speedup;
    assert(_switch_hold_in[expanded_input] == expanded_output);
    //as soon as a vc knows a output port, put up a notificaiton
    if(ENABLE_NOTE){
      if(SIMPLE_NOTE){
	_output_notifications[expanded_input*_outputs+expanded_output]=_port_note[expanded_input];
      } else {
	_output_notifications[expanded_input*_outputs+expanded_output]=
	  MAX(_output_notifications[expanded_input*_outputs+expanded_output],
	      cur_buf->GetNotification(vc));
      }
    }
    


    BufferState const * const dest_buf = _next_buf[match_port];
    
    if(dest_buf->IsFullFor(match_vc)) {
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
    
    int const & expanded_output = item.second.second;
    
    if(expanded_output >= 0) {
      
      assert(_switch_hold_in[expanded_input] == expanded_output);
      assert(_switch_hold_out[expanded_output] == expanded_input);
      
      int const output = expanded_output / _output_speedup;
      assert((output >= 0) && (output < _outputs));
      assert(cur_buf->GetOutputPort(vc) == output);
      
      int const match_vc = cur_buf->GetOutputVC(vc);
      assert((match_vc >= 0) && (match_vc < _vcs));
      
      BufferState * const dest_buf = _next_buf[output];
      
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Scheduling switch connection from input " << input
		   << "." << (vc % _input_speedup)
		   << " to output " << output
		   << "." << (expanded_output % _output_speedup)
		   << "." << endl;
      }
      
      cur_buf->RemoveFlit(vc);
      --_stored_flits[input];
      //hold occurs intrapacket, flits cannot be the head
      assert(!f->head);
      if(f->tail) --_active_packets[input];
      _bufferMonitor->read(input, f) ;
      
      f->hops++;
      f->vc = match_vc;
      
      dest_buf->SendingFlit(f);
      
      _crossbar_flits.push_back(make_pair(-1, make_pair(f, make_pair(expanded_input, expanded_output))));
      
      if(_out_queue_credits.count(input) == 0) {
	_out_queue_credits.insert(make_pair(input, Credit::New()));
      }
      _out_queue_credits.find(input)->second->vc.insert(vc);
      
      if(cur_buf->Empty(vc)) {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  Cancelling held connection from input " << input
		     << "." << (expanded_input % _input_speedup)
		     << " to " << output
		     << "." << (expanded_output % _output_speedup)
		     << ": No more flits." << endl;
	}
	_switch_hold_vc[expanded_input] = -1;
	_switch_hold_in[expanded_input] = -1;
	_switch_hold_out[expanded_output] = -1;
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
	    cur_buf->Route(vc, _rf, this, nf, input);
	    cur_buf->SetState(vc, VC::vc_alloc);
	    if(_speculative) {
	      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							      -1)));
	    }
	    if(_vc_allocator) {
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
  
  if((_switch_hold_in[expanded_input] < 0) && 
     (_switch_hold_out[expanded_output] < 0)) {
    
    
    Allocator * allocator;
    if(SHARED_ALLOCATOR){
      allocator = shared_sw_allocator->GetNonSpec();
    } else {
      allocator = _sw_allocator;
    }
    int prio = cur_buf->GetPriority(vc);
    int in_prio = prio;
    if(DISABLE_SW_INPUT_PRIO)
      in_prio=1;
    int out_prio = prio;
    if(DISABLE_SW_OUTPUT_PRIO)   
      out_prio=1;
    
    if(_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)) {
      if(_spec_sw_allocator||SHARED_ALLOCATOR) {
	if(SHARED_ALLOCATOR){
	  allocator = shared_sw_allocator->GetSpec();
	} else {
	  allocator = _spec_sw_allocator;
	}
      } else {
	//adding MIN will message up the WRR
	assert(false);
	prio += numeric_limits<int>::min();
      }
    }

    Allocator::sRequest req;    
    if(allocator->ReadRequest(req, expanded_input, expanded_output)) {
      if(RoundRobinArbiter::Supersedes(vc, in_prio, req.label, req.in_pri, 
				       _sw_rr_offset[expanded_input], _vcs)) {
	if(f->watch ) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "  Replacing earlier request from VC " << req.label
		     << " for output " << output 
		     << "." << (expanded_output % _output_speedup)
		     << " with priority " << req.in_pri
		     << " (" << ((cur_buf->GetState(vc) == VC::active) ? 
				 "non-spec" : 
				 "spec")
		     << ", pri: " << in_prio
		     << ")." << endl;
	}
  
	allocator->RemoveRequest(expanded_input, expanded_output, req.label);

	allocator->AddRequest(expanded_input, expanded_output, vc, 
			      in_prio, out_prio);
	return true;
      }

      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Output " << output
		   << "." << (expanded_output % _output_speedup)
		   << " was already requested by VC " << req.label
		   << " with priority " << req.in_pri
		   << " (pri: " << in_prio
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
		 << ", pri: " << in_prio
		 << ")." << endl;
    }

    allocator->AddRequest(expanded_input, expanded_output, vc, 
			  in_prio, out_prio);
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
    
    if(_input_channels[input]->IsIO()){
      _inj_request++;
    } else {
      _cross_request++;
    }

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
      
      int const dest_output = cur_buf->GetOutputPort(vc);
      assert((dest_output >= 0) && (dest_output < _outputs));
      //as soon as a vc knows a output port, put up a notificaiton
      if(ENABLE_NOTE){
	if(SIMPLE_NOTE){
	  _output_notifications[input*_outputs+dest_output]= _port_note[input];
	} else {
	  _output_notifications[input*_outputs+dest_output]=
	    MAX(cur_buf->GetNotification(vc),_output_notifications[input*_outputs+dest_output] );
	}
      }

      int const dest_vc = cur_buf->GetOutputVC(vc);
      assert((dest_vc >= 0) && (dest_vc < _vcs));
      
      BufferState const * const dest_buf = _next_buf[dest_output];
      
      if(dest_buf->IsFullFor(dest_vc)) {
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
    
    OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
    assert(route_set);
    
    set<OutputSet::sSetElement> const setlist = route_set->GetSet();
    
    for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
	iset != setlist.end();
	++iset) {
      int const & dest_output = iset->output_port;
      assert((dest_output >= 0) && (dest_output < _outputs));
      //as soon as a vc knows a output port, put up a notificaiton
      if(ENABLE_NOTE){
	if(SIMPLE_NOTE){
	  _output_notifications[input*_outputs+dest_output]= _port_note[input];
	} else {
	  _output_notifications[input*_outputs+dest_output]=
	    MAX(cur_buf->GetNotification(vc),_output_notifications[input*_outputs+dest_output] );
	}
      }
      // for lower levels of speculation, ignore credit availability and always 
      // issue requests for all output ports in route set
      
      bool do_request;
      
      if(_spec_check_elig) {
	
	do_request = false;
	
	// for higher levels of speculation, check if at least one suitable VC 
	// is available at the current output
	
	BufferState const * const dest_buf = _next_buf[dest_output];
	
	for(int dest_vc = iset->vc_start; dest_vc <= iset->vc_end; ++dest_vc) {
	  assert((dest_vc >= 0) && (dest_vc < _vcs));
	  
	  if(dest_buf->IsAvailableFor(dest_vc) && !dest_buf->IsFullFor(dest_vc)) {
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
    Allocator* all = (SHARED_ALLOCATOR)?shared_sw_allocator->GetNonSpec():_sw_allocator;
    *gWatchOut << GetSimTime() << " | " << all->FullName() << " | ";
    all->PrintRequests(gWatchOut);
    all = (SHARED_ALLOCATOR)?shared_sw_allocator->GetSpec():_spec_sw_allocator;
    if(all) {
      *gWatchOut << GetSimTime() << " | " << all->FullName() << " | ";
      all->PrintRequests(gWatchOut);
    }
    cout<<"pointer at 4 "<<_sw_rr_offset[4] <<endl;
  }
  if(SHARED_ALLOCATOR){
    shared_sw_allocator->Allocate();
  } else {
    _sw_allocator->Allocate();
    if(_spec_sw_allocator)
      _spec_sw_allocator->Allocate();
  }

  if(watched) {
    Allocator* all = (SHARED_ALLOCATOR)?shared_sw_allocator->GetNonSpec():_sw_allocator;
    *gWatchOut << GetSimTime() << " | " << all->FullName() << " | ";
    all->PrintGrants(gWatchOut);
    all = (SHARED_ALLOCATOR)?shared_sw_allocator->GetSpec():_spec_sw_allocator;
    if(all) {
      *gWatchOut << GetSimTime() << " | " << all->FullName() << " | ";
      all->PrintGrants(gWatchOut);
    }
  }
  

  bool matched= false;

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

    

    if(SHARED_ALLOCATOR){
      expanded_output = shared_sw_allocator->GetNonSpec()->OutputAssigned(expanded_input);
      if(expanded_output >= 0) {
	assert((expanded_output % _output_speedup) == (input % _output_speedup));
	if(shared_sw_allocator->GetNonSpec()->ReadRequest(expanded_input, expanded_output) == vc) {
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Assigning output " << (expanded_output / _output_speedup)
		       << "." << (expanded_output % _output_speedup)
		       << " to VC " << vc
		       << " at input " << input
		       << "." << (vc % _input_speedup)
		       << "." << endl;
	  }
	  shared_sw_allocator->UpdateNonSpec(input, expanded_output);
	  if(_input_channels[expanded_input]->IsIO()){
	    _inj_grant++;
	  } else {
	    _cross_grant++;
	  }
	  _sw_rr_offset[expanded_input] = (vc + _input_speedup) % _vcs;
	} else {
	  expanded_output = -1;
	}
      } else {
	matched= true;
	expanded_output = shared_sw_allocator->GetSpec()->OutputAssigned(expanded_input);
	if(expanded_output >= 0) {
	  assert((expanded_output % _output_speedup) == (input % _output_speedup));
	  if(_spec_mask_by_reqs && 
	     shared_sw_allocator->GetNonSpec()->OutputHasRequests(expanded_output)) {
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
		    (shared_sw_allocator->GetNonSpec()->InputAssigned(expanded_output) >= 0)) {
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
	  } else if(shared_sw_allocator->GetSpec()->ReadRequest(expanded_input, 
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
	    shared_sw_allocator->UpdateSpec(input, expanded_output);
	    _sw_rr_offset[expanded_input] = (vc + _input_speedup) % _vcs;
	  } else {
	    expanded_output = -1;
	  }

	}
      }
    }  //shared allocator
    else {
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
	  _sw_allocator->UpdateState(expanded_input,  expanded_output);
	  if(_input_channels[expanded_input]->IsIO()){
	    _inj_grant++;
	  } else {
	    _cross_grant++;
	  }
	  _sw_rr_offset[expanded_input] = (vc + _input_speedup) % _vcs;
	} else {
	  expanded_output = -1;
	}
      } else if(_spec_sw_allocator) {
	matched= true;
	expanded_output = _spec_sw_allocator->OutputAssigned(expanded_input);
	if(expanded_output >= 0) {
	  _spec_sw_allocator->UpdateState(expanded_input,  expanded_output);
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
    }  //not shared allocator


  }
  if(!_speculative && (_sw_alloc_delay <= 1)) {
    return;
  }


  bool matched_again = false;
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
      assert((vc >= 0) && (vc < _vcs));
      
      int const expanded_input = input * _input_speedup + vc % _input_speedup;
      assert(_switch_hold_vc[expanded_input] != vc);
      
      Buffer const * const cur_buf = _buf[input];
      assert(!cur_buf->Empty(vc));
      assert((cur_buf->GetState(vc) == VC::active) ||
	     (_speculative && (cur_buf->GetState(vc) == VC::vc_alloc)));
      
      Flit const * const f = cur_buf->FrontFlit(vc);
      assert(f);
      
      if((_switch_hold_in[expanded_input] >= 0) ||
	 (_switch_hold_out[expanded_output] >= 0)) {
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

	assert(f->head);

	if(_vc_allocator) { // separate VC and switch allocators

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
	  } else if((output_and_vc / _vcs) != output) {
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Discarding grant from input " << input
			 << "." << (vc % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << " due to port mismatch between VC and switch allocator." << endl;
	    }
	    iter->second.second = -1;
	  } else if(dest_buf->IsFullFor((output_and_vc % _vcs))) {
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

	  OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
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
		assert((out_vc >= 0) && (out_vc < _vcs));
		if(dest_buf->IsAvailableFor(out_vc) && 
		   !dest_buf->IsFullFor(out_vc)) {
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
	assert((match_vc >= 0) && (match_vc < _vcs));

	if(dest_buf->IsFullFor(match_vc)) {
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "  Discarding grant from input " << input
		       << "." << (vc % _input_speedup)
		       << " to output " << output
		       << "." << (expanded_output % _output_speedup)
		       << " due to lack of credit." << endl;
	  }
	  iter->second.second = -1;
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
      assert(_switch_hold_vc[expanded_input] < 0);
      assert(_switch_hold_in[expanded_input] < 0);
      assert(_switch_hold_out[expanded_output] < 0);

      int const output = expanded_output / _output_speedup;
      assert((output >= 0) && (output < _outputs));

      BufferState * const dest_buf = _next_buf[output];

      int match_vc;

      if(!_vc_allocator && (cur_buf->GetState(vc) == VC::vc_alloc)) {

	int const & cl = f->cl;
	assert((cl >= 0) && (cl < _classes));

	int const & vc_offset = _vc_rr_offset[output*_classes+cl];

	match_vc = -1;
	int match_prio = numeric_limits<int>::min();

	const OutputSet * route_set = cur_buf->GetRouteSet(vc);
	set<OutputSet::sSetElement> const setlist = route_set->GetSet();

	for(set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
	    iset != setlist.end();
	    ++iset) {
	  if(iset->output_port == output) {
	    for(int out_vc = iset->vc_start; 
		out_vc <= iset->vc_end; 
		++out_vc) {
	      assert((out_vc >= 0) && (out_vc < _vcs));
	      
	      // FIXME: This check should probably be performed in Evaluate(), 
	      // not Update(), as the latter can cause the outcome to depend on 
	      // the order of evaluation!
	      if(dest_buf->IsAvailableFor(out_vc) && 
		 !dest_buf->IsFullFor(out_vc) &&
		 ((match_vc < 0) || 
		  RoundRobinArbiter::Supersedes(out_vc, iset->pri, 
						match_vc, match_prio, 
						vc_offset, _vcs))) {
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

	_vc_rr_offset[output*_classes+cl] = (match_vc + 1) % _vcs;

      } else {

	assert(cur_buf->GetOutputPort(vc) == output);

	match_vc = cur_buf->GetOutputVC(vc);

      }
      assert((match_vc >= 0) && (match_vc < _vcs));

      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "  Scheduling switch connection from input " << input
		   << "." << (vc % _input_speedup)
		   << " to output " << output
		   << "." << (expanded_output % _output_speedup)
		   << "." << endl;
      }

     if(ENABLE_NOTE && f->head){
	int sum=0;
	for(int i = 0; i<_inputs; i++){
	  sum += _output_notifications[i*_outputs+expanded_output];
	}
	assert(SIMPLE_NOTE || f->notification<= sum);
	cur_buf->FrontFlit(vc)->next_notification = sum;
      }



      cur_buf->RemoveFlit(vc);
      --_stored_flits[input];
      if(f->tail) --_active_packets[input];
      _bufferMonitor->read(input, f) ;

      f->hops++;
      f->vc = match_vc;
      
   
      dest_buf->SendingFlit(f);
      _crossbar_flits.push_back(make_pair(-1, make_pair(f, make_pair(expanded_input, expanded_output))));

      if(_out_queue_credits.count(input) == 0) {
	_out_queue_credits.insert(make_pair(input, Credit::New()));
      }
      _out_queue_credits.find(input)->second->vc.insert(vc);

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
	    cur_buf->Route(vc, _rf, this, nf, input);
	    cur_buf->SetState(vc, VC::vc_alloc);
	    if(_speculative) {
	      _sw_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							      -1)));
	    }
	    _vc_alloc_vcs.push_back(make_pair(-1, make_pair(item.second.first,
							    -1)));
	  }
	} else {
	  if(_hold_switch_for_packet) {
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Setting up switch hold for VC " << vc
			 << " at input " << input
			 << "." << (expanded_input % _input_speedup)
			 << " to output " << output
			 << "." << (expanded_output % _output_speedup)
			 << "." << endl;
	    }
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

    Flit * const & f = item.second.first;
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
    _output_buffer[output].push(f);

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
  for ( int output = 0; output < _outputs; ++output ) {
    if ( !_output_buffer[output].empty( ) ) {
      Flit * const f = _output_buffer[output].front( );
      assert(f);
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
    size+= dest_buf->Occupancy(v);
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
      size += _buf[input]->GetOccupancy(vc);
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
      sizes[vc] += _buf[input]->GetOccupancy(vc);
    }
  }
  return sizes;
}
