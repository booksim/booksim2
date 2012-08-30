// $Id: iq_router.cpp 1137 2009-03-01 23:24:43Z mebauer $

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

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <assert.h>

#include "random_utils.hpp"
#include "iq_router.hpp"

IQRouter::IQRouter( const Configuration& config,
		    Module *parent, string name, int id,
		    int inputs, int outputs )
  : Router( config,
	    parent, name,
	    id,
	    inputs, outputs ), 
    bufferMonitor(inputs), 
    switchMonitor(inputs, outputs) 
{
  string alloc_type;
  string arb_type;
  int iters;
  ostringstream vc_name;
  
  _vcs         = config.GetInt( "num_vcs" );
  _vc_size     = config.GetInt( "vc_buf_size" );
  _speculative = config.GetInt( "speculative" ) ;

  int partition_vcs = config.GetInt("partition_vcs") ;
  int rqb_vc = config.GetInt("read_request_begin_vc");
  int rqe_vc = config.GetInt("read_request_end_vc");    
  int rrb_vc = config.GetInt("read_reply_begin_vc");    
  int rre_vc = config.GetInt("read_reply_end_vc");      
  int wqb_vc = config.GetInt("write_request_begin_vc"); 
  int wqe_vc = config.GetInt("write_request_end_vc");   
  int wrb_vc = config.GetInt("write_reply_begin_vc");   
  int wre_vc = config.GetInt("write_reply_end_vc");     

  // Routing
  _rf = GetRoutingFunction( config );

  // Alloc VC's
  _vc = new VC * [_inputs];

  for ( int i = 0; i < _inputs; ++i ) {
    _vc[i] = new VC [_vcs];
    for (int j = 0; j < _vcs; ++j )
      _vc[i][j]._Init(config,_outputs);

    for ( int v = 0; v < _vcs; ++v ) { // Name the vc modules
      vc_name << "vc_i" << i << "_v" << v;
      _vc[i][v].SetName( this, vc_name.str( ) );
      vc_name.seekp( 0, ios::beg );
    }
  }

  // Alloc next VCs' buffer state
  _next_vcs = new BufferState [_outputs];
  for (int j = 0; j < _outputs; ++j) 
    _next_vcs[j]._Init( config );

  for ( int o = 0; o < _outputs; ++o ) {
    vc_name << "next_vc_o" << o;
    _next_vcs[o].SetName( this, vc_name.str( ) );
    vc_name.seekp( 0, ios::beg );
  }

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

  // Alloc pipelines (to simulate processing/transmission delays)
  _crossbar_pipe = 
    new PipelineFIFO<Flit>( this, "crossbar_pipeline", _outputs*_output_speedup, 
			    _st_prepare_delay + _st_final_delay );

  _credit_pipe =
    new PipelineFIFO<Credit>( this, "credit_pipeline", _inputs,
			      _credit_delay );

  // Input and output queues
  _input_buffer  = new queue<Flit *> [_inputs]; 
  _output_buffer = new queue<Flit *> [_outputs]; 

  _in_cred_buffer  = new queue<Credit *> [_inputs]; 
  _out_cred_buffer = new queue<Credit *> [_outputs];

  // Switch configuration (when held for multiple cycles)
  _hold_switch_for_packet = config.GetInt( "hold_switch_for_packet" );
  _switch_hold_in  = new int [_inputs*_input_speedup];
  _switch_hold_out = new int [_outputs*_output_speedup];
  _switch_hold_vc  = new int [_inputs*_input_speedup];

  for ( int i = 0; i < _inputs*_input_speedup; ++i ) {
    _switch_hold_in[i] = -1;
    _switch_hold_vc[i] = -1;
  }

  for ( int i = 0; i < _outputs*_output_speedup; ++i ) {
    _switch_hold_out[i] = -1;
  }


}

IQRouter::~IQRouter( )
{
  if(_print_activity){
    cout << _name << ".bufferMonitor:" << endl ; 
    cout << bufferMonitor << endl ;
    
    cout << _name << ".switchMonitor:" << endl ; 
    cout << "Inputs=" << _inputs ;
    cout << "Outputs=" << _outputs ;
    cout << switchMonitor << endl ;
  }

  for ( int i = 0; i < _inputs; ++i ) {
    delete [] _vc[i];
  }

  delete [] _vc;
  delete [] _next_vcs;

  delete _vc_allocator;
  delete _sw_allocator;

  if ( _speculative == 2 )
    delete _spec_sw_allocator;

  delete [] _sw_rr_offset;

  delete _crossbar_pipe;
  delete _credit_pipe;

  delete [] _input_buffer;
  delete [] _output_buffer;

  delete [] _in_cred_buffer;
  delete [] _out_cred_buffer;

  delete [] _switch_hold_in;
  delete [] _switch_hold_vc;
  delete [] _switch_hold_out;
}
  
void IQRouter::ReadInputs( )
{
  _ReceiveFlits( );
  _ReceiveCredits( );


}

void IQRouter::InternalStep( )
{
  _InputQueuing( );
  _Route( );
  _VCAlloc( );
  _SWAlloc( );

  for ( int input = 0; input < _inputs; ++input ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {
      _vc[input][vc].AdvanceTime( );
    }
  }

  _crossbar_pipe->Advance( );
  _credit_pipe->Advance( );

  _OutputQueuing( );
}

void IQRouter::WriteOutputs( )
{
  _SendFlits( );
  _SendCredits( );
  if(_trace){
    int load = 0;
    cout<<"Router "<<this->GetID()<<endl;
    //need to modify router to report the buffere dept
    //cout<<"Input Channel "<<in_channel<<endl;
    //load +=r->GetBuffer(in_channel);
    cout<<"Rload "<<load<<endl;
  }
}

void IQRouter::_ReceiveFlits( )
{
  Flit *f;

  bufferMonitor.cycle() ;

  for ( int input = 0; input < _inputs; ++input ) {
    f = (*_input_channels)[input]->ReceiveFlit();
    if ( f ) {
      _input_buffer[input].push( f );
      bufferMonitor.write( input, f ) ;
    }
  }
}

void IQRouter::_ReceiveCredits( )
{
  Credit *c;

  for ( int output = 0; output < _outputs; ++output ) {  
    c = (*_output_credits)[output]->ReceiveCredit();
    if ( c ) {
      _out_cred_buffer[output].push( c );
    }
  }
}

void IQRouter::_InputQueuing( )
{
  Flit   *f;
  Credit *c;
  VC     *cur_vc;

  for ( int input = 0; input < _inputs; ++input ) {
    if ( !_input_buffer[input].empty( ) ) {
      f = _input_buffer[input].front( );
      _input_buffer[input].pop( );

      cur_vc = &_vc[input][f->vc];

      if ( !cur_vc->AddFlit( f ) ) {
	Error( "VC buffer overflow" );
      }

      if ( f->watch ) {
	cout << "Received flit at " << _fullname << endl;
	cout << *f;
      }
    }
  }
      
  for ( int input = 0; input < _inputs; ++input ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {

      cur_vc = &_vc[input][vc];
      
      if ( cur_vc->GetState( ) == VC::idle ) {
	f = cur_vc->FrontFlit( );

	if ( f ) {
	  if ( !f->head ) {
	    Error( "Received non-head flit at idle VC" );
	  }

	  cur_vc->Route( _rf, this, f, input );
	  cur_vc->SetState( VC::routing );
	}
      }
    }
  }  

  for ( int output = 0; output < _outputs; ++output ) {
    if ( !_out_cred_buffer[output].empty( ) ) {
      c = _out_cred_buffer[output].front( );
      _out_cred_buffer[output].pop( );
   
      _next_vcs[output].ProcessCredit( c );
      delete c;
    }
  }
}

void IQRouter::_Route( )
{
  VC *cur_vc;

  for ( int input = 0; input < _inputs; ++input ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {

      cur_vc = &_vc[input][vc];

      if ( ( cur_vc->GetState( ) == VC::routing ) &&
	   ( cur_vc->GetStateTime( ) >= _routing_delay ) ) {
	
	if ( _speculative > 0 )
	  cur_vc->SetState( VC::vc_spec ) ;
	else
	  cur_vc->SetState( VC::vc_alloc ) ;

      }
    }
  }
}

void IQRouter::_AddVCRequests( VC* cur_vc, int input_index, bool watch )
{
  const OutputSet *route_set;
  BufferState *dest_vc;
  int vc_cnt, out_vc;
  int in_priority, out_priority;

  route_set    = cur_vc->GetRouteSet( );
  out_priority = cur_vc->GetPriority( );
  in_priority = out_priority;

  for ( int output = 0; output < _outputs; ++output ) {
    vc_cnt = route_set->NumVCs( output );
    dest_vc = &_next_vcs[output];

    for ( int vc_index = 0; vc_index < vc_cnt; ++vc_index ) {
      out_vc = route_set->GetVC( output, vc_index, &in_priority );

      if ( watch ) {
	cout << "  trying vc " << out_vc << " (out = " << output << ") ... ";
      }
     
      // On the input input side, a VC might request several output 
      // VCs.  These VCs can be prioritized by the routing function
      // and this is reflected in "in_priority".  On the output,
      // if multiple VCs are requesting the same output VC, the priority
      // of VCs is based on the actual packet priorities, which is
      // reflected in "out_priority".

      if ( dest_vc->IsAvailableFor( out_vc ) ) {
	_vc_allocator->AddRequest( input_index, output*_vcs + out_vc, 1, 
				   in_priority, out_priority );
	if ( watch ) {
	  cout << "available" << endl;
	}
      } else if ( watch ) {
	cout << "busy" << endl;
      }
    }
  }
}

void IQRouter::_VCAlloc( )
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

      if ( ( ( cur_vc->GetState( ) == VC::vc_alloc ) ||
	     ( cur_vc->GetState( ) == VC::vc_spec ) ) &&
	   ( cur_vc->GetStateTime( ) >= _vc_alloc_delay ) ) {
	
  	f = cur_vc->FrontFlit( );
	if ( f->watch ) {
	  cout << "VC requesting allocation at " << _fullname << endl;
	  cout << "  input: = " << input 
	       << "  vc: " << vc << endl;
	  cout << *f;
	  watched = true;
	}
	
	_AddVCRequests( cur_vc, input*_vcs + vc, f->watch );
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
	       << " (input index " << input_and_vc << " )" << endl;
	  cout << *f;
	}
      }
    }
  }
}

void IQRouter::_SWAlloc( )
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

	if ((cur_vc->GetState( ) == VC::active) && !cur_vc->Empty() ) {
	  
	  dest_vc = &_next_vcs[cur_vc->GetOutputPort( )];
	  
	  if ( !dest_vc->IsFullFor( cur_vc->GetOutputVC( ) ) ) {
	    
	    // When input_speedup > 1, the virtual channel buffers
	    // are interleaved to create multiple input ports to
	    // the switch.  Similarily, the output ports are
	    // interleaved based on their originating input when
	    // output_speedup > 1.

	    assert( expanded_input == (vc%_input_speedup)*_inputs + input );
	    expanded_output = (input%_output_speedup)*_outputs + cur_vc->GetOutputPort( );
	    
	    if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		 ( _switch_hold_out[expanded_output] == -1 ) ) {
	      
	      // We could have requested this same input-output pair in a 
	      // previous iteration; only replace the previous request if the 
	      // current request has a higher priority (this is default behavior
	      // of the allocators).  Switch allocation priorities are strictly 
	      // determined by the packet priorities.
	      
	      if( _speculative == 1 )
		_sw_allocator->AddRequest( expanded_input, expanded_output, vc, 
					   1, 
					   1);
	      else
		_sw_allocator->AddRequest( expanded_input, expanded_output, vc, 
					   cur_vc->GetPriority( ), 
					   cur_vc->GetPriority( ));
	      any_nonspec_reqs = true;
	      any_nonspec_output_reqs[expanded_output] = true;
	      
	    }
	  }
	}
	

	//
	// The following models the speculative VC allocation aspects 
	// of the pipeline. An input VC with a request in for an egress
	// virtual channel will also speculatively bid for the switch
	// regardless of whether the VC allocation succeeds. These
	// speculative requests are handled in a separate allocator so 
	// as to prevent them from interfering with non-speculative bids
	//
	bool enter_spec_sw_req = !cur_vc->Empty() &&
	  ((cur_vc->GetState() == VC::vc_spec) ||
	   (cur_vc->GetState() == VC::vc_spec_grant)) ;

	if ( enter_spec_sw_req ) {
	 
	  assert( _speculative > 0 );
	  assert( expanded_input == (vc%_input_speedup)*_inputs + input );
	  expanded_output = (input%_output_speedup)*_outputs + cur_vc->GetOutputPort( );
	  
	  if ( ( _switch_hold_in[expanded_input] == -1 ) && 
	       ( _switch_hold_out[expanded_output] == -1 ) ) {
	    
	    // Speculative requests are sent to the allocator with a priority
	    // of 0 regardless of whether there is buffer space available
	    // at the downstream router because request is speculative. 
	    if( _speculative == 1 )
	      _sw_allocator->AddRequest( expanded_input, expanded_output, vc,
					 0, 
					 0);
	    else
	      _spec_sw_allocator->AddRequest( expanded_input, expanded_output, 
					      vc,
					      cur_vc->GetPriority( ), 
					      cur_vc->GetPriority( ));
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
	// allocation failed and prevenet the switch from forwarding
	if ( cur_vc->GetState() == VC::active ) {


	  if ( _hold_switch_for_packet ) {
	    _switch_hold_in[expanded_input] = expanded_output;
	    _switch_hold_vc[expanded_input] = vc;
	    _switch_hold_out[expanded_output] = expanded_input;
	  }
	  
	  assert( ( cur_vc->GetState( ) == VC::active ) && 
		  ( !cur_vc->Empty( ) ) && 
		  ( cur_vc->GetOutputPort( ) == ( expanded_output % _outputs ) ) );
	  
	  dest_vc = &_next_vcs[cur_vc->GetOutputPort( )];
	  
	  if ( dest_vc->IsFullFor( cur_vc->GetOutputVC( ) ) )
	    continue ;
	  
	  // Forward flit to crossbar and send credit back
	  f = cur_vc->RemoveFlit( );
	  
	  f->hops++;
	  
	  //
	  // Switch Power Modelling
	  //
	  switchMonitor.traversal( input, output, f) ;
	  bufferMonitor.read(input, f) ;
	  
	  if ( f->watch ) {
	    cout << "Forwarding flit through crossbar at " << _fullname << ":" << endl;
	    cout << *f;
	    cout << "  input: " << expanded_input 
		 << "  output: " << expanded_output << endl ;
	  }
	  
	  if ( !c ) {
	    c = _NewCredit( _vcs );
	  }
	  assert(c->vc_cnt<1);
	  c->vc = f->vc;
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
	  }
	  
	  _sw_rr_offset[expanded_input] = ( f->vc + 1 ) % _vcs;
	} 
      }
    }
    _credit_pipe->Write( c, input );
  }
}

void IQRouter::_OutputQueuing( )
{
  Flit   *f;
  Credit *c;
  int expanded_output;

  for ( int output = 0; output < _outputs; ++output ) {
    for ( int t = 0; t < _output_speedup; ++t ) {
      expanded_output = _outputs*t + output;
      f = _crossbar_pipe->Read( expanded_output );

      if ( f ) {
	_output_buffer[output].push( f );
      }
    }
  }  

  for ( int input = 0; input < _inputs; ++input ) {
    c = _credit_pipe->Read( input );

    if ( c ) {
      _in_cred_buffer[input].push( c );
    }
  }
}

void IQRouter::_SendFlits( )
{
  Flit *f;

  for ( int output = 0; output < _outputs; ++output ) {
    if ( !_output_buffer[output].empty( ) ) {
      f = _output_buffer[output].front( );
      f->from_router = this->GetID();
      _output_buffer[output].pop( );
    } else {
      f = 0;
    }
    if(_trace && f){cout<<"Outport "<<output<<endl;cout<<"Stop Mark"<<endl;}
    (*_output_channels)[output]->SendFlit( f );
  }
}

void IQRouter::_SendCredits( )
{
  Credit *c;

  for ( int input = 0; input < _inputs; ++input ) {
    if ( !_in_cred_buffer[input].empty( ) ) {
      c = _in_cred_buffer[input].front( );
      _in_cred_buffer[input].pop( );
    } else {
      c = 0;
    }
    (*_input_credits)[input]->SendCredit( c );
  }
}

void IQRouter::Display( ) const
{
  for ( int input = 0; input < _inputs; ++input ) {
    for ( int v = 0; v < _vcs; ++v ) {
      _vc[input][v].Display( );
    }
  }
}

int IQRouter::GetCredit(int out, int vc_begin, int vc_end ) const
{
 

  BufferState *dest_vc;
  int    tmpsum = 0;
  int    vc_cnt = vc_end - vc_begin + 1;
  int cnt = 0;
  
  if (out >= _outputs ) {
    cout << " ERROR  - big output  GetCredit : " << out << endl;
    exit(-1);
  }
  
  dest_vc = &_next_vcs[out];
  //dest_vc_tmp = &_next_vcs_tmp[out];
  
  if (vc_begin == -1) {
    for (int v =0;v<_vcs;v++){
      tmpsum+= dest_vc->Size(v);
    }
    return tmpsum;
  }  else if (vc_begin != -1) {
    assert(vc_begin >= 0);
    for (int v =vc_begin;v<= vc_end ;v++)  {
      tmpsum+= dest_vc->Size(v);
      cnt++;
    }
    return tmpsum;
  }
  assert(0); // Should never reach here.
  return -5;
}

int IQRouter::GetBuffer(int i) const{
  int size = 0;
  VC *cur_vc;
  for(int j=0; j<_vcs; j++){
    cur_vc = &_vc[i][j];
    size += cur_vc->GetSize();
  }
  return size;
}


// ----------------------------------------------------------------------
//
//   Switch Monitor
//
// ----------------------------------------------------------------------
SwitchMonitor::SwitchMonitor( int inputs, int outputs ) {
  // something is stomping on the arrays, so padding is applied
  const int Offset = 16 ;
  _cycles  = 0 ;
  _inputs  = inputs ;
  _outputs = outputs ;
  const int n = 2 * Offset + (inputs+1) * (outputs+1) * Flit::NUM_FLIT_TYPES ;
  _event = new int [ n ] ;
  for ( int i = 0 ; i < n ; i++ ) {
	_event[i] = 0 ;
  }
  _event += Offset ;
}

int SwitchMonitor::index( int input, int output, int flitType ) const {
  return flitType + Flit::NUM_FLIT_TYPES * ( output + _outputs * input ) ;
}

void SwitchMonitor::cycle() {
  _cycles++ ;
}

void SwitchMonitor::traversal( int input, int output, Flit* flit ) {
  _event[ index( input, output, flit->type) ]++ ;
}

ostream& operator<<( ostream& os, const SwitchMonitor& obj ) {
  for ( int i = 0 ; i < obj._inputs ; i++ ) {
    for ( int o = 0 ; o < obj._outputs ; o++) {
      os << "[" << i << " -> " << o << "] " ;
      for ( int f = 0 ; f < Flit::NUM_FLIT_TYPES ; f++ ) {
	os << f << ":" << obj._event[ obj.index(i,o,f)] << " " ;
      }
      os << endl ;
    }
  }
  return os ;
}

// ----------------------------------------------------------------------
//
//   Flit Buffer Monitor
//
// ----------------------------------------------------------------------
BufferMonitor::BufferMonitor( int inputs ) {
  // something is stomping on the arrays, so padding is applied
  const int Offset = 16 ;
  _cycles = 0 ;
  _inputs = inputs ;

  const int n = 2*Offset + 4 * inputs  * Flit::NUM_FLIT_TYPES ;
  _reads  = new int [ n ] ;
  _writes = new int [ n ] ;
  for ( int i = 0 ; i < n ; i++ ) {
    _reads[i]  = 0 ; 
    _writes[i] = 0 ;
  }
  _reads += Offset ;
  _writes += Offset ;
}

int BufferMonitor::index( int input, int flitType ) const {
  if ( input < 0 || input > _inputs ) 
    cerr << "ERROR: input out of range in BufferMonitor" << endl ;
  if ( flitType < 0 || flitType> Flit::NUM_FLIT_TYPES ) 
    cerr << "ERROR: flitType out of range in flitType" << endl ;
  return flitType + Flit::NUM_FLIT_TYPES * input ;
}

void BufferMonitor::cycle() {
  _cycles++ ;
}

void BufferMonitor::write( int input, Flit* flit ) {
  _writes[ index(input, flit->type) ]++ ;
}

void BufferMonitor::read( int input, Flit* flit ) {
  _reads[ index(input, flit->type) ]++ ;
}

ostream& operator<<( ostream& os, const BufferMonitor& obj ) {
  for ( int i = 0 ; i < obj._inputs ; i++ ) {
    os << "[ " << i << " ] " ;
    for ( int f = 0 ; f < Flit::NUM_FLIT_TYPES ; f++ ) {
      os << "Type=" << f
	 << ":(R#" << obj._reads[ obj.index( i, f) ]  << ","
	 << "W#" << obj._writes[ obj.index( i, f) ] << ")" << " " ;
    }
    os << endl ;
  }
  return os ;
}

