// $Id$

/*
Copyright (c) 2007-2010, Trustees of The Leland Stanford Junior University
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
#include "allocator.hpp"
#include "switch_monitor.hpp"
#include "buffer_monitor.hpp"

IQRouter::IQRouter( Configuration const & config, Module *parent, 
		    string const & name, int id, int inputs, int outputs )
  : Router( config, parent, name, id, inputs, outputs )
{
  _vcs         = config.GetInt( "num_vcs" );
  _speculative = (config.GetInt("speculative") > 0);
  _spec_use_prio = (config.GetInt("spec_use_prio") > 0);
  _spec_check_elig = (config.GetInt("spec_check_elig") > 0);
  _spec_mask_by_reqs = (config.GetInt("spec_mask_by_reqs") > 0);

  _routing_delay    = config.GetInt( "routing_delay" );
  _vc_alloc_delay   = config.GetInt( "vc_alloc_delay" );
  _sw_alloc_delay   = config.GetInt( "sw_alloc_delay" );
  
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
  string arb_type = config.GetStr( "vc_alloc_arb_type" );
  int iters = config.GetInt( "vc_alloc_iters" );
  if(iters == 0) iters = config.GetInt("alloc_iters");
  _vc_allocator = Allocator::NewAllocator( this, "vc_allocator",
					   alloc_type,
					   _vcs*_inputs,
					   _vcs*_outputs,
					   iters, arb_type );

  if ( !_vc_allocator ) {
    Error("Unknown vc_allocator type: " + alloc_type);
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
  
  if ( _speculative && !_spec_use_prio ) {    
    _spec_sw_allocator = Allocator::NewAllocator( this, "spec_sw_allocator",
						  alloc_type,
						  _inputs*_input_speedup, 
						  _outputs*_output_speedup,
						  iters, arb_type );
    if ( !_spec_sw_allocator ) {
      Error("Unknown spec_sw_allocator type: " + alloc_type);
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

  _received_flits.resize(_inputs);
  _sent_flits.resize(_outputs);
  ResetFlitStats();

  _bufferMonitor = new BufferMonitor(inputs);
  _switchMonitor = new SwitchMonitor(inputs, outputs);

}

IQRouter::~IQRouter( )
{

  if(gPrintActivity){
    cout << Name() << ".bufferMonitor:" << endl ; 
    cout << *_bufferMonitor << endl ;
    
    cout << Name() << ".switchMonitor:" << endl ; 
    cout << "Inputs=" << _inputs ;
    cout << "Outputs=" << _outputs ;
    cout << *_switchMonitor << endl ;
  }

  for (int i = 0; i < _inputs; ++i)
    delete _buf[i];
  
  for (int j = 0; j < _outputs; ++j)
    delete _next_buf[j];

  delete _vc_allocator;
  delete _sw_allocator;
  if ( _speculative && !_spec_use_prio )
    delete _spec_sw_allocator;

  delete _bufferMonitor;
  delete _switchMonitor;
}
  
void IQRouter::ReadInputs( )
{
  _ReceiveFlits( );
  _ReceiveCredits( );
}

void IQRouter::_InternalStep( )
{
  _bufferMonitor->cycle( );
  _switchMonitor->cycle( );

  _InputQueuing( );
  _Route( );
  _VCAlloc( );
  _SWAlloc( );
  _OutputQueuing( );

  for ( int input = 0; input < _inputs; ++input ) {
    _buf[input]->AdvanceTime( );
  }

}

void IQRouter::WriteOutputs( )
{
  _SendFlits( );
  _SendCredits( );
}


//------------------------------------------------------------------------------
// read inputs
//------------------------------------------------------------------------------

void IQRouter::_ReceiveFlits( )
{
  for(int input = 0; input < _inputs; ++input) { 
    Flit * const f = _input_channels[input]->Receive();
    if(f) {
      ++_received_flits[input];
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Received flit " << f->id
		   << " from channel at input " << input
		   << "." << endl;
      }

      Buffer * const cur_buf = _buf[input];
      int const vc = f->vc;
      assert((vc >= 0) && (vc < _vcs));

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
      if ( !cur_buf->AddFlit( vc, f ) ) {
	Error( "VC buffer overflow" );
      }
      _bufferMonitor->write( input, f ) ;

      _in_queue_vcs.push_back(make_pair(input, vc));
    }
  }
}

void IQRouter::_ReceiveCredits( )
{
  for(int output = 0; output < _outputs; ++output) {  
    Credit * const c = _output_credits[output]->Receive();
    if(c) {
      _in_queue_credits.push_back(make_pair(c, output));
    }
  }
}


//------------------------------------------------------------------------------
// input queuing
//------------------------------------------------------------------------------

void IQRouter::_InputQueuing( )
{
  while(!_in_queue_vcs.empty()) {
    
    pair<int, int> const & item = _in_queue_vcs.front();
    int const & input = item.first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = item.second;
    assert((vc >= 0) && (vc < _vcs));

    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));

    if (cur_buf->GetState(vc) == VC::idle) {
      
      cur_buf->SetState(vc, VC::routing);
      _route_waiting_vcs.push(make_pair(GetSimTime() + _routing_delay, item));
    }

    _in_queue_vcs.pop_front();
  }

  while(!_in_queue_credits.empty()) {
    pair<Credit *, int> const & item = _in_queue_credits.front();
    _proc_waiting_credits.push(make_pair(GetSimTime() + _credit_delay, item));

    _in_queue_credits.pop_front();
  }

  while(!_proc_waiting_credits.empty()) {
    pair<int, pair<Credit *, int> > const & item = _proc_waiting_credits.front();
    int const & time = item.first;
    if(GetSimTime() < time) {
      return;
    }

    Credit * const & c = item.second.first;
    assert(c);

    int const & output = item.second.second;
    assert((output >= 0) && (output < _outputs));
    
    BufferState * const dest_buf = _next_buf[output];
    
    dest_buf->ProcessCredit(c);
    c->Free();
    _proc_waiting_credits.pop();
  }
}


//------------------------------------------------------------------------------
// routing
//------------------------------------------------------------------------------

void IQRouter::_Route( )
{
  while(!_route_waiting_vcs.empty()) {
    pair<int, pair<int, int> > & item = _route_waiting_vcs.front();
    int & time = item.first;
    if(GetSimTime() < time) {
      return;
    }

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

    cur_buf->Route(vc, _rf, this, f, input);
    time += _vc_alloc_delay;
    _vc_alloc_waiting_vcs.push(item);
    cur_buf->SetState(vc, _speculative ? VC::vc_spec : VC::vc_alloc);
    _route_waiting_vcs.pop();
  }
}


//------------------------------------------------------------------------------
// VC allocation
//------------------------------------------------------------------------------

void IQRouter::_VCAlloc( )
{
  while(!_vc_alloc_waiting_vcs.empty()) {
    pair<int, pair<int, int> > const & item = _vc_alloc_waiting_vcs.front();
    if(GetSimTime() < item.first) {
      break;
    }
    _vc_alloc_pending_vcs.push_back(item.second);
    _vc_alloc_waiting_vcs.pop();
  }

  if(_vc_alloc_pending_vcs.empty()) {
    return;
  }

  bool watched = false;

  _vc_allocator->Clear();

  list<pair<int, int> >::iterator iter = _vc_alloc_pending_vcs.begin();
  while(iter != _vc_alloc_pending_vcs.end()) {
    
    int const & input = iter->first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = iter->second;
    assert((vc >= 0) && (vc < _vcs));

    Buffer * const cur_buf = _buf[input];
    assert(!cur_buf->Empty(vc));
    assert(cur_buf->GetState(vc) == (_speculative ? VC::vc_spec : VC::vc_alloc));

    Flit const * const f = cur_buf->FrontFlit(vc);
    assert(f);
    assert(f->head);
    
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		 << "VC " << vc << " at input " << input
		 << " is requesting VC allocation for flit " << f->id
		 << "." << endl;
      watched = true;
    }
    
    OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
    assert(route_set);

    int const out_priority = cur_buf->GetPriority(vc);
    set<OutputSet::sSetElement> const setlist = route_set ->GetSet();
    set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
    while(iset != setlist.end()){

      int const & out_port = iset->output_port;
      assert((out_port >= 0) && (out_port < _outputs));

      BufferState const * const dest_buf = _next_buf[out_port];

      for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc) {
	assert((out_vc >= 0) && (out_vc < _vcs));

	int const in_priority = iset->pri;
	// On the input input side, a VC might request several output 
	// VCs.  These VCs can be prioritized by the routing function
	// and this is reflected in "in_priority".  On the output,
	// if multiple VCs are requesting the same output VC, the priority
	// of VCs is based on the actual packet priorities, which is
	// reflected in "out_priority".
	
	if(dest_buf->IsAvailableFor(out_vc)) {
	  if(f->watch){
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Requesting VC " << out_vc
		       << " at output " << out_port 
		       << " with priorities " << in_priority
		       << " and " << out_priority
		       << "." << endl;
	  }
	  _vc_allocator->AddRequest(input*_vcs + vc, out_port*_vcs + out_vc, 0, 
				    in_priority, out_priority);
	} else {
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "VC " << out_vc << " at output " << out_port 
		       << " is unavailable." << endl;
	}
      }
      //go to the next item in the outputset
      ++iset;
    }
    ++iter;
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

  // Winning flits get a VC

  iter = _vc_alloc_pending_vcs.begin();
  while(iter != _vc_alloc_pending_vcs.end()) {
    int const & input = iter->first;
    assert((input >= 0) && (input < _inputs));
    int const & vc = iter->second;
    assert((vc >= 0) && (vc < _vcs));

    int const output_and_vc = _vc_allocator->OutputAssigned(input * _vcs + vc);
    
    if(output_and_vc < 0) {

      // no match -- keep request and retry in next round

      ++iter;

    } else {
      
      int const match_output = output_and_vc / _vcs;
      assert((match_output >= 0) && (match_output < _outputs));
      int const match_vc = output_and_vc % _vcs;
      assert((match_vc >= 0) && (match_vc < _vcs));

      if(watched) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "VC allocation grants VC " << match_vc
		   << " at output " << match_output 
		   << " to VC " << vc
		   << " at output " << input
		   << "." << endl;
      }

      // match -- update state and remove request from pending list

      Buffer * const cur_buf = _buf[input];
      assert(!cur_buf->Empty(vc));
      assert(cur_buf->GetState(vc) == (_speculative ? VC::vc_spec : VC::vc_alloc));

      cur_buf->SetState(vc, _speculative ? VC::vc_spec_grant : VC::active);
      cur_buf->SetOutput(vc, match_output, match_vc);

      BufferState * const dest_buf = _next_buf[match_output];

      dest_buf->TakeBuffer(match_vc);
      
      Flit const * const f = cur_buf->FrontFlit(vc);
      assert(f);
      assert(f->head);

      if(f->watch)
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Granted VC " << match_vc << " at output " << match_output
		   << " to VC " << vc << " at input " << input
		   << " (flit: " << f->id << ")." << endl;
      
      iter = _vc_alloc_pending_vcs.erase(iter);
    }
  }
}


//------------------------------------------------------------------------------
// switch allocation
//------------------------------------------------------------------------------

void IQRouter::_SWAlloc( )
{
  bool watched = false;

  _sw_allocator->Clear();
  if (_speculative && !_spec_use_prio)
    _spec_sw_allocator->Clear();
  
  for ( int input = 0; input < _inputs; ++input ) {
    for ( int s = 0; s < _input_speedup; ++s ) {
      int const expanded_input  = input * _input_speedup + s;
      
      // Arbitrate (round-robin) between multiple 
      // requesting VCs at the same input (handles 
      // the case when multiple VC's are requesting
      // the same output port)
      int vc = _sw_rr_offset[ expanded_input ];
      assert((vc >= 0) && (vc < _vcs));
      assert((vc % _input_speedup) == s);

      for ( int v = 0; v < _vcs / _input_speedup; ++v ) {

	Buffer const * const cur_buf = _buf[input];

	if(!cur_buf->Empty(vc) &&
	   (cur_buf->GetStateTime(vc) >= _sw_alloc_delay)) {
	  
	  if(cur_buf->GetState(vc) == VC::active) {
	    
	    int const output = cur_buf->GetOutputPort(vc);
	    assert((output >= 0) && (output < _outputs));

	    BufferState const * const dest_buf = _next_buf[output];
	    
	    if ( !dest_buf->IsFullFor( cur_buf->GetOutputVC(vc) ) ) {
	      
	      // When input_speedup > 1, the virtual channel buffers are 
	      // interleaved to create multiple input ports to the switch. 
	      // Similarily, the output ports are interleaved based on their 
	      // originating input when output_speedup > 1.
	      
	      assert( expanded_input == input *_input_speedup + vc % _input_speedup );
	      int const expanded_output = 
		output * _output_speedup + input % _output_speedup;
	      
	      if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		   ( _switch_hold_out[expanded_output] == -1 ) ) {
		
		// We could have requested this same input-output pair in a 
		// previous iteration; only replace the previous request if 
		// the current request has a higher priority (this is default 
		// behavior of the allocators).  Switch allocation priorities 
		// are strictly determined by the packet priorities.
		
		Flit const * const f = cur_buf->FrontFlit(vc);
		assert(f);
		if(f->watch) {
		  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			     << "VC " << vc << " at input " << input 
			     << " requested output " << output 
			     << " (non-spec., exp. input: " << expanded_input
			     << ", exp. output: " << expanded_output
			     << ", flit: " << f->id
			     << ", prio: " << cur_buf->GetPriority(vc)
			     << ")." << endl;
		  watched = true;
		}
		
		// dub: for the old-style speculation implementation, we 
		// overload the packet priorities to prioritize 
		// non-speculative requests over speculative ones
		_sw_allocator->AddRequest(expanded_input, expanded_output, 
					  vc, 
					  cur_buf->GetPriority(vc), 
					  cur_buf->GetPriority(vc));

	      }
	    } else {
	      //if this vc has a hold on the switch need to cancel it to prevent deadlock
	      if(_hold_switch_for_packet){
		int const expanded_output = 
		  output * _output_speedup + input % _output_speedup;
		if(_switch_hold_in[expanded_input] == expanded_output &&
		   _switch_hold_vc[expanded_input] == vc &&
		   _switch_hold_out[expanded_output] == expanded_input){
		  _switch_hold_in[expanded_input]   = -1;
		  _switch_hold_vc[expanded_input]   = -1;
		  _switch_hold_out[expanded_output] = -1;
		}
	      }
	    }
	  } else if((cur_buf->GetState(vc) == VC::vc_spec) ||
		    (cur_buf->GetState(vc) == VC::vc_spec_grant)) {
	  
	    //
	    // The following models the speculative VC allocation aspects 
	    // of the pipeline. An input VC with a request in for an egress
	    // virtual channel will also speculatively bid for the switch
	    // regardless of whether the VC allocation succeeds. These
	    // speculative requests are handled in a separate allocator so 
	    // as to prevent them from interfering with non-speculative bids
	    //

	    assert( _speculative );
	    assert( expanded_input == input * _input_speedup + vc % _input_speedup );
	    
	    OutputSet const * const route_set = cur_buf->GetRouteSet(vc);
	    set<OutputSet::sSetElement> const setlist = route_set->GetSet();
	    set<OutputSet::sSetElement>::const_iterator iset = setlist.begin( );
	    while(iset!=setlist.end( )){
	      
	      int const & out_port = iset->output_port;
	      assert((out_port >= 0) && (out_port < _outputs));
	      
	      bool do_request;
	      
	      if(_spec_check_elig) {
		
		do_request = false;

		BufferState const * const dest_buf = _next_buf[out_port];
		
		// check if at least one suitable VC is available at this output
		
		for ( int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc ) {
		  assert((out_vc >= 0) && (out_vc < _vcs));

		  if(dest_buf->IsAvailableFor(out_vc)) {
		    do_request = true;
		    break;
		  }
		}
	      } else {
		do_request = true;
	      }

	      if(do_request) { 
		int const expanded_output = out_port * _output_speedup + input % _output_speedup;
		if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		     ( _switch_hold_out[expanded_output] == -1 ) ) {
		  
		  int const prio = (_spec_use_prio ? numeric_limits<int>::min() : 0) + cur_buf->GetPriority(vc);
		  
		  Flit const * const f = cur_buf->FrontFlit(vc);
		  assert(f);
		  if(f->watch) {
		    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			       << "VC " << vc << " at input " << input 
			       << " requested output " << out_port
			       << " (spec., exp. input: " << expanded_input
			       << ", exp. output: " << expanded_output
			       << ", flit: " << f->id
			       << ", prio: " << prio
			       << ")." << endl;
		    watched = true;
		  }
		  
		  Allocator * const alloc = _spec_use_prio ? _sw_allocator : _spec_sw_allocator;
		  alloc->AddRequest(expanded_input, expanded_output, vc, prio, prio);
		
		}
	      }
	      iset++;
	    }
	  }
	}
	vc += _input_speedup;
	if(vc >= _vcs) vc = s;
      }
    }
  }
  
  if(watched) {
    *gWatchOut << GetSimTime() << " | " << _sw_allocator->FullName() << " | ";
    _sw_allocator->PrintRequests( gWatchOut );
    if(_speculative && !_spec_use_prio) {
      *gWatchOut << GetSimTime() << " | " << _spec_sw_allocator->FullName() << " | ";
      _spec_sw_allocator->PrintRequests( gWatchOut );
    }
  }
  
  _sw_allocator->Allocate();
  if(_speculative && !_spec_use_prio)
    _spec_sw_allocator->Allocate();
  
  if(watched) {
    *gWatchOut << GetSimTime() << " | " << _sw_allocator->FullName() << " | ";
    _sw_allocator->PrintGrants( gWatchOut );
    if(_speculative && !_spec_use_prio) {
      *gWatchOut << GetSimTime() << " | " << _spec_sw_allocator->FullName() << " | ";
      _spec_sw_allocator->PrintGrants( gWatchOut );
    }
  }

  // Winning flits cross the switch

  for ( int input = 0; input < _inputs; ++input ) {
    Credit * c = 0;
    
    int vc_grant_nonspec = 0;
    int vc_grant_spec = 0;
    
    Buffer * const cur_buf = _buf[input];

    for ( int s = 0; s < _input_speedup; ++s ) {

      bool use_spec_grant = false;
      
      int const expanded_input = input * _input_speedup + s;
      int expanded_output = _switch_hold_in[expanded_input];
      int vc = _switch_hold_vc[expanded_input];

      if ( expanded_output >= 0 ) {

	// grant through held switch

	assert((vc >= 0) && (vc < _vcs));

	if(watched) {
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "Switch held for VC " << vc
		     << " at input " << input
		     << " (exp. input " << expanded_input
		     << ", exp. output: " << expanded_output
		     << ")." << endl;
	}
	
	if ( cur_buf->Empty(vc) ) { // Cancel held match if VC is empty
	  _switch_hold_in[expanded_input]   = -1;
	  _switch_hold_vc[expanded_input]   = -1;
	  _switch_hold_out[expanded_output] = -1;
	  expanded_output = -1;
	}

      } else {

	assert(vc < 0);

	expanded_output = _sw_allocator->OutputAssigned( expanded_input );
	
	if(expanded_output >= 0) {
	  
	  // grant through main allocator

	  assert(_sw_allocator->InputAssigned(expanded_output) == expanded_input);
	  assert(_sw_allocator->OutputHasRequests(expanded_output));

	  vc = _sw_allocator->ReadRequest(expanded_input, expanded_output);
	  assert((vc >= 0) && (vc < _vcs));

	  if(watched) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Switch allocator grants exp. output " << expanded_output
		       << " to VC " << vc
		       << " at input " << input
		       << " (exp. input " << expanded_input
		       << ")." << endl;
	  }

	} else if ( _speculative && !_spec_use_prio ) {

	  expanded_output = _spec_sw_allocator->OutputAssigned(expanded_input);
	  
	  if ( expanded_output >= 0 ) {
	    
	    // grant through dedicated speculative allocator

	    assert(_spec_sw_allocator->InputAssigned(expanded_output) == expanded_input);
	    assert(_spec_sw_allocator->OutputHasRequests(expanded_output));
	    
	    if(_spec_mask_by_reqs ? 
	       _sw_allocator->OutputHasRequests(expanded_output) : 
	       (_sw_allocator->InputAssigned(expanded_output) >= 0)) {
	      
	      expanded_output = -1;
	      
	    } else {
	      
	      vc = _spec_sw_allocator->ReadRequest(expanded_input, expanded_output);
	      assert((vc >= 0) && (vc < _vcs));

	      if(watched) {
		*gWatchOut << GetSimTime() << " | " << FullName() << " | "
			   << "Speculative switch allocator grants exp. output " << expanded_output
			   << " to VC " << vc
			   << " at input " << input
			   << " (exp. input " << expanded_input
			   << ")." << endl;
	      }

	    }
	    
	  }
	}
      }

      if ( expanded_output >= 0 ) {
	int const output = expanded_output / _output_speedup;
	assert((output >= 0) && (output < _outputs));

	// Detect speculative switch requests which succeeded when VC 
	// allocation failed and prevenet the switch from forwarding;
	// also, in case the routing function can return multiple outputs, 
	// check to make sure VC allocation and speculative switch allocation 
	// pick the same output port.
	if ( ( ( cur_buf->GetState(vc) == VC::vc_spec_grant ) ||
	       ( cur_buf->GetState(vc) == VC::active ) ) &&
	     ( cur_buf->GetOutputPort(vc) == output ) ) {
	  
	  if(use_spec_grant) {
	    vc_grant_spec++;
	  } else {
	    vc_grant_nonspec++;
	  }
	  
	  if ( _hold_switch_for_packet ) {
	    _switch_hold_in[expanded_input] = expanded_output;
	    _switch_hold_vc[expanded_input] = vc;
	    _switch_hold_out[expanded_output] = expanded_input;
	  }
	  
	  assert((cur_buf->GetState(vc) == VC::vc_spec_grant) ||
		 (cur_buf->GetState(vc) == VC::active));
	  assert(!cur_buf->Empty(vc));
	  assert(cur_buf->GetOutputPort(vc) == output);
	  
	  BufferState * const dest_buf = _next_buf[output];
	  
	  if ( dest_buf->IsFullFor( cur_buf->GetOutputVC( vc ) ) )
	    continue ;
	  
	  // Forward flit to crossbar and send credit back
	  Flit * const f = cur_buf->RemoveFlit(vc);
	  assert(f);
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Output " << output
		       << " granted to VC " << vc << " at input " << input;
	    if(cur_buf->GetState(vc) == VC::vc_spec_grant)
	      *gWatchOut << " (spec";
	    else
	      *gWatchOut << " (non-spec";
	    *gWatchOut << ", exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ", flit: " << f->id << ")." << endl;
	  }
	  
	  f->hops++;
	  
	  _bufferMonitor->read(input, f) ;
	  
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Forwarding flit " << f->id << " through crossbar "
		       << "(exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ")." << endl;
	  
	  if ( !c ) {
	    c = Credit::New( );
	  }

	  assert(vc == f->vc);

	  c->vc.insert(f->vc);
	  f->vc = cur_buf->GetOutputVC(vc);
	  dest_buf->SendingFlit( f );
	  
	  _switchMonitor->traversal( input, output, f) ;
	  _crossbar_waiting_flits.push(make_pair(GetSimTime() + _routing_delay, 
						 make_pair(f, expanded_output)));
	  
	  if(f->tail) {
	    cur_buf->SetState(vc, VC::idle);
	    if(!cur_buf->Empty(vc)) {
	      _in_queue_vcs.push_back(make_pair(input, vc));
	    }
	    _switch_hold_in[expanded_input]   = -1;
	    _switch_hold_vc[expanded_input]   = -1;
	    _switch_hold_out[expanded_output] = -1;
	  }
	  
	  int const next_offset = vc + _input_speedup;
	  _sw_rr_offset[expanded_input] = 
	    (next_offset < _vcs) ? next_offset : s;

	} else {
	  assert(cur_buf->GetState(vc) == VC::vc_spec);
	  Flit const * const f = cur_buf->FrontFlit(vc);
	  assert(f);
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Speculation failed at output " << output
		       << " (exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ", flit: " << f->id << ")." << endl;
	} 
      }
    }
    
    // Promote all other virtual channel grants marked as speculative to active.
    for ( int vc = 0 ; vc < _vcs ; vc++ ) {
      if ( cur_buf->GetState(vc) == VC::vc_spec_grant ) {
	cur_buf->SetState(vc, VC::active);	
      } 
    }
    
    _credit_buffer[input].push(c);
  }
}


//------------------------------------------------------------------------------
// output queuing
//------------------------------------------------------------------------------

void IQRouter::_OutputQueuing( )
{
  while(!_crossbar_waiting_flits.empty()) {
    pair<int, pair<Flit *, int> > const & item = _crossbar_waiting_flits.front();
    int const & time = item.first;
    if(GetSimTime() < time) {
      break;
    }
    Flit * const & f = item.second.first;
    assert(f);

    int const & expanded_output = item.second.second;
    int const output = expanded_output / _output_speedup;
    assert((output >= 0) && (output < _outputs));

    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		 << "Buffering flit " << f->id
		 << " at output " << output
		 << "." << endl;
    }
    _output_buffer[output].push(f);
    _crossbar_waiting_flits.pop();
  }
}


//------------------------------------------------------------------------------
// write outputs
//------------------------------------------------------------------------------

void IQRouter::_SendFlits( )
{
  for ( int output = 0; output < _outputs; ++output ) {
    Flit * f = NULL;
    if ( !_output_buffer[output].empty( ) ) {
      f = _output_buffer[output].front( );
      _output_buffer[output].pop( );
      ++_sent_flits[output];
      if(f->watch)
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		    << "Sending flit " << f->id
		    << " to channel at output " << output
		    << "." << endl;
      if(gTrace){cout<<"Outport "<<output<<endl;cout<<"Stop Mark"<<endl;}
    }
    _output_channels[output]->Send( f );
  }
}

void IQRouter::_SendCredits( )
{
  for ( int input = 0; input < _inputs; ++input ) {
    Credit * c = NULL;
    if ( !_credit_buffer[input].empty( ) ) {
      c = _credit_buffer[input].front( );
      _credit_buffer[input].pop( );
    }
    _input_credits[input]->Send( c );
  }
}


//------------------------------------------------------------------------------
// misc.
//------------------------------------------------------------------------------

void IQRouter::Display( ) const
{
  for ( int input = 0; input < _inputs; ++input ) {
    _buf[input]->Display( );
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

int IQRouter::GetReceivedFlits(int i) const {
  assert(i < _inputs);

  int count = 0;
  int const i_start = (i >= 0) ? i : 0;
  int const i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input)
    count += _received_flits[input];
  return count;
}

int IQRouter::GetSentFlits(int o) const {
  assert(o < _outputs);

  int count = 0;
  int const o_start = (o >= 0) ? o : 0;
  int const o_end = (o >= 0) ? o : (_outputs - 1);
  for(int output = o_start; output <= o_end; ++output)
    count += _sent_flits[output];
  return count;
}

void IQRouter::ResetFlitStats() {
  for(int i = 0; i < _inputs; ++i)
    _received_flits[i] = 0;
  for(int o = 0; o < _outputs; ++o)
    _sent_flits[o] = 0;
}
