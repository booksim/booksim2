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
#include "iq_router_base.hpp"

IQRouterBase::IQRouterBase( const Configuration& config,
		    Module *parent, string name, int id,
		    int inputs, int outputs )
  : Router( config, parent, name, id, inputs, outputs ), 
    bufferMonitor(inputs), 
    switchMonitor(inputs, outputs) 
{
  ostringstream vc_name;
  
  _vcs         = config.GetInt( "num_vcs" );
  _vc_size     = config.GetInt( "vc_buf_size" );

  int partition_vcs = config.GetInt("partition_vcs") ;
  int rqb_vc = config.GetInt("read_request_begin_vc");
  int rqe_vc = config.GetInt("read_request_end_vc");    
  int rrb_vc = config.GetInt("read_reply_begin_vc");    
  int rre_vc = config.GetInt("read_reply_end_vc");      
  int wqb_vc = config.GetInt("write_request_begin_vc"); 
  int wqe_vc = config.GetInt("write_request_end_vc");   
  int wrb_vc = config.GetInt("write_reply_begin_vc");   
  int wre_vc = config.GetInt("write_reply_end_vc");     
  
  _routing_delay    = config.GetInt( "routing_delay" );
  _vc_alloc_delay   = config.GetInt( "vc_alloc_delay" );
  _sw_alloc_delay   = config.GetInt( "sw_alloc_delay" );
  
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

IQRouterBase::~IQRouterBase( )
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
  
void IQRouterBase::ReadInputs( )
{
  _ReceiveFlits( );
  _ReceiveCredits( );
}

void IQRouterBase::InternalStep( )
{
  _InputQueuing( );
  _Route( );
  _Alloc( );
  
  for ( int input = 0; input < _inputs; ++input ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {
      _vc[input][vc].AdvanceTime( );
    }
  }

  _crossbar_pipe->Advance( );
  _credit_pipe->Advance( );

  _OutputQueuing( );
}

void IQRouterBase::WriteOutputs( )
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

void IQRouterBase::_ReceiveFlits( )
{
  Flit *f;

  for ( int input = 0; input < _inputs; ++input ) { 
    f = (*_input_channels)[input]->ReceiveFlit();

    if ( f ) {
      _input_buffer[input].push( f );
    }
  }
}

void IQRouterBase::_ReceiveCredits( )
{
  Credit *c;

  for ( int output = 0; output < _outputs; ++output ) {  
    c = (*_output_credits)[output]->ReceiveCredit();

    if ( c ) {
      _out_cred_buffer[output].push( c );
    }
  }
}

void IQRouterBase::_InputQueuing( )
{
  bufferMonitor.cycle() ;

  for ( int input = 0; input < _inputs; ++input ) {
    if ( !_input_buffer[input].empty( ) ) {
      Flit * f = _input_buffer[input].front( );
      _input_buffer[input].pop( );
      
      VC * cur_vc = &_vc[input][f->vc];
      
      if(f->watch) {
	cout << GetSimTime() << " | " << _fullname << " | "
	     << "VC " << f->vc << " at input " << input 
	     << " received flit " << f->id << " from channel (state: "
	     << VC::VCSTATE[cur_vc->GetState()] << ", empty: "
	     << cur_vc->Empty();
	if(cur_vc->FrontFlit())
	  cout << ", front: " << cur_vc->FrontFlit()->id;
	cout << ")." << endl;
      }
      
      if ( !cur_vc->AddFlit( f ) ) {
	Error( "VC buffer overflow" );
      }
      bufferMonitor.write( input, f ) ;
      
    }
  }
  
  for ( int input = 0; input < _inputs; ++input ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {

      VC * cur_vc = &_vc[input][vc];
      
      if ( cur_vc->GetState( ) == VC::idle ) {
	Flit * f = cur_vc->FrontFlit( );

	if ( f ) {
	  if ( !f->head ) {
	    Error( "Received non-head flit at idle VC" );
	  }

	  cur_vc->SetState( VC::routing );
	}
      }
    }
  }  

  for ( int output = 0; output < _outputs; ++output ) {
    if ( !_out_cred_buffer[output].empty( ) ) {
      Credit * c = _out_cred_buffer[output].front( );
      _out_cred_buffer[output].pop( );
   
      _next_vcs[output].ProcessCredit( c );
      delete c;
    }
  }
}

void IQRouterBase::_Route( )
{
  for ( int input = 0; input < _inputs; ++input ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {

      VC * cur_vc = &_vc[input][vc];

      if ( ( cur_vc->GetState( ) == VC::routing ) &&
	   ( cur_vc->GetStateTime( ) >= _routing_delay ) ) {
	
	Flit * f = cur_vc->FrontFlit( );
	cur_vc->Route( _rf, this, f, input );
	cur_vc->SetState( VC::vc_alloc ) ;
	
      }
    }
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
	  cout << GetSimTime() << " | " << _fullname << " | "
	       << "VC " << f->vc << " at output " << output 
	       << " sent flit " << f->id << " to channel." << endl;
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

void IQRouterBase::_SendCredits( )
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

void IQRouterBase::Display( ) const
{
  for ( int input = 0; input < _inputs; ++input ) {
    for ( int v = 0; v < _vcs; ++v ) {
      _vc[input][v].Display( );
    }
  }
}

int IQRouterBase::GetCredit(int out, int vc_begin, int vc_end ) const
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

int IQRouterBase::GetBuffer(int i) const{
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

