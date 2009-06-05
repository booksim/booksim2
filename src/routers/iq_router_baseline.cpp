// $Id$

/*
Copyright (c) 2007, Trustees of Leland Stanford Junior University
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
#include "iq_router_baseline.hpp"

IQRouterBaseline::IQRouterBaseline( const Configuration& config,
		    Module *parent, string name, int id,
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
  
  if ( _speculative == 2 ) {
    
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

  _sw_rr_offset = new int [_inputs*_input_speedup];
  for ( int i = 0; i < _inputs*_input_speedup; ++i ) {
    _sw_rr_offset[i] = 0;
  }
}

IQRouterBaseline::~IQRouterBaseline( )
{
  delete _vc_allocator;
  delete _sw_allocator;

  if ( _speculative == 2 )
    delete _spec_sw_allocator;

  delete [] _sw_rr_offset;
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
  bool        watched;

  _vc_allocator->Clear( );
  watched = false;

  for ( int input = 0; input < _inputs; ++input ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {

      cur_vc = &_vc[input][vc];
      
      if ( ( _speculative > 0 ) && ( cur_vc->GetState( ) == VC::vc_alloc ) )
	cur_vc->SetState( VC::vc_spec ) ;
      
      if ( ( ( cur_vc->GetState( ) == VC::vc_alloc ) ||
	     ( cur_vc->GetState( ) == VC::vc_spec ) ) &&
	   ( cur_vc->GetStateTime( ) >= _vc_alloc_delay ) ) {
	
  	f = cur_vc->FrontFlit( );
	if ( f->watch ) {
	  cout << "VC requesting allocation at " << _fullname
	       << " at time " << GetSimTime() << endl
	       << "  Input: " << input << " VC: " << vc << endl
	       << *f;
	  watched = true;
	}
	
	const OutputSet *route_set    = cur_vc->GetRouteSet( );
	int out_priority = cur_vc->GetPriority( );
	
	for ( int output = 0; output < _outputs; ++output ) {
	  int vc_cnt = route_set->NumVCs( output );
	  BufferState *dest_vc = &_next_vcs[output];
	  
	  for ( int vc_index = 0; vc_index < vc_cnt; ++vc_index ) {
	    int in_priority;
	    int out_vc = route_set->GetVC( output, vc_index, &in_priority );
	    
	    if ( f->watch ) {
	      cout << "  trying vc " << out_vc << " (out = " << output << ") ... ";
	    }
	    
	    // On the input input side, a VC might request several output 
	    // VCs.  These VCs can be prioritized by the routing function
	    // and this is reflected in "in_priority".  On the output,
	    // if multiple VCs are requesting the same output VC, the priority
	    // of VCs is based on the actual packet priorities, which is
	    // reflected in "out_priority".
	    
	    if ( dest_vc->IsAvailableFor( out_vc ) ) {
	      _vc_allocator->AddRequest( input*_vcs + vc, output*_vcs + out_vc, 
					 out_vc, in_priority, out_priority );
	      if ( f->watch ) {
		cout << "available" << endl;
	      }
	    } else if ( f->watch ) {
	      cout << "busy" << endl;
	    }
	  }
	}
      }
    }
    
  }
  //  watched = true;
  if ( watched ) {
    _vc_allocator->PrintRequests( );
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

	cur_vc  = &_vc[match_input][match_vc];
	dest_vc = &_next_vcs[output];

	if ( _speculative > 0 )
	  cur_vc->SetState( VC::vc_spec_grant );
	else
	  cur_vc->SetState( VC::active );

	cur_vc->SetOutput( output, vc );
	dest_vc->TakeBuffer( vc );

	f = cur_vc->FrontFlit( );
	
	if ( f->watch ) {
	  cout << "Granted VC allocation at " << _fullname 
	       << " at time " << GetSimTime() << endl
	       << "  Input: " << match_input << " VC: " << match_vc << endl
	       << "  Output: " << output << " VC: " << vc << endl
	       << *f;
	}
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
  
  bool any_nonspec_reqs = false;
  bool any_nonspec_output_reqs[_outputs*_output_speedup];
  memset(any_nonspec_output_reqs, 0, _outputs*_output_speedup*sizeof(bool));
  
  _sw_allocator->Clear( );
  if ( _speculative == 2 )
    _spec_sw_allocator->Clear( );
  
  for ( input = 0; input < _inputs; ++input ) {
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
	
	cur_vc = &_vc[input][vc];

	if((cur_vc->GetStateTime() >= _sw_alloc_delay) &&
	   !cur_vc->Empty()) {
	  
	  switch(cur_vc->GetState()) {
	    
	  case VC::active:
	    {
	      
	      dest_vc = &_next_vcs[cur_vc->GetOutputPort( )];
	      
	      if ( !dest_vc->IsFullFor( cur_vc->GetOutputVC( ) ) ) {
		
		// When input_speedup > 1, the virtual channel buffers are 
		// interleaved to create multiple input ports to the switch. 
		// Similarily, the output ports are interleaved based on their 
		// originating input when output_speedup > 1.
		
		assert( expanded_input == (vc%_input_speedup)*_inputs + input );
		expanded_output = 
		  (input%_output_speedup)*_outputs + cur_vc->GetOutputPort( );
		
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
		    cout << "Switch allocation requested at " << _fullname
			 << " at time " << GetSimTime() << endl
			 << "  Input: " << input << " VC: " << vc << endl
			 << "  Output: " << cur_vc->GetOutputPort() << endl
			 << *f;
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
	      int out_priority = cur_vc->GetPriority( );
	      
	      for ( int output = 0; output < _outputs; ++output ) {
		int vc_cnt = route_set->NumVCs( output );
		if ( vc_cnt != 0 ) {
		  int in_priority = 0;
		  for ( int vc_index = 0; vc_index < vc_cnt; ++vc_index ) {
		    int vc_prio;
		    int out_vc = route_set->GetVC( output, vc_index, &vc_prio );
		    if ( vc_prio > in_priority ) {
		      in_priority = vc_prio;
		    }
		  }
		  expanded_output = (input%_output_speedup)*_outputs + output;
		  
		  if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		       ( _switch_hold_out[expanded_output] == -1 ) ) {
		    
		    Flit * f = cur_vc->FrontFlit();
		    assert(f);
		    if(f->watch) {
		      cout << "Speculative switch allocation requested at "
			   << _fullname << " at time " << GetSimTime() << endl
			   << "  Input: " << input << " VC: " << vc << endl
			   << "  Output: " << cur_vc->GetOutputPort() << endl
			   << *f;
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
						     in_priority + out_priority,
						     out_priority);
		  }
		}
	      }
	    }
	    break;
	  }
	}
	vc = ( vc + 1 ) % _vcs;
      }
    }
  }

  _sw_allocator->Allocate( );
  if ( _speculative == 2 )
    _spec_sw_allocator->Allocate( );
  
  // Promote virtual channel grants marked as speculative to active
  // now that the speculative switch request has been processed. Those
  // not marked active will not release flits speculatiely sent to the
  // switch to reflect the failure to secure buffering at the downstream
  // router
  for ( int input = 0 ; input < _inputs ; input++ ) {
    for ( int vc = 0 ; vc < _vcs ; vc++ ) {
      cur_vc = &_vc[input][vc] ;
      if ( cur_vc->GetState() == VC::vc_spec_grant ) {
	cur_vc->SetState( VC::active ) ;	
      } 
    }
  }

  // Winning flits cross the switch

  _crossbar_pipe->WriteAll( 0 );

  //////////////////////////////
  // Switch Power Modelling
  //  - Record Total Cycles
  //
  switchMonitor.cycle() ;

  for ( int input = 0; input < _inputs; ++input ) {
    c = 0;

    for ( int s = 0; s < _input_speedup; ++s ) {

      bool use_spec_grant = false;
      
      expanded_input  = s*_inputs + input;

      if ( _switch_hold_in[expanded_input] != -1 ) {
	assert(_switch_hold_in[expanded_input] >= 0);
	expanded_output = _switch_hold_in[expanded_input];
	vc = _switch_hold_vc[expanded_input];
	cur_vc = &_vc[input][vc];
	
	if ( cur_vc->Empty( ) ) { // Cancel held match if VC is empty
	  expanded_output = -1;
	}
      } else {
	expanded_output = _sw_allocator->OutputAssigned( expanded_input );
	if ( ( _speculative == 2 ) && ( expanded_output < 0 ) ) {
	  expanded_output = _spec_sw_allocator->OutputAssigned(expanded_input);
	  if ( expanded_output >= 0 ) {
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
	  vc = (use_spec_grant ?
		_spec_sw_allocator :
		_sw_allocator)->ReadRequest( expanded_input, expanded_output );
	  cur_vc = &_vc[input][vc];
	}

	// Detect speculative switch requests which succeeded when VC 
	// allocation failed and prevenet the switch from forwarding;
	// also, in case the routing function can return multiple outputs, 
	// check to make sure VC allocation and speculative switch allocation 
	// pick the same output port.
	if ( ( cur_vc->GetState() == VC::active ) &&
	     ( cur_vc->GetOutputPort() == output ) ) {

	  if ( _hold_switch_for_packet ) {
	    _switch_hold_in[expanded_input] = expanded_output;
	    _switch_hold_vc[expanded_input] = vc;
	    _switch_hold_out[expanded_output] = expanded_input;
	  }
	  
	  assert(cur_vc->GetState() == VC::active);
	  assert(!cur_vc->Empty());
	  assert(cur_vc->GetOutputPort() == output);
	  
	  dest_vc = &_next_vcs[output];
	  
	  if ( dest_vc->IsFullFor( cur_vc->GetOutputVC( ) ) )
	    continue ;
	  
	  // Forward flit to crossbar and send credit back
	  f = cur_vc->RemoveFlit( );
	  assert(f);
	  if(f->watch)
	    cout << "Granted switch allocation at " << _fullname 
		 << " at time " << GetSimTime() << endl
		 << "  Input: " << input << " VC: " << vc
		 << "  Output: " << output << endl
		 << *f;
	  
	  f->hops++;
	  
	  //
	  // Switch Power Modelling
	  //
	  switchMonitor.traversal( input, output, f) ;
	  bufferMonitor.read(input, f) ;
	  
	  if ( f->watch ) {
	    cout << "Forwarding flit through crossbar at " << _fullname 
		 << " at time " << GetSimTime() << endl
		 << "  Input: " << expanded_input 
		 << " Output: " << expanded_output << endl
		 << *f;
	  }
	  
	  if ( !c ) {
	    c = _NewCredit( _vcs );
	  }
	  
	  c->vc[c->vc_cnt] = f->vc;
	  c->vc_cnt++;
	  c->dest_router = f->from_router;
	  f->vc = cur_vc->GetOutputVC( );
	  dest_vc->SendingFlit( f );
	  
	  _crossbar_pipe->Write( f, expanded_output );
	  
	  if ( f->tail ) {
	    cur_vc->SetState( VC::idle );
	    _switch_hold_in[expanded_input]   = -1;
	    _switch_hold_vc[expanded_input]   = -1;
	    _switch_hold_out[expanded_output] = -1;
	  } else {
	    // reset state timer for next flit
	    cur_vc->SetState( VC::active );
	  }
	  
	  _sw_rr_offset[expanded_input] = ( f->vc + 1 ) % _vcs;
	} 
      }
    }
    _credit_pipe->Write( c, input );
  }
}
