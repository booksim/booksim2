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
#include <stdlib.h>
#include <assert.h>

#include "globals.hpp"
#include "random_utils.hpp"
#include "vc.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer_state.hpp"
#include "pipefifo.hpp"
#include "allocator.hpp"
#include "iq_router_baseline.hpp"

IQRouterBaseline::IQRouterBaseline( const Configuration& config,
				    Module *parent, const string & name, int id,
				    int inputs, int outputs )
  : IQRouterBase( config, parent, name, id, inputs, outputs )
{
  string alloc_type;
  string arb_type;
  int iters;

  // Alloc allocators
  config.GetStr( "vc_allocator", alloc_type );
  config.GetStr( "vc_alloc_arb_type", arb_type );
  iters = config.GetInt( "vc_alloc_iters" );
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
  VC          *cur_vc;
  BufferState *dest_vc;
  int         input_and_vc;
  int         match_input;
  int         match_vc;

  Flit        *f;
  bool        watched = false;

  _vc_allocator->Clear( );


  for ( set<int>::iterator item = _vcalloc_vcs.begin(); item!=_vcalloc_vcs.end(); ++item ) {
    int vc_encode = *item;
    int input =  vc_encode/_vcs;
    int vc =vc_encode%_vcs;
    cur_vc = _vc[input][vc];
    if ( ( _speculative > 0 ) && ( cur_vc->GetState( ) == VC::vc_alloc )){
      cur_vc->SetState( VC::vc_spec ) ;
    }
    if (  cur_vc->GetStateTime( ) >= _vc_alloc_delay  ) {
      f = cur_vc->FrontFlit( );
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		   << "VC " << vc << " at input " << input
		   << " is requesting VC allocation for flit " << f->id
		   << "." << endl;
	watched = true;
      }
      
      const OutputSet *route_set    = cur_vc->GetRouteSet( );
      int out_priority = cur_vc->GetPriority( );
      const list<OutputSet::sSetElement>* setlist = route_set ->GetSetList();
      //cout<<setlist->size()<<endl;
      list<OutputSet::sSetElement>::const_iterator iset = setlist->begin( );
      while(iset!=setlist->end( )){
	BufferState *dest_vc = _next_vcs[iset->output_port];
	for ( int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc ) {
	  int in_priority = iset->pri;
	  // On the input input side, a VC might request several output 
	  // VCs.  These VCs can be prioritized by the routing function
	  // and this is reflected in "in_priority".  On the output,
	  // if multiple VCs are requesting the same output VC, the priority
	  // of VCs is based on the actual packet priorities, which is
	  // reflected in "out_priority".
	    
	  //	    cout<<
	  if(dest_vc->IsAvailableFor(out_vc)) {
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
      input_and_vc = _vc_allocator->InputAssigned( output*_vcs + vc );

      if ( input_and_vc != -1 ) {
	assert(input_and_vc >= 0);
	match_input = input_and_vc / _vcs;
	match_vc    = input_and_vc - match_input*_vcs;

	cur_vc  = _vc[match_input][match_vc];
	dest_vc = _next_vcs[output];

	if ( _speculative > 0 )
	  cur_vc->SetState( VC::vc_spec_grant );
	else
	  cur_vc->SetState( VC::active );
	_vcalloc_vcs.erase(match_input*_vcs+match_vc);
	
	cur_vc->SetOutput( output, vc );
	dest_vc->TakeBuffer( vc );

	f = cur_vc->FrontFlit( );
	
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
  Flit        *f;
  Credit      *c;

  VC          *cur_vc;
  BufferState *dest_vc;

  int input;
  int output;
  int vc;

  int expanded_input;
  int expanded_output;
  
  bool        watched = false;

  bool any_nonspec_reqs = false;
  bool any_nonspec_output_reqs[_outputs*_output_speedup];
  memset(any_nonspec_output_reqs, 0, _outputs*_output_speedup*sizeof(bool));
  
  _sw_allocator->Clear( );
  if ( _speculative >= 2 )
    _spec_sw_allocator->Clear( );
  
  for ( input = 0; input < _inputs; ++input ) {
    int vc_ready_nonspec = 0;
    int vc_ready_spec = 0;
    for ( int s = 0; s < _input_speedup; ++s ) {
      expanded_input  = s*_inputs + input;
      
      // Arbitrate (round-robin) between multiple 
      // requesting VCs at the same input (handles 
      // the case when multiple VC's are requesting
      // the same output port)
      vc = _sw_rr_offset[ expanded_input ];

      for ( int v = 0; v < _vcs; ++v ) {

	// This continue acounts for the interleaving of 
	// VCs when input speedup is used
	// dub: Essentially, this skips loop iterations corresponding to those 
	// VCs not in the current speedup set. The skipped iterations will be 
	// handled in a different iteration of the enclosing loop over 's'.
	if ( ( vc % _input_speedup ) != s ) {
	  vc = ( vc + 1 ) % _vcs;
	  continue;
	}
	
	cur_vc = _vc[input][vc];

	if(!cur_vc->Empty() &&
	   (cur_vc->GetStateTime() >= _sw_alloc_delay)) {
	  
	  switch(cur_vc->GetState()) {
	    
	  case VC::active:
	    {
	      output = cur_vc->GetOutputPort( );

	      dest_vc = _next_vcs[output];
	      
	      if ( !dest_vc->IsFullFor( cur_vc->GetOutputVC( ) ) ) {
		
		// When input_speedup > 1, the virtual channel buffers are 
		// interleaved to create multiple input ports to the switch. 
		// Similarily, the output ports are interleaved based on their 
		// originating input when output_speedup > 1.
		
		assert( expanded_input == (vc%_input_speedup)*_inputs + input );
		expanded_output = 
		  (input%_output_speedup)*_outputs + output;
		
		if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		     ( _switch_hold_out[expanded_output] == -1 ) ) {
		  
		  // We could have requested this same input-output pair in a 
		  // previous iteration; only replace the previous request if 
		  // the current request has a higher priority (this is default 
		  // behavior of the allocators).  Switch allocation priorities 
		  // are strictly determined by the packet priorities.
		  
		  Flit * f = cur_vc->FrontFlit();
		  assert(f);
		  if(f->watch) {
		    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			       << "VC " << vc << " at input " << input 
			       << " requested output " << output 
			       << " (non-spec., exp. input: " << expanded_input
			       << ", exp. output: " << expanded_output
			       << ", flit: " << f->id
			       << ", prio: " << cur_vc->GetPriority()
			       << ")." << endl;
		    watched = true;
		  }
		  
		  // dub: for the old-style speculation implementation, we 
		  // overload the packet priorities to prioritize 
		  // non-speculative requests over speculative ones
		  if( _speculative == 1 )
		    _sw_allocator->AddRequest(expanded_input, expanded_output, 
					      vc, 1, 1);
		  else
		    _sw_allocator->AddRequest(expanded_input, expanded_output, 
					      vc, cur_vc->GetPriority( ), 
					      cur_vc->GetPriority( ));
		  any_nonspec_reqs = true;
		  any_nonspec_output_reqs[expanded_output] = true;
		  vc_ready_nonspec++;
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
	      assert( expanded_input == (vc%_input_speedup)*_inputs + input );
	      
	      const OutputSet * route_set = cur_vc->GetRouteSet( );
	      const list<OutputSet::sSetElement>* setlist = route_set ->GetSetList();
	      list<OutputSet::sSetElement>::const_iterator iset = setlist->begin( );
	      while(iset!=setlist->end( )){
		BufferState * dest_vc = _next_vcs[iset->output_port];
		bool do_request = false;
		
		// check if any suitable VCs are available
	
		for ( int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc ) {
		  int vc_prio = iset->pri;
		  if(!do_request && 
		     ((_speculative < 3) || dest_vc->IsAvailableFor(out_vc))) {
		    do_request = true;
		    break;
		  }
		}
		
		if(do_request) { 
		  expanded_output = (input%_output_speedup)*_outputs + iset->output_port;
		  if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		       ( _switch_hold_out[expanded_output] == -1 ) ) {
		    
		    Flit * f = cur_vc->FrontFlit();
		    assert(f);
		    if(f->watch) {
		      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
				 << "VC " << vc << " at input " << input 
				 << " requested output " << iset->output_port
				 << " (spec., exp. input: " << expanded_input
				 << ", exp. output: " << expanded_output
				 << ", flit: " << f->id
				 << ", prio: " << cur_vc->GetPriority()
				 << ")." << endl;
		      watched = true;
		    }
		    
		    // dub: for the old-style speculation implementation, we 
		    // overload the packet priorities to prioritize non-
		    // speculative requests over speculative ones
		    if( _speculative == 1 )
		      _sw_allocator->AddRequest(expanded_input, expanded_output,
						vc, 0, 0);
		    else
		      _spec_sw_allocator->AddRequest(expanded_input, 
						     expanded_output, vc,
						     cur_vc->GetPriority( ), 
						     cur_vc->GetPriority( ));
		    vc_ready_spec++;
		  }
		}
		iset++;
	      }
	    }
	    break;
	  }
	}
	vc = ( vc + 1 ) % _vcs;
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
  switchMonitor.cycle() ;

  for ( int input = 0; input < _inputs; ++input ) {
    c = 0;
    
    int vc_grant_nonspec = 0;
    int vc_grant_spec = 0;
    
    for ( int s = 0; s < _input_speedup; ++s ) {

      bool use_spec_grant = false;
      
      expanded_input  = s*_inputs + input;

      if ( _switch_hold_in[expanded_input] != -1 ) {
	assert(_switch_hold_in[expanded_input] >= 0);
	expanded_output = _switch_hold_in[expanded_input];
	vc = _switch_hold_vc[expanded_input];
	assert(vc >= 0);
	cur_vc = _vc[input][vc];
	
	if ( cur_vc->Empty( ) ) { // Cancel held match if VC is empty
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
	output = expanded_output % _outputs;

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
	  cur_vc = _vc[input][vc];
	}

	// Detect speculative switch requests which succeeded when VC 
	// allocation failed and prevenet the switch from forwarding;
	// also, in case the routing function can return multiple outputs, 
	// check to make sure VC allocation and speculative switch allocation 
	// pick the same output port.
	if ( ( ( cur_vc->GetState() == VC::vc_spec_grant ) ||
	       ( cur_vc->GetState() == VC::active ) ) &&
	     ( cur_vc->GetOutputPort() == output ) ) {
	  
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
	  
	  assert((cur_vc->GetState() == VC::vc_spec_grant) ||
		 (cur_vc->GetState() == VC::active));
	  assert(!cur_vc->Empty());
	  assert(cur_vc->GetOutputPort() == output);
	  
	  dest_vc = _next_vcs[output];
	  
	  if ( dest_vc->IsFullFor( cur_vc->GetOutputVC( ) ) )
	    continue ;
	  
	  // Forward flit to crossbar and send credit back
	  f = cur_vc->RemoveFlit( );
	  assert(f);
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Output " << output
		       << " granted to VC " << vc << " at input " << input;
	    if(cur_vc->GetState() == VC::vc_spec_grant)
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
	  switchMonitor.traversal( input, output, f) ;
	  bufferMonitor.read(input, f) ;
	  
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
	  f->vc = cur_vc->GetOutputVC( );
	  dest_vc->SendingFlit( f );
	  
	  _crossbar_pipe->Write( f, expanded_output );
	  
	  if(f->tail) {
	    if(cur_vc->Empty()) {
	      cur_vc->SetState(VC::idle);
	    } else if(_routing_delay > 0) {
	      cur_vc->SetState(VC::routing);
	      _routing_vcs.push(input*_vcs+vc);
	    } else {
	      cur_vc->Route(_rf, this, cur_vc->FrontFlit(), input);
	      cur_vc->SetState(VC::vc_alloc);
	      _vcalloc_vcs.insert(input*_vcs+vc);
	    }
	    _switch_hold_in[expanded_input]   = -1;
	    _switch_hold_vc[expanded_input]   = -1;
	    _switch_hold_out[expanded_output] = -1;
	  } else {
	    // reset state timer for next flit
	    cur_vc->SetState(VC::active);
	  }
	  
	  _sw_rr_offset[expanded_input] = ( vc + 1 ) % _vcs;
	} else {
	  assert(cur_vc->GetState() == VC::vc_spec);
	  Flit * f = cur_vc->FrontFlit();
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
      cur_vc = _vc[input][vc] ;
      if ( cur_vc->GetState() == VC::vc_spec_grant ) {
	cur_vc->SetState( VC::active ) ;	
      } 
    }
    
    _credit_pipe->Write( c, input );
  }
}
