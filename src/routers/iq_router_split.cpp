// $Id: iq_router_split.cpp -1   $

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
#include <stdlib.h>
#include <assert.h>

#include "globals.hpp"
#include "random_utils.hpp"
#include "iq_router_split.hpp"

IQRouterSplit::IQRouterSplit( const Configuration& config,
		    Module *parent, string name, int id,
		    int inputs, int outputs )
  : IQRouterBase( config, parent, name, id, inputs, outputs )
{
  string alloc_type;
  string arb_type;
  int iters;

  // check constraints
  if(_routing_delay != 0)
    Error("This router architecture requires lookahead routing!");
  
  // Allocate the allocators
  config.GetStr( "sw_allocator", alloc_type );
  config.GetStr( "sw_alloc_arb_type", arb_type );
  iters = config.GetInt("sw_alloc_iters");
  if(iters == 0) iters = config.GetInt("alloc_iters");
  _sw_allocator = Allocator::NewAllocator( this, "sw_allocator",
					   alloc_type,
					   _inputs*_input_speedup, 
					   _outputs*_output_speedup,
					   iters, arb_type );
  
  _vc_rr_offset = new int [_inputs*_vcs];
  for ( int i = 0; i < _inputs*_vcs; ++i ) {
    _vc_rr_offset[i] = 0;
  }
  _sw_rr_offset = new int [_inputs*_input_speedup];
  for ( int i = 0; i < _inputs*_input_speedup; ++i ) {
    _sw_rr_offset[i] = 0;
  }
  
  _use_fast_path = new bool [_inputs*_vcs];
  for(int i = 0; i < _inputs*_vcs; i++) {
    _use_fast_path[i] = true;
  }
}

IQRouterSplit::~IQRouterSplit( )
{
  delete _sw_allocator;
  
  delete [] _vc_rr_offset;
  delete [] _sw_rr_offset;
  
  delete [] _use_fast_path;
}

void IQRouterSplit::_Alloc( )
{
  int fast_path_vcs[_inputs];
  _sw_allocator->Clear( );
  
  for(int input = 0; input < _inputs; ++input) {
    
    fast_path_vcs[input] = -1;
    
    for(int s = 0; s < _input_speedup; ++s) {
      int expanded_input  = s*_inputs + input;
      
      // Arbitrate (round-robin) between multiple requesting VCs at the same 
      // input (handles the case when multiple VC's are requesting the same 
      // output port)
      int vc = _sw_rr_offset[expanded_input];

      for(int v = 0; v < _vcs; ++v) {

	// This continue acounts for the interleaving of VCs when input speedup 
	// is used.
	// dub: Essentially, this skips loop iterations corresponding to those 
	// VCs not in the current speedup set. The skipped iterations will be 
	// handled in a different iteration of the enclosing loop over 's'.
	// dub: Furthermore, we skip this iteration if the current VC has only a
	// single, newly arrived flit.
	if(((vc % _input_speedup) != s) || _use_fast_path[input*_vcs+vc]) {
	  vc = (vc + 1) % _vcs;
	  continue;
	}
	
	VC * cur_vc = &_vc[input][vc];
	
	VC::eVCState vc_state = cur_vc->GetState();

	if(!cur_vc->Empty() &&
	   ((vc_state == VC::vc_alloc) || (vc_state == VC::active)) &&
	   (cur_vc->GetStateTime() >= _sw_alloc_delay)) {
	  
	  const OutputSet * route_set = cur_vc->GetRouteSet();
	  Flit * f = cur_vc->FrontFlit();
	  assert(f);
	  
	  int output = _vc_rr_offset[input*_vcs+vc];
	  
	  for(int output_index = 0; output_index < _outputs; ++output_index) {
	    
	    // in active state, we only care about our assigned output port
	    if(vc_state == VC::active) {
	      output = cur_vc->GetOutputPort();
	    }
	    
	    // When input_speedup > 1, the virtual channel buffers are 
	    // interleaved to create multiple input ports to the switch.
	    // Similarily, the output ports are interleaved based on their 
	    // originating input when output_speedup > 1.
	    
	    assert(expanded_input == (vc%_input_speedup)*_inputs+input);
	    int expanded_output = (input%_output_speedup)*_outputs + output;
	    
	    if((_switch_hold_in[expanded_input] == -1) && 
	       (_switch_hold_out[expanded_output] == -1)) {
	      
	      BufferState * dest_vc = &_next_vcs[output];
	      
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
		     dest_vc->IsAvailableFor(out_vc)) || 
		    ((vc_state == VC::active) &&
		     (out_vc == cur_vc->GetOutputVC()))) &&
		   (!do_request || (vc_prio > in_priority)) &&
		   !dest_vc->IsFullFor(out_vc)) {
		  do_request = true;
		  in_priority = vc_prio;
		}
	      }
	      
	      if(do_request) {
		
		if(f->watch) {
		  cout << "Switch ";
		  if(vc_state == VC::vc_alloc) {
		    cout << "and VC ";
		  }
		  cout << "allocation requested at "
		       << _fullname << " at time " << GetSimTime() << endl
		       << "  Input: " << input << " VC: " << vc << endl
		       << "  Output: " << output << endl
		       << "  Exp. Input: " << expanded_input << endl
		       << "  Exp. Output: " << expanded_output << endl
		       << *f;
		}
		
		// We could have requested this same input-output pair in a 
		// previous iteration; only replace the previous request if the 
		// current request has a higher priority (this is default 
		// behavior of the allocators). Switch allocation priorities 
		// are strictly determined by the packet priorities.
		
		_sw_allocator->AddRequest(expanded_input, expanded_output, vc, 
					  in_priority, cur_vc->GetPriority());
		
	      }
	    }

	    // in active state, we only care about our assigned output port
	    if(vc_state == VC::active) {
	      break;
	    }
	    
	    output = (output + 1) % _outputs;
	  }
	}
	vc = (vc + 1) % _vcs;
      }
    }
    
    // dub: handle fast-path flits separately so we know all switch requests 
    // from other VCs that are on the regular path have been issued already
    for(int vc = 0; vc < _vcs; vc++) {
      
      VC * cur_vc = &_vc[input][vc];
      VC::eVCState vc_state = cur_vc->GetState();
      
      if(_use_fast_path[input*_vcs+vc] && 
	 !cur_vc->Empty() &&
	 ((vc_state == VC::vc_alloc) || (vc_state == VC::active)) && 
	 (cur_vc->GetStateTime() >= _sw_alloc_delay)) {
	
	// dub: "There can be only one!"
	assert(fast_path_vcs[input] < 0);
	
	fast_path_vcs[input] = vc;
	
	Flit * f = cur_vc->FrontFlit();
	assert(f);
	
	if(f->watch)
	  cout << "Entering fast path at " << _fullname 
	       << " at time " << GetSimTime() << endl
	       << *f;
	
	const OutputSet * route_set = cur_vc->GetRouteSet();
	int expanded_input = (vc%_input_speedup)*_inputs+input;
      
	for(int output = 0; output < _outputs; ++output) {
	  
	  // dub: if we're done with VC allocation, we already know our output
	  if(vc_state == VC::active) {
	    output = cur_vc->GetOutputPort();
	  }
	  
	  if(f->watch)
	    cout << "Checking for requests to output port " << output 
		 << "..." << endl;
	  
	  BufferState * dest_vc = &_next_vcs[output];
	  
	  int vc_cnt = route_set->NumVCs(output);
	  assert((vc_state != VC::active) || (vc_cnt > 0));
	  
	  int expanded_output = (input%_output_speedup)*_outputs + output;
	  
	  if(_sw_allocator->ReadRequest(expanded_input, expanded_output) >= 0) {
	    if(f->watch) {
	      cout << " output already requested by buffered flit" << endl;
	    }
	    if(vc_state == VC::active) {
	      break;
	    } else {
	      continue;
	    }
	  }
	  
	  if(f->watch)
	    if(vc_cnt == 0)
	      cout << " no suitable VCs found" << endl;

	  for(int vc_index = 0; vc_index < vc_cnt; ++vc_index) {
	    int vc_prio;
	    int out_vc = route_set->GetVC(output, vc_index, &vc_prio);
	    
	    if(f->watch)
	      cout << " checking for requests to VC " << out_vc << endl;
	    
	    if((((vc_state == VC::vc_alloc) && 
		 dest_vc->IsAvailableFor(out_vc)) ||
		((vc_state == VC::active) && 
		 (out_vc == cur_vc->GetOutputVC()))) &&
	       !dest_vc->IsFullFor(out_vc)) {
	      
	      if(f->watch) {
		cout << "Fast-path switch ";
		if(vc_state == VC::vc_alloc) {
		  cout << "and VC ";
		}
		cout << "allocation requested at "
		     << _fullname << " at time " << GetSimTime() << endl
		     << "  Input: " << input << " VC: " << f->vc << endl
		     << "  Output: " << output << endl
		     << "  Exp. Input: " << expanded_input << endl
		     << "  Exp. Output: " << expanded_output << endl
 		     << *f;
	      }
	      
	      _sw_allocator->AddRequest(expanded_input, expanded_output,
					f->vc, vc_prio, 
					cur_vc->GetPriority());
	      
	    }
	  }
	  
	  if(f->watch)
	    cout << "Done checking for requests to output port " << output 
		 << "..." << endl;
	  
	  if(vc_state == VC::active) {
	    break;
	  }
	  
	}
      }
    }
  }
  
  _sw_allocator->Allocate();
  
  // Winning flits cross the switch

  _crossbar_pipe->WriteAll(NULL);

  //////////////////////////////
  // Switch Power Modelling
  //  - Record Total Cycles
  //
  switchMonitor.cycle() ;

  for(int input = 0; input < _inputs; ++input) {
    
    Credit * c = NULL;
    
    for(int s = 0; s < _input_speedup; ++s) {
      
      int expanded_input = s*_inputs + input;
      int expanded_output;
      VC * cur_vc;
      int vc;
      
      if(_switch_hold_in[expanded_input] != -1) {
	assert(_switch_hold_in[expanded_input] >= 0);
	expanded_output = _switch_hold_in[expanded_input];
	vc = _switch_hold_vc[expanded_input];
	cur_vc = &_vc[input][vc];
	
	if(cur_vc->Empty()) { // Cancel held match if VC is empty
	  expanded_output = -1;
	}
      } else {
	expanded_output = _sw_allocator->OutputAssigned(expanded_input);
	if(expanded_output >= 0) {
	  vc = _sw_allocator->ReadRequest(expanded_input, expanded_output);
	  cur_vc = &_vc[input][vc];
	  }
      }
      
      if(expanded_output >= 0) {
	int output = expanded_output % _outputs;
	
	BufferState * dest_vc = &_next_vcs[output];
	Flit * f = cur_vc->FrontFlit();
	assert(f);
	
	switch(cur_vc->GetState()) {
	  
	case VC::vc_alloc:
	  {
	    const OutputSet * route_set = cur_vc->GetRouteSet();
	    int sel_prio = -1;
	    int sel_vc = -1;
	    int vc_cnt = route_set->NumVCs(output);
	    
	    for(int vc_index = 0; vc_index < vc_cnt; ++vc_index) {
	      int out_prio;
	      int out_vc = route_set->GetVC(output, vc_index, &out_prio);
	      if(dest_vc->IsAvailableFor(out_vc) && 
		 !dest_vc->IsFullFor(out_vc) && 
		 (out_prio > sel_prio)) {
		sel_vc = out_vc;
		sel_prio = out_prio;
	      }
	    }
	    
	    // we should only get to this point if some VC requested 
	    // allocation
	    assert(sel_vc > -1);
	    
	    cur_vc->SetState(VC::active);
	    cur_vc->SetOutput(output, sel_vc);
	    dest_vc->TakeBuffer(sel_vc);
	    
	    _vc_rr_offset[input*_vcs+vc] = (output + 1) % _outputs;
	    
	    if(f->watch) {
	      cout << "Granted VC allocation at " << _fullname 
		   << " at time " << GetSimTime() << endl
		   << "  Input: " << input << " VC: " << vc << endl
		   << "  Output: " << output << " VC: " << sel_vc << endl
		   << *f;
	    }
	  }
	  // NOTE: from here, we just fall through to the code for VC::active!
	  
	case VC::active:
	  
	  if(_hold_switch_for_packet) {
	    _switch_hold_in[expanded_input] = expanded_output;
	    _switch_hold_vc[expanded_input] = vc;
	    _switch_hold_out[expanded_output] = expanded_input;
	  }
	  
	  assert(cur_vc->GetState() == VC::active);
	  assert(!cur_vc->Empty());
	  assert(cur_vc->GetOutputPort() == output);
	  
	  dest_vc = &_next_vcs[output];
	  
	  assert(!dest_vc->IsFullFor(cur_vc->GetOutputVC()));
	  
	  // Forward flit to crossbar and send credit back
	  f = cur_vc->RemoveFlit();
	  
	  if(f->watch) {
	    cout << "Granted switch allocation at " << _fullname 
		 << " at time " << GetSimTime() << endl
		 << "  Input: " << input << " VC: " << vc << endl
		 << "  Output: " << output << endl
		 << "  Exp. Input: " << expanded_input << endl
		 << "  Exp. Output: " << expanded_output << endl
		 << *f;
	  }
	  
	  f->hops++;
	  
	  //
	  // Switch Power Modelling
	  //
	  switchMonitor.traversal(input, output, f);
	  bufferMonitor.read(input, f);
	  
	  if(f->watch) {
	    cout << "Forwarding flit through crossbar at " << _fullname 
		 << " at time " << GetSimTime() << endl
		 << "  Input: " << expanded_input << endl
		 << "  Output: " << expanded_output << endl
		 << *f;
	  }
	  
	  if (c == NULL) {
	    c = _NewCredit(_vcs);
	  }
	  
	  c->vc[c->vc_cnt] = f->vc;
	  c->vc_cnt++;
	  c->dest_router = f->from_router;
	  f->vc = cur_vc->GetOutputVC();
	  dest_vc->SendingFlit(f);
	  
	  _crossbar_pipe->Write(f, expanded_output);
	  
	  if(f->tail) {
	    cur_vc->SetState(VC::idle);
	    _switch_hold_in[expanded_input] = -1;
	    _switch_hold_vc[expanded_input] = -1;
	    _switch_hold_out[expanded_output] = -1;
	  } else {
	    // reset state timer for next flit
	    cur_vc->SetState(VC::active);
	  }	  

	  if(!_use_fast_path[input*_vcs+f->vc]) {
	    _sw_rr_offset[expanded_input] = (f->vc + 1) % _vcs;
	  }
	  
	  if(cur_vc->Empty() && !_use_fast_path[input*_vcs+f->vc]) {
	    if(f->watch) {
	      cout << "Enabling fast path for input " << input
		   << ", VC " << f->vc << " at " << _fullname
		   << " at time " << GetSimTime() << endl
		   << *f;
	    }
	    _use_fast_path[input*_vcs+f->vc] = true;
	  }
	  
	} 
      }
    }
    
    int vc = fast_path_vcs[input];
    if(vc >= 0) {
      assert(_use_fast_path[input*_vcs+vc]);
      int expanded_input = (vc % _input_speedup) * _inputs + input;
      int expanded_output = _sw_allocator->OutputAssigned(expanded_input);
      if((expanded_output < 0) ||
	 (_sw_allocator->ReadRequest(expanded_input, expanded_output) != vc)) {
	
	Flit * f = _vc[input][vc].FrontFlit();
	assert(f);
	
	if(f->watch) {
	  cout << "Disabling fast path for input " << input
	       << ", VC " << vc << " at " << _fullname
	       << " at time " << GetSimTime() << endl
	       << *f;
	}
	_use_fast_path[input*_vcs+vc] = false;
      }
    }
    
    _credit_pipe->Write(c, input);
  }
}
