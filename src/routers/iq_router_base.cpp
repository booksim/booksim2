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

#include "iq_router_base.hpp"

#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cassert>

#include "globals.hpp"
#include "random_utils.hpp"
#include "buffer.hpp"
#include "vc.hpp"
#include "outputset.hpp"
#include "buffer_state.hpp"
#include "buffer_monitor.hpp"
#include "switch_monitor.hpp"

IQRouterBase::IQRouterBase( const Configuration& config,
		    Module *parent, const string & name, int id,
		    int inputs, int outputs )
  : Router( config, parent, name, id, inputs, outputs )
{  
  _vcs         = config.GetInt( "num_vcs" );

  _routing_delay    = config.GetInt( "routing_delay" );
  _vc_alloc_delay   = config.GetInt( "vc_alloc_delay" );
  _sw_alloc_delay   = config.GetInt( "sw_alloc_delay" );
  
  // Routing
  string rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
  map<string, tRoutingFunction>::iterator rf_iter = gRoutingFunctionMap.find(rf);
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

  // Alloc pipelines (to simulate processing/transmission delays)
  _crossbar_pipe = 
    new PipelineFIFO<Flit>( this, "crossbar_pipeline", _outputs*_output_speedup, 
			    _crossbar_delay );

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

  int classes = config.GetInt("classes");
  _bufferMonitor = new BufferMonitor(inputs, classes);
  _switchMonitor = new SwitchMonitor(inputs, outputs, classes);
}

IQRouterBase::~IQRouterBase( )
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

  delete _crossbar_pipe;
  delete _credit_pipe;

  delete _bufferMonitor;
  delete _switchMonitor;
}
  
void IQRouterBase::ReadInputs( )
{
  _ReceiveFlits( );
  _ReceiveCredits( );
}

void IQRouterBase::_InternalStep( )
{
  _InputQueuing( );
  _Route( );
  _Alloc( );
  
  for ( int input = 0; input < _inputs; ++input ) {
    _buf[input]->AdvanceTime( );
  }

  _crossbar_pipe->Advance( );
  _credit_pipe->Advance( );


  _OutputQueuing( );
}

void IQRouterBase::WriteOutputs( )
{
  _SendFlits( );
  _SendCredits( );
}

void IQRouterBase::_ReceiveFlits( )
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
      int vc = f->vc;

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
      _queuing_vcs.push(make_pair(input, vc));
      _bufferMonitor->write( input, f ) ;
    }
  }
}

void IQRouterBase::_ReceiveCredits( )
{
  for ( int output = 0; output < _outputs; ++output ) {  
    Credit * c = _output_credits[output]->Receive();
    if ( c ) {
      _next_buf[output]->ProcessCredit( c );
      c->Free();
    }
  }
}

void IQRouterBase::_InputQueuing( )
{
  while(!_queuing_vcs.empty()) {

    pair<int, int> item = _queuing_vcs.front();
    _queuing_vcs.pop();
    int & input = item.first;
    int & vc = item.second;

    Buffer * cur_buf = _buf[input];

    assert(cur_buf->FrontFlit(vc));

    if ( cur_buf->GetState(vc) == VC::idle ) {
      
      cur_buf->SetState( vc, VC::routing );
      _routing_vcs.push(item);
    }

  } 
}

void IQRouterBase::_Route( )
{
  while(!_routing_vcs.empty()) {
    pair<int, int> item = _routing_vcs.front();
    int & input = item.first;
    int & vc = item.second;
    Buffer * cur_buf = _buf[input];
    if(cur_buf->GetStateTime(vc) < _routing_delay) {
      return;
    }
    _routing_vcs.pop();
    Flit * f = cur_buf->FrontFlit(vc);
    cur_buf->Route(vc, _rf, this, f,  input);
    cur_buf->SetState(vc, VC::vc_alloc) ;
    _vcalloc_vcs.insert(item);
  }
}

void IQRouterBase::_OutputQueuing( )
{

  for ( int output = 0; output < _outputs; ++output ) {
    for ( int t = 0; t < _output_speedup; ++t ) {
      int expanded_output = _outputs*t + output;
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

void IQRouterBase::_SendFlits( )
{
  for ( int output = 0; output < _outputs; ++output ) {
    if ( !_output_buffer[output].empty( ) ) {
      Flit *f = _output_buffer[output].front( );
      _output_buffer[output].pop( );
      ++_sent_flits[output];
      if(f->watch)
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		    << "Sending flit " << f->id
		    << " to channel at output " << output
		    << "." << endl;
      if(gTrace){cout<<"Outport "<<output<<endl;cout<<"Stop Mark"<<endl;}
      _output_channels[output]->Send( f );
    }
  }
}

void IQRouterBase::_SendCredits( )
{
  for ( int input = 0; input < _inputs; ++input ) {
    if ( !_in_cred_buffer[input].empty( ) ) {
      Credit * c = _in_cred_buffer[input].front( );
      _in_cred_buffer[input].pop( );
      _input_credits[input]->Send( c );
    }
  }
}



void IQRouterBase::Display( ) const
{
  for ( int input = 0; input < _inputs; ++input ) {
    _buf[input]->Display( );
  }
}

int IQRouterBase::GetCredit(int out, int vc_begin, int vc_end ) const
{
  if (out >= _outputs ) {
    cout << " ERROR  - big output  GetCredit : " << out << endl;
    exit(-1);
  }
  
  const BufferState * dest_buf = _next_buf[out];
  //dest_buf_tmp = &_next_buf_tmp[out];
  
  int start = (vc_begin >= 0) ? vc_begin : 0;
  int end = (vc_begin >= 0) ? vc_end : (_vcs - 1);

  int size = 0;
  for (int v = start; v <= end; v++)  {
    size+= dest_buf->Size(v);
  }
  return size;
}

int IQRouterBase::GetBuffer(int i) const {
  int size = 0;
  int i_start = (i >= 0) ? i : 0;
  int i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input) {
    for(int vc = 0; vc < _vcs; ++vc) {
      size += _buf[input]->GetSize(vc);
    }
  }
  return size;
}

int IQRouterBase::GetReceivedFlits(int i) const {
  int count = 0;
  int i_start = (i >= 0) ? i : 0;
  int i_end = (i >= 0) ? i : (_inputs - 1);
  for(int input = i_start; input <= i_end; ++input)
    count += _received_flits[input];
  return count;
}

int IQRouterBase::GetSentFlits(int o) const {
  int count = 0;
  int o_start = (o >= 0) ? o : 0;
  int o_end = (o >= 0) ? o : (_outputs - 1);
  for(int output = o_start; output <= o_end; ++output)
    count += _sent_flits[output];
  return count;
}

void IQRouterBase::ResetFlitStats() {
  for(int i = 0; i < _inputs; ++i)
    _received_flits[i] = 0;
  for(int o = 0; o < _outputs; ++o)
    _sent_flits[o] = 0;
}
