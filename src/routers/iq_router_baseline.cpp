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
#include <limits>

#include "globals.hpp"
#include "random_utils.hpp"
#include "vc.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer.hpp"
#include "buffer_state.hpp"
#include "pipefifo.hpp"
#include "allocator.hpp"
#include "iq_router_baseline.hpp"
#include "switch_monitor.hpp"
#include "buffer_monitor.hpp"

IQRouterBaseline::IQRouterBaseline( const Configuration& config,
				    Module *parent, const string & name, int id,
				    int inputs, int outputs )
  : IQRouterBase( config, parent, name, id, inputs, outputs )
{
  // Alloc allocators
  string alloc_type;
  config.GetStr( "vc_allocator", alloc_type );
  string arb_type;
  config.GetStr( "vc_alloc_arb_type", arb_type );
  int iters = config.GetInt( "vc_alloc_iters" );
  if(iters == 0) iters = config.GetInt("alloc_iters");
  _vc_allocator = Allocator::NewAllocator( this, "vc_allocator",
					   alloc_type,
					   _vcs*_inputs,
					   _vcs*_outputs,
					   iters, arb_type );

  if ( !_vc_allocator ) {
    cout << "ERROR: Unknown vc_allocator type " << alloc_type << endl;
    exit(-1);
  }

  config.GetStr( "sw_allocator", alloc_type );
  config.GetStr( "sw_alloc_arb_type", arb_type );
  iters = config.GetInt("sw_alloc_iters");
  if(iters == 0) iters = config.GetInt("alloc_iters");
  _sw_allocator = Allocator::NewAllocator( this, "sw_allocator",
					   alloc_type,
					   _inputs*_input_speedup, 
					   _outputs*_output_speedup,
					   iters, arb_type );

  if ( !_sw_allocator ) {
    cout << "ERROR: Unknown sw_allocator type " << alloc_type << endl;
    exit(-1);
  }
  
  _speculative = config.GetInt( "speculative" ) ;
  
  if ( _speculative >= 2 ) {
    
    string filter_spec_grants;
    config.GetStr("filter_spec_grants", filter_spec_grants);
    if(filter_spec_grants == "any_nonspec_gnts") {
      _filter_spec_grants = 0;
    } else if(filter_spec_grants == "confl_nonspec_reqs") {
      _filter_spec_grants = 1;
    } else if(filter_spec_grants == "confl_nonspec_gnts") {
      _filter_spec_grants = 2;
    } else assert(false);
    
    _spec_sw_allocator = Allocator::NewAllocator( this, "spec_sw_allocator",
						  alloc_type,
						  _inputs*_input_speedup, 
						  _outputs*_output_speedup,
						  iters, arb_type );
    if ( !_spec_sw_allocator ) {
      cout << "ERROR: Unknown sw_allocator type " << alloc_type << endl;
      exit(-1);
    }

  }

  _sw_rr_offset.resize(_inputs*_input_speedup);
  for(int i = 0; i < _inputs*_input_speedup; ++i)
    _sw_rr_offset[i] = i % _input_speedup;
  
}

IQRouterBaseline::~IQRouterBaseline( )
{
  delete _vc_allocator;
  delete _sw_allocator;

  if ( _speculative >= 2 )
    delete _spec_sw_allocator;
}
  
void IQRouterBaseline::_Alloc( )
{
  _VCAlloc( );
  _SWAlloc( );
}

void IQRouterBaseline::_VCAlloc( )
{
  bool watched = false;

  _vc_allocator->Clear( );

  for ( set<pair<int, int> >::iterator item = _vcalloc_vcs.begin(); item!=_vcalloc_vcs.end(); ++item ) {
    int input = item->first;
    int vc = item->second;
    Buffer * cur_buf = _buf[input];
    if ( ( _speculative > 0 ) && ( cur_buf->GetState(vc) == VC::vc_alloc )){
      cur_buf->SetState(vc, VC::vc_spec);
    }
    if (  cur_buf->GetStateTime(vc) >= _vc_alloc_delay  ) {
      Flit * f = cur_buf->FrontFlit(vc);
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		   << "VC " << vc << " at input " << input
		   << " is requesting VC allocation for flit " << f->id
		   << "." << endl;
	watched = true;
      }
      
      const OutputSet *route_set    = cur_buf->GetRouteSet(vc);
      int out_priority = cur_buf->GetPriority(vc);
      const set<OutputSet::sSetElement> setlist = route_set ->GetSet();
      //cout<<setlist->size()<<endl;
      set<OutputSet::sSetElement>::const_iterator iset = setlist.begin( );
      while(iset!=setlist.end( )){
	BufferState *dest_buf = _next_buf[iset->output_port];
	for ( int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc ) {
	  int in_priority = iset->pri;
	  // On the input input side, a VC might request several output 
	  // VCs.  These VCs can be prioritized by the routing function
	  // and this is reflected in "in_priority".  On the output,
	  // if multiple VCs are requesting the same output VC, the priority
	  // of VCs is based on the actual packet priorities, which is
	  // reflected in "out_priority".
	    
	  //	    cout<<
	  if(dest_buf->IsAvailableFor(out_vc)) {
	    if(f->watch){
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "Requesting VC " << out_vc
			 << " at output " << iset->output_port 
			 << " with priorities " << in_priority
			 << " and " << out_priority
			 << "." << endl;
	    }
	    _vc_allocator->AddRequest(input*_vcs + vc, iset->output_port*_vcs + out_vc, 
				      out_vc, in_priority, out_priority);
	  } else {
	    if(f->watch)
	      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 << "VC " << out_vc << " at output " << iset->output_port 
			 << " is unavailable." << endl;
	  }
	}
	//go to the next item in the outputset
	iset++;
      }
    }
    
  }
  //  watched = true;
  if ( watched ) {
    *gWatchOut << GetSimTime() << " | " << _vc_allocator->FullName() << " | ";
    _vc_allocator->PrintRequests( gWatchOut );
  }

  _vc_allocator->Allocate( );

  // Winning flits get a VC

  for ( int output = 0; output < _outputs; ++output ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {
      int input_and_vc = _vc_allocator->InputAssigned( output*_vcs + vc );

      if ( input_and_vc != -1 ) {
	assert(input_and_vc >= 0);
	int match_input = input_and_vc / _vcs;
	int match_vc    = input_and_vc % _vcs;

	Buffer * cur_buf  = _buf[match_input];
	BufferState * dest_buf = _next_buf[output];

	if ( _speculative > 0 )
	  cur_buf->SetState(match_vc, VC::vc_spec_grant);
	else
	  cur_buf->SetState(match_vc, VC::active);
	_vcalloc_vcs.erase(make_pair(match_input, match_vc));
	
	cur_buf->SetOutput(match_vc, output, vc);
	dest_buf->TakeBuffer( vc );

	Flit * f = cur_buf->FrontFlit(match_vc);
	
	if(f->watch)
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "Granted VC " << vc << " at output " << output
		     << " to VC " << match_vc << " at input " << match_input
		     << " (flit: " << f->id << ")." << endl;
      }
    }
  }
}

void IQRouterBaseline::_SWAlloc( )
{
  bool watched = false;

  bool any_nonspec_reqs = false;
  vector<bool> any_nonspec_output_reqs(_outputs*_output_speedup, 0);
  
  _sw_allocator->Clear( );
  if ( _speculative >= 2 )
    _spec_sw_allocator->Clear( );
  
  for ( int input = 0; input < _inputs; ++input ) {
    int vc_ready_nonspec = 0;
    int vc_ready_spec = 0;
    for ( int s = 0; s < _input_speedup; ++s ) {
      int expanded_input  = input * _input_speedup + s;
      
      // Arbitrate (round-robin) between multiple 
      // requesting VCs at the same input (handles 
      // the case when multiple VC's are requesting
      // the same output port)
      int vc = _sw_rr_offset[ expanded_input ];
      assert((vc % _input_speedup) == s);

      for ( int v = 0; v < _vcs / _input_speedup; ++v ) {

	Buffer * cur_buf = _buf[input];

	if(!cur_buf->Empty(vc) &&
	   (cur_buf->GetStateTime(vc) >= _sw_alloc_delay)) {
	  
	  switch(cur_buf->GetState(vc)) {
	    
	  case VC::active:
	    {
	      int output = cur_buf->GetOutputPort(vc);

	      BufferState * dest_buf = _next_buf[output];
	      
	      if ( !dest_buf->IsFullFor( cur_buf->GetOutputVC(vc) ) ) {
		
		// When input_speedup > 1, the virtual channel buffers are 
		// interleaved to create multiple input ports to the switch. 
		// Similarily, the output ports are interleaved based on their 
		// originating input when output_speedup > 1.
		
		assert( expanded_input == input *_input_speedup + vc % _input_speedup );
		int expanded_output = 
		  output * _output_speedup + input % _output_speedup;
		
		if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		     ( _switch_hold_out[expanded_output] == -1 ) ) {
		  
		  // We could have requested this same input-output pair in a 
		  // previous iteration; only replace the previous request if 
		  // the current request has a higher priority (this is default 
		  // behavior of the allocators).  Switch allocation priorities 
		  // are strictly determined by the packet priorities.
		  
		  Flit * f = cur_buf->FrontFlit(vc);
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
		  any_nonspec_reqs = true;
		  any_nonspec_output_reqs[expanded_output] = true;
		  vc_ready_nonspec++;
		}
	      } else {
		//if this vc has a hold on the switch need to cancel it to prevent deadlock
		if(_hold_switch_for_packet){
		  int expanded_output = 
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
	    }
	    break;
	    
	    
	    //
	    // The following models the speculative VC allocation aspects 
	    // of the pipeline. An input VC with a request in for an egress
	    // virtual channel will also speculatively bid for the switch
	    // regardless of whether the VC allocation succeeds. These
	    // speculative requests are handled in a separate allocator so 
	    // as to prevent them from interfering with non-speculative bids
	    //
	  case VC::vc_spec:
	  case VC::vc_spec_grant:
	    {	      
	      assert( _speculative > 0 );
	      assert( expanded_input == input * _input_speedup + vc % _input_speedup );
	      
	      const OutputSet * route_set = cur_buf->GetRouteSet(vc);
	      const set<OutputSet::sSetElement> setlist = route_set->GetSet();
	      set<OutputSet::sSetElement>::const_iterator iset = setlist.begin( );
	      while(iset!=setlist.end( )){

		bool do_request = (_speculative < 3);

		if(_speculative >= 3) {

		  BufferState * dest_buf = _next_buf[iset->output_port];
		  
		  // check if at least one suitable VC is available at this output
		  
		  for ( int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc ) {
		    if(dest_buf->IsAvailableFor(out_vc)) {
		      do_request = true;
		      break;
		    }
		  }
		}
		
		if(do_request) { 
		  int expanded_output = iset->output_port * _output_speedup + input % _output_speedup;
		  if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		       ( _switch_hold_out[expanded_output] == -1 ) ) {
		    
		    int prio = ((_speculative == 1) ? numeric_limits<int>::min() : 0) + cur_buf->GetPriority(vc);

		    Flit * f = cur_buf->FrontFlit(vc);
		    assert(f);
		    if(f->watch) {
		      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
				 << "VC " << vc << " at input " << input 
				 << " requested output " << iset->output_port
				 << " (spec., exp. input: " << expanded_input
				 << ", exp. output: " << expanded_output
				 << ", flit: " << f->id
				 << ", prio: " << prio
				 << ")." << endl;
		      watched = true;
		    }
		    
		    // dub: for the old-style speculation implementation, we 
		    // overload the packet priorities to prioritize non-
		    // speculative requests over speculative ones
		    if( _speculative == 1 )
		      _sw_allocator->AddRequest(expanded_input, expanded_output,
						vc, prio, prio);
		    else
		      _spec_sw_allocator->AddRequest(expanded_input, 
						     expanded_output, vc,
						     prio, prio);
		    vc_ready_spec++;
		  }
		}
		iset++;
	      }
	    }
	    break;
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
    if(_speculative >= 2) {
      *gWatchOut << GetSimTime() << " | " << _spec_sw_allocator->FullName() << " | ";
      _spec_sw_allocator->PrintRequests( gWatchOut );
    }
  }
  
  _sw_allocator->Allocate();
  if(_speculative >= 2)
    _spec_sw_allocator->Allocate();
  
  // Winning flits cross the switch

  _crossbar_pipe->WriteAll( 0 );

  //////////////////////////////
  // Switch Power Modelling
  //  - Record Total Cycles
  //
  _switchMonitor->cycle() ;

  for ( int input = 0; input < _inputs; ++input ) {
    Credit * c = 0;
    
    int vc_grant_nonspec = 0;
    int vc_grant_spec = 0;
    
    Buffer * cur_buf = _buf[input];

    for ( int s = 0; s < _input_speedup; ++s ) {

      bool use_spec_grant = false;
      
      int expanded_input  = input * _input_speedup + s;
      int expanded_output;
      int vc;

      if ( _switch_hold_in[expanded_input] != -1 ) {
	assert(_switch_hold_in[expanded_input] >= 0);
	expanded_output = _switch_hold_in[expanded_input];
	vc = _switch_hold_vc[expanded_input];
	assert(vc >= 0);
	
	if ( cur_buf->Empty(vc) ) { // Cancel held match if VC is empty
	  _switch_hold_in[expanded_input]   = -1;
	  _switch_hold_vc[expanded_input]   = -1;
	  _switch_hold_out[expanded_output] = -1;
	  expanded_output = -1;
	}
      } else {
	expanded_output = _sw_allocator->OutputAssigned( expanded_input );
	if ( ( _speculative >= 2 ) && ( expanded_output < 0 ) ) {
	  expanded_output = _spec_sw_allocator->OutputAssigned(expanded_input);
	  if ( expanded_output >= 0 ) {
	    assert(_spec_sw_allocator->InputAssigned(expanded_output) >= 0);
	    assert(_spec_sw_allocator->ReadRequest(expanded_input, expanded_output) >= 0);
	    switch ( _filter_spec_grants ) {
	    case 0:
	      if ( any_nonspec_reqs )
		expanded_output = -1;
	      break;
	    case 1:
	      if ( any_nonspec_output_reqs[expanded_output] )
		expanded_output = -1;
	      break;
	    case 2:
	      if ( _sw_allocator->InputAssigned(expanded_output) >= 0 )
		expanded_output = -1;
	      break;
	    default:
	      assert(false);
	    }
	  }
	  use_spec_grant = (expanded_output >= 0);
	}
      }

      if ( expanded_output >= 0 ) {
	int output = expanded_output / _output_speedup;

	if ( _switch_hold_in[expanded_input] == -1 ) {
	  if(use_spec_grant) {
	    assert(_spec_sw_allocator->OutputAssigned(expanded_input) >= 0);
	    assert(_spec_sw_allocator->InputAssigned(expanded_output) >= 0);
	    vc = _spec_sw_allocator->ReadRequest(expanded_input, 
						 expanded_output);
	  } else {
	    assert(_sw_allocator->OutputAssigned(expanded_input) >= 0);
	    assert(_sw_allocator->InputAssigned(expanded_output) >= 0);
	    vc = _sw_allocator->ReadRequest(expanded_input, expanded_output);
	  }
	  assert(vc >= 0);
	}

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
	  
	  BufferState * dest_buf = _next_buf[output];
	  
	  if ( dest_buf->IsFullFor( cur_buf->GetOutputVC( vc ) ) )
	    continue ;
	  
	  // Forward flit to crossbar and send credit back
	  Flit * f = cur_buf->RemoveFlit(vc);
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
	  
	  //
	  // Switch Power Modelling
	  //
	  _switchMonitor->traversal( input, output, f) ;
	  _bufferMonitor->read(input, f) ;
	  
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Forwarding flit " << f->id << " through crossbar "
		       << "(exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ")." << endl;
	  
	  if ( !c ) {
	    c = _NewCredit( _vcs );
	  }

	  assert(vc == f->vc);

	  c->vc[c->vc_cnt] = f->vc;
	  c->vc_cnt++;
	  c->dest_router = f->from_router;
	  f->vc = cur_buf->GetOutputVC(vc);
	  dest_buf->SendingFlit( f );
	  
	  _crossbar_pipe->Write( f, expanded_output );
	  
	  if(f->tail) {
	    if(cur_buf->Empty(vc)) {
	      cur_buf->SetState(vc, VC::idle);
	    } else if(_routing_delay > 0) {
	      cur_buf->SetState(vc, VC::routing);
	      _routing_vcs.push(make_pair(input, vc));
	    } else {
	      cur_buf->Route(vc, _rf, this, cur_buf->FrontFlit(vc), input);
	      cur_buf->SetState(vc, VC::vc_alloc);
	      _vcalloc_vcs.insert(make_pair(input, vc));
	    }
	    _switch_hold_in[expanded_input]   = -1;
	    _switch_hold_vc[expanded_input]   = -1;
	    _switch_hold_out[expanded_output] = -1;
	  } else {
	    // reset state timer for next flit
	    cur_buf->SetState(vc, VC::active);
	  }
	  
	  int next_offset = vc + _input_speedup;
	  _sw_rr_offset[expanded_input] = 
	    (next_offset < _vcs) ? next_offset : s;

	} else {
	  assert(cur_buf->GetState(vc) == VC::vc_spec);
	  Flit * f = cur_buf->FrontFlit(vc);
	  assert(f);
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Speculation failed at output " << output
		       << "(exp. input: " << expanded_input
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
    
    _credit_pipe->Write( c, input );
  }
}
