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
#include "pipefifo.hpp"
#include "allocator.hpp"
#include "switch_monitor.hpp"
#include "buffer_monitor.hpp"

IQRouter::IQRouter( const Configuration& config, Module *parent, 
		    const string & name, int id, int inputs, int outputs )
  : Router( config, parent, name, id, inputs, outputs )
{
  _vcs         = config.GetInt( "num_vcs" );
  _speculative = config.GetInt( "speculative" ) ;
  
  _routing_delay    = config.GetInt( "routing_delay" );
  _vc_alloc_delay   = config.GetInt( "vc_alloc_delay" );
  _sw_alloc_delay   = config.GetInt( "sw_alloc_delay" );
  
  // Routing
  _rf = GetRoutingFunction( config );

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
  
  // Alloc pipelines (to simulate processing/transmission delays)
  _crossbar_pipe = 
    new PipelineFIFO<Flit>( this, "crossbar_pipeline", _outputs*_output_speedup, 
			    _st_prepare_delay + _st_final_delay );

  _credit_pipe =
    new PipelineFIFO<Credit>( this, "credit_pipeline", _inputs,
			      _credit_delay );

  // Input and output queues
  //_input_buffer.resize(_inputs); 
  _output_buffer.resize(_outputs); 

  _in_cred_buffer.resize(_inputs); 
  //_out_cred_buffer.resize(_outputs);

  // Switch configuration (when held for multiple cycles)
  _hold_switch_for_packet = config.GetInt( "hold_switch_for_packet" );
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

  if(_print_activity){
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
  if ( _speculative >= 2 )
    delete _spec_sw_allocator;

  delete _crossbar_pipe;
  delete _credit_pipe;

  delete _bufferMonitor;
  delete _switchMonitor;
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
    _buf[input]->AdvanceTime( );
  }

  _crossbar_pipe->Advance( );
  _credit_pipe->Advance( );


  _OutputQueuing( );
}

void IQRouter::WriteOutputs( )
{
  _SendFlits( );
  _SendCredits( );
}

void IQRouter::_ReceiveFlits( )
{
  _bufferMonitor->cycle() ;
  for ( int input = 0; input < _inputs; ++input ) { 
    Flit * f = _input_channels[input]->Receive();
    if ( f ) {

      ++_received_flits[input];
      if ( f->watch ) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Received flit " << f->id
		   << " from channel at input " << input
		   << "." << endl;
      }

      Buffer * cur_buf = _buf[input];
      const int vc = f->vc;

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
      _queuing_vcs.push_back(make_pair(input, vc));
      _bufferMonitor->write( input, f ) ;
    }
  }
}

void IQRouter::_ReceiveCredits( )
{
  for(int output = 0; output < _outputs; ++output) {  
    Credit * c = _output_credits[output]->Receive();
    if(c) {
      _next_buf[output]->ProcessCredit(c);
      _RetireCredit(c);
    }
  }
}

void IQRouter::_InputQueuing( )
{
  while(!_queuing_vcs.empty()) {
    
    const pair<int, int> & item = _queuing_vcs.front();
    const int & input = item.first;
    const int & vc = item.second;

    Buffer * cur_buf = _buf[input];

    assert(cur_buf->FrontFlit(vc));

    if (cur_buf->GetState(vc) == VC::idle) {
      
      cur_buf->SetState(vc, VC::routing);
      _route_waiting_vcs.push(make_pair(GetSimTime() + _routing_delay, item));
    }

    _queuing_vcs.pop_front();
  }
}

void IQRouter::_Route( )
{
  while(!_route_waiting_vcs.empty()) {
    pair<int, pair<int, int> > & item = _route_waiting_vcs.front();
    int & time = item.first;
    if(GetSimTime() < time) {
      return;
    }
    const int & input = item.second.first;
    const int & vc = item.second.second;
    Buffer * cur_buf = _buf[input];
    Flit * f = cur_buf->FrontFlit(vc);
    cur_buf->Route(vc, _rf, this, f, input);
    time += _vc_alloc_delay;
    _vc_alloc_waiting_vcs.push(item);
    if(_speculative == 0) {
      cur_buf->SetState(vc, VC::vc_alloc);
    } else {
      cur_buf->SetState(vc, VC::vc_spec) ;
    }
    _route_waiting_vcs.pop();
  }
}

void IQRouter::_VCAlloc( )
{
  while(!_vc_alloc_waiting_vcs.empty()) {
    const pair<int, pair<int, int> > & item = _vc_alloc_waiting_vcs.front();
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
    
    const int & input = iter->first;
    const int & vc = iter->second;
    Buffer * cur_buf = _buf[input];
    Flit * f = cur_buf->FrontFlit(vc);
    if(f->watch) {
      *gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		 << "VC " << vc << " at input " << input
		 << " is requesting VC allocation for flit " << f->id
		 << "." << endl;
      watched = true;
    }
    
    const OutputSet *route_set = cur_buf->GetRouteSet(vc);
    int out_priority = cur_buf->GetPriority(vc);
    const set<OutputSet::sSetElement> setlist = route_set ->GetSet();
    set<OutputSet::sSetElement>::const_iterator iset = setlist.begin();
    while(iset != setlist.end()){
      BufferState *dest_buf = _next_buf[iset->output_port];
      for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc) {
	const int in_priority = iset->pri;
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
		       << " at output " << iset->output_port 
		       << " with priorities " << in_priority
		       << " and " << out_priority
		       << "." << endl;
	  }
	  _vc_allocator->AddRequest(input * _vcs + vc, 
				    iset->output_port * _vcs + out_vc, out_vc, 
				    in_priority, out_priority);
	} else {
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "VC " << out_vc << " at output " << iset->output_port 
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

  // Winning flits get a VC

  iter = _vc_alloc_pending_vcs.begin();
  while(iter != _vc_alloc_pending_vcs.end()) {
    const int & input = iter->first;
    const int & vc = iter->second;
    
    const int output_and_vc = _vc_allocator->OutputAssigned(input * _vcs + vc);
    
    if(output_and_vc < 0) {

      // no match -- keep request and retry in next round

      ++iter;

    } else {
      
      // match -- update state and remove request from pending list

      Buffer * cur_buf = _buf[input];
      if(_speculative == 0) {
	cur_buf->SetState(vc, VC::active);
      } else {
	cur_buf->SetState(vc, VC::vc_spec_grant);
      }
      
      const int match_output = output_and_vc / _vcs;
      const int match_vc = output_and_vc % _vcs;
      cur_buf->SetOutput(vc, match_output, match_vc);

      BufferState * dest_buf = _next_buf[match_output];
      dest_buf->TakeBuffer(match_vc);
      
      const Flit * f = cur_buf->FrontFlit(vc);
      assert(f);
      if(f->watch)
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Granted VC " << match_vc << " at output " << match_output
		   << " to VC " << vc << " at input " << input
		   << " (flit: " << f->id << ")." << endl;
      
      iter = _vc_alloc_pending_vcs.erase(iter);
    }
  }
}

void IQRouter::_SWAlloc( )
{
  bool watched = false;

  bool any_nonspec_reqs = false;
  vector<bool> any_nonspec_output_reqs(_outputs*_output_speedup, 0);
  
  _sw_allocator->Clear();
  if (_speculative > 1)
    _spec_sw_allocator->Clear();
  
  for ( int input = 0; input < _inputs; ++input ) {
    int vc_ready_nonspec = 0;
    int vc_ready_spec = 0;
    for ( int s = 0; s < _input_speedup; ++s ) {
      const int expanded_input  = input * _input_speedup + s;
      
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
	  
	  if(cur_buf->GetState(vc) == VC::active) {
	    
	    const int output = cur_buf->GetOutputPort(vc);
	    
	    BufferState * dest_buf = _next_buf[output];
	    
	    if ( !dest_buf->IsFullFor( cur_buf->GetOutputVC(vc) ) ) {
	      
	      // When input_speedup > 1, the virtual channel buffers are 
	      // interleaved to create multiple input ports to the switch. 
	      // Similarily, the output ports are interleaved based on their 
	      // originating input when output_speedup > 1.
	      
	      assert( expanded_input == input *_input_speedup + vc % _input_speedup );
	      const int expanded_output = 
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
		const int expanded_output = 
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
		const int expanded_output = iset->output_port * _output_speedup + input % _output_speedup;
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
      
      const int expanded_input  = input * _input_speedup + s;
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
	const int output = expanded_output / _output_speedup;

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
	    cur_buf->SetState(vc, VC::idle);
	    if(!cur_buf->Empty(vc)) {
	      _queuing_vcs.push_back(make_pair(input, vc));
	    }
	    _switch_hold_in[expanded_input]   = -1;
	    _switch_hold_vc[expanded_input]   = -1;
	    _switch_hold_out[expanded_output] = -1;
	  }
	  
	  const int next_offset = vc + _input_speedup;
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

void IQRouter::_OutputQueuing( )
{

  for ( int output = 0; output < _outputs; ++output ) {
    for ( int t = 0; t < _output_speedup; ++t ) {
      const int expanded_output = _outputs*t + output;
      Flit * f = _crossbar_pipe->Read( expanded_output );

      if ( f ) {
	_output_buffer[output].push( f );
	if(f->watch)
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		      << "Buffering flit " << f->id
		      << " at output " << output
		      << "." << endl;
      }
    }
  }  

  for ( int input = 0; input < _inputs; ++input ) {
    Credit * c = _credit_pipe->Read( input );

    if ( c ) {
      _in_cred_buffer[input].push( c );
    }
  }
}

void IQRouter::_SendFlits( )
{
  for ( int output = 0; output < _outputs; ++output ) {
    Flit *f = NULL;
    if ( !_output_buffer[output].empty( ) ) {
      f = _output_buffer[output].front( );
      f->from_router = this->GetID();
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
    if ( !_in_cred_buffer[input].empty( ) ) {
      c = _in_cred_buffer[input].front( );
      _in_cred_buffer[input].pop( );
    }
    _input_credits[input]->Send( c );
  }
}

void IQRouter::Display( ) const
{
  for ( int input = 0; input < _inputs; ++input ) {
    _buf[input]->Display( );
  }
}

int IQRouter::GetCredit(int out, int vc_begin, int vc_end ) const
{
  if (out >= _outputs ) {
    cout << " ERROR  - big output  GetCredit : " << out << endl;
    exit(-1);
  }
  
  const BufferState * dest_buf = _next_buf[out];
  
  const int start = (vc_begin >= 0) ? vc_begin : 0;
  const int end = (vc_begin >= 0) ? vc_end : (_vcs - 1);

  int size = 0;
  for (int v = start; v <= end; v++)  {
    size+= dest_buf->Size(v);
  }
  return size;
}

int IQRouter::GetBuffer(int i) const {
  int size = 0;
  const int i_start = (i >= 0) ? i : 0;
  const int i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input) {
    for(int vc = 0; vc < _vcs; ++vc) {
      size += _buf[input]->GetSize(vc);
    }
  }
  return size;
}

int IQRouter::GetReceivedFlits(int i) const {
  int count = 0;
  const int i_start = (i >= 0) ? i : 0;
  const int i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input)
    count += _received_flits[input];
  return count;
}

int IQRouter::GetSentFlits(int o) const {
  int count = 0;
  const int o_start = (o >= 0) ? o : 0;
  const int o_end = (o >= 0) ? o : (_outputs - 1);
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
