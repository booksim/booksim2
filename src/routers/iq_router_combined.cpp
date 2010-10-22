// $Id$

/*
Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
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

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cassert>

#include "globals.hpp"
#include "random_utils.hpp"
#include "buffer.hpp"
#include "iq_router_combined.hpp"
#include "switch_monitor.hpp"
#include "buffer_monitor.hpp"

IQRouterCombined::IQRouterCombined( const Configuration& config,
		    Module *parent, const string & name, int id,
		    int inputs, int outputs )
  : IQRouterBase( config, parent, name, id, inputs, outputs )
{
  // Allocate the allocators
  string alloc_type;
  config.GetStr( "sw_allocator", alloc_type );
  string arb_type;
  config.GetStr( "sw_alloc_arb_type", arb_type );
  int iters = config.GetInt("sw_alloc_iters");
  if(iters == 0) iters = config.GetInt("alloc_iters");
  _sw_allocator = Allocator::NewAllocator( this, "sw_allocator",
					   alloc_type,
					   _inputs*_input_speedup, 
					   _outputs*_output_speedup,
					   iters, arb_type );
  
  _vc_rr_offset.resize(_inputs*_input_speedup*_vcs);
  _sw_rr_offset.resize(_inputs*_input_speedup);
  for(int i = 0; i < _inputs*_input_speedup; ++i)
    _sw_rr_offset[i] = i % _input_speedup;

}

IQRouterCombined::~IQRouterCombined( )
{
  delete _sw_allocator;
}
  
void IQRouterCombined::_Alloc( )
{
  bool watched = false;
  
  _sw_allocator->Clear( );
  
  for(int input = 0; input < _inputs; ++input) {
    for(int s = 0; s < _input_speedup; ++s) {
      int expanded_input  =  input * _input_speedup + s;
      
      // Arbitrate (round-robin) between multiple requesting VCs at the same 
      // input (handles the case when multiple VC's are requesting the same 
      // output port)
      int vc = _sw_rr_offset[expanded_input];
      assert((vc % _input_speedup) == s);

      for(int v = 0; v < _vcs; ++v) {

	Buffer * cur_buf = _buf[input];
	
	VC::eVCState vc_state = cur_buf->GetState(vc);

	if(!cur_buf->Empty(vc) &&
	   ((vc_state == VC::vc_alloc) || (vc_state == VC::active)) &&
	   (cur_buf->GetStateTime(vc) >= _sw_alloc_delay)) {
	  
	  const OutputSet * route_set = cur_buf->GetRouteSet(vc);
	  Flit * f = cur_buf->FrontFlit(vc);
	  assert(f);
	  
	  int output = _vc_rr_offset[expanded_input*_vcs+vc];
	  
	  for(int output_index = 0; output_index < _outputs; ++output_index) {
	    
	    // in active state, we only care about our assigned output port
	    if(vc_state == VC::active) {
	      output = cur_buf->GetOutputPort(vc);
	    }
	    
	    // When input_speedup > 1, the virtual channel buffers are 
	    // interleaved to create multiple input ports to the switch.
	    // Similarily, the output ports are interleaved based on their 
	    // originating input when output_speedup > 1.
	    
	    assert(expanded_input == input * _input_speedup + vc % _input_speedup);
	    int expanded_output = 
	      output * _output_speedup + input % _output_speedup;
	    
	    if((_switch_hold_in[expanded_input] == -1) && 
	       (_switch_hold_out[expanded_output] == -1)) {
	      
	      BufferState * dest_buf = _next_buf[output];
	      
	      bool do_request = false;
	      int in_priority;
	      
	      // check if any suitable VCs are available and determine the 
	      // highest priority for this port
	      int vc_cnt = route_set->NumVCs(output);
	      assert(!((vc_state == VC::active) && (vc_cnt == 0)));
	      for(int vc_index = 0; vc_index < vc_cnt; ++vc_index) {
		int vc_prio;
		int out_vc = route_set->GetVC(output, vc_index, &vc_prio);
		if((((vc_state == VC::vc_alloc) &&
		     dest_buf->IsAvailableFor(out_vc)) || 
		    ((vc_state == VC::active) &&
		     (out_vc == cur_buf->GetOutputVC(vc)))) &&
		   (!do_request || (vc_prio > in_priority)) &&
		   !dest_buf->IsFullFor(out_vc)) {
		  do_request = true;
		  in_priority = vc_prio;
		}
	      }
	      
	      if(do_request) {
		
		if(f->watch) {
		  *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
			      << "VC " << vc << " at input "
			      << input << " requests output " << output 
			      << " (flit: " << f->id
			      << ", exp. input: " << expanded_input
			      << ", exp. output: " << expanded_output
			      << ")." << endl;
		  watched = true;
		}
		
		// We could have requested this same input-output pair in a 
		// previous iteration; only replace the previous request if the 
		// current request has a higher priority (this is default 
		// behavior of the allocators). Switch allocation priorities 
		// are strictly determined by the packet priorities.
		
		_sw_allocator->AddRequest(expanded_input, expanded_output, vc, 
					  in_priority, cur_buf->GetPriority(vc));
		
	      }
	    }

	    // in active state, we only care about our assigned output port
	    if(vc_state == VC::active) {
	      break;
	    }
	    
	    output = (output + 1) % _outputs;
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
  }
  
  _sw_allocator->Allocate();
  
  // Winning flits cross the switch

  _crossbar_pipe->WriteAll(NULL);

  //////////////////////////////
  // Switch Power Modelling
  //  - Record Total Cycles
  //
  _switchMonitor->cycle() ;

  for(int input = 0; input < _inputs; ++input) {
    
    Credit * c = NULL;
    
    for(int s = 0; s < _input_speedup; ++s) {
      
      int expanded_input = input * _input_speedup + s;
      int expanded_output;
      Buffer * cur_buf = _buf[input];
      int vc;
      
      if(_switch_hold_in[expanded_input] != -1) {
	assert(_switch_hold_in[expanded_input] >= 0);
	expanded_output = _switch_hold_in[expanded_input];
	vc = _switch_hold_vc[expanded_input];
	
	if (cur_buf->Empty(vc)) { // Cancel held match if VC is empty
	  expanded_output = -1;
	}
      } else {
	expanded_output = _sw_allocator->OutputAssigned(expanded_input);
	if(expanded_output >= 0) {
	  vc = _sw_allocator->ReadRequest(expanded_input, expanded_output);
	}
      }
      
      if(expanded_output >= 0) {
	int output = expanded_output / _output_speedup;
	
	BufferState * dest_buf = _next_buf[output];
	Flit * f = cur_buf->FrontFlit(vc);
	assert(f);
	
	switch(cur_buf->GetState(vc)) {

	case VC::vc_alloc:
	  {
	    const OutputSet * route_set = cur_buf->GetRouteSet(vc);
	    int sel_prio = -1;
	    int sel_vc = -1;
	    int vc_cnt = route_set->NumVCs(output);
	    
	    for(int vc_index = 0; vc_index < vc_cnt; ++vc_index) {
	      int out_prio;
	      int out_vc = route_set->GetVC(output, vc_index, &out_prio);
	      if(dest_buf->IsAvailableFor(out_vc) && 
		 !dest_buf->IsFullFor(out_vc) && 
		 (out_prio > sel_prio)) {
		sel_vc = out_vc;
		sel_prio = out_prio;
	      }
	    }
	    
	    // we should only get to this point if some VC requested allocation
	    assert(sel_vc > -1);
	    
	    // dub: this is taken care of later on
	    //cur_vc->SetState(VC::active);
	    cur_buf->SetOutput(vc, output, sel_vc);
	    dest_buf->TakeBuffer(sel_vc);
	    
	    _vc_rr_offset[expanded_input*_vcs+vc] = (output + 1) % _outputs;
	    
	    if(f->watch)
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			  << "VC " << sel_vc << " at output " << output
			  << " granted to VC " << vc << " at input " << input
			  << " (flit: " << f->id << ")." << endl;
	  }
	  // NOTE: from here, we just fall through to the code for VC::active!
	  
	case VC::active:
	  
	  if(_hold_switch_for_packet) {
	    _switch_hold_in[expanded_input] = expanded_output;
	    _switch_hold_vc[expanded_input] = vc;
	    _switch_hold_out[expanded_output] = expanded_input;
	  }
	  
	  //assert(cur_vc->GetState() == VC::active);
	  assert(!cur_buf->Empty(vc));
	  assert(cur_buf->GetOutputPort(vc) == output);
	  
	  dest_buf = _next_buf[output];
	  
	  assert(!dest_buf->IsFullFor(cur_buf->GetOutputVC(vc)));
	  
	  // Forward flit to crossbar and send credit back
	  f = cur_buf->RemoveFlit(vc);
	  
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
			<< "Output " << output
			<< " granted to VC " << vc << " at input " << input
			<< " (flit: " << f->id
			<< ", exp. input: " << expanded_input
			<< ", exp. output: " << expanded_output
			<< ")." << endl;
	  
	  f->hops++;
	  
	  //
	  // Switch Power Modelling
	  //
	  _switchMonitor->traversal(input, output, f);
	  _bufferMonitor->read(input, f);
	  
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			<< "Forwarding flit " << f->id << " through crossbar "
			<< "(exp. input: " << expanded_input
			<< ", exp. output: " << expanded_output
			<< ")." << endl;
	  
	  if (c == NULL) {
	    c = _NewCredit(_vcs);
	  }

	  assert(vc == f->vc);

	  c->vc[c->vc_cnt] = f->vc;
	  c->vc_cnt++;
	  c->dest_router = f->from_router;
	  f->vc = cur_buf->GetOutputVC(vc);
	  dest_buf->SendingFlit(f);
	  
	  _crossbar_pipe->Write(f, expanded_output);
	  
	  if(f->tail) {
	    cur_buf->SetState(vc, VC::idle);
	    _switch_hold_in[expanded_input] = -1;
	    _switch_hold_vc[expanded_input] = -1;
	    _switch_hold_out[expanded_output] = -1;
	  }
	  
	  int next_offset = vc + _input_speedup;
	  _sw_rr_offset[expanded_input] = 
	    (next_offset < _vcs) ? next_offset : s;

	} 
      }
    }

    _credit_pipe->Write(c, input);
  }
}
