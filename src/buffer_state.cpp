// $Id: buffer_state.cpp 2388 2010-08-05 21:36:41Z qtedq $

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

/*buffer_state.cpp
 *
 * This class is the buffere state of the next router down the channel
 * tracks the credit and how much of the buffer is in use 
 */

#include "booksim.hpp"
#include <iostream>
#include <stdlib.h>
#include <assert.h>

#include "buffer_state.hpp"
#include "random_utils.hpp"

vector<int> BufferState::_vc_range_begin(0);
vector<int> BufferState::_vc_range_size(0);

BufferState::BufferState( const Configuration& config ) :
  Module( )
{
  _Init( config );
}

BufferState::BufferState( const Configuration& config, 
			  Module *parent, const string& name ) : 
  Module( parent, name )
{
  _Init( config );
}

void BufferState::_Init( const Configuration& config )
{
  _buf_size     = config.GetInt( "vc_buf_size" );
  _vcs          = config.GetInt( "num_vcs" );
  
  _wait_for_tail_credit = config.GetInt( "wait_for_tail_credit" );
  _vc_busy_when_full = config.GetInt( "vc_busy_when_full" );

  _in_use       = new bool [_vcs];
  _tail_sent    = new bool [_vcs];
  _cur_occupied = new int  [_vcs];

  for ( int v = 0; v < _vcs; ++v ) {
    _in_use[v]       = false;
    _tail_sent[v]    = false;
    _cur_occupied[v] = 0;
  }
  if( BufferState::_vc_range_begin.size() == 0){
    if(config.GetInt( "use_read_write" )){
      BufferState::_vc_range_begin[Flit::READ_REQUEST] 
	= config.GetInt( "read_request_begin_vc" );
      BufferState::_vc_range_size[Flit::READ_REQUEST] 
	= _vc_sel_last[Flit::READ_REQUEST] - BufferState::_vc_range_begin[Flit::READ_REQUEST] + 1;

      BufferState::_vc_range_begin[Flit::WRITE_REQUEST] 
	= config.GetInt( "write_request_begin_vc" );
      BufferState::_vc_range_size[Flit::WRITE_REQUEST] 
	= _vc_sel_last[Flit::WRITE_REQUEST] - BufferState::_vc_range_begin[Flit::WRITE_REQUEST] + 1;

      BufferState::_vc_range_begin[Flit::READ_REPLY] 
	= config.GetInt( "read_reply_begin_vc" );
      BufferState::_vc_range_size[Flit::READ_REPLY] 
	= _vc_sel_last[Flit::READ_REPLY] - BufferState::_vc_range_begin[Flit::READ_REPLY] + 1;

      BufferState::_vc_range_begin[Flit::WRITE_REPLY] 
	= config.GetInt( "write_reply_begin_vc" );
      BufferState::_vc_range_size[Flit::WRITE_REPLY] 
	= _vc_sel_last[Flit::WRITE_REPLY] - BufferState::_vc_range_begin[Flit::WRITE_REPLY] + 1;
    }  else {
      assert(config.GetInt( "message_classes")*config.GetInt( "routing_classes")*config.GetInt( "vc_per_class")<=_vcs);
      int vc_per_message = config.GetInt("routing_classes") * config.GetInt("vc_per_class");
      int vc_start = 0;
      for(int i = 0; i<config.GetInt( "message_classes"); i++){
	BufferState::_vc_range_begin.push_back(vc_start) ;
	vc_start+=vc_per_message;
	BufferState::_vc_range_size.push_back(vc_per_message) ;
      }
    }
  }
  
  //select pointer for the vc within a class
  if(config.GetInt( "use_read_write" )){
    _vc_sel_last[Flit::READ_REQUEST]
      = config.GetInt( "read_request_end_vc" );
    
    _vc_sel_last[Flit::WRITE_REQUEST]
      = config.GetInt( "write_request_end_vc" );
    
    _vc_sel_last[Flit::READ_REPLY]
      = config.GetInt( "read_reply_end_vc" );
    
    _vc_sel_last[Flit::WRITE_REPLY]
      = config.GetInt( "write_reply_end_vc" );
    
  }  else {
    assert(config.GetInt( "message_classes")*config.GetInt( "routing_classes")*config.GetInt( "vc_per_class")<=_vcs);
    int vc_per_message = config.GetInt("routing_classes") * config.GetInt("vc_per_class");
    int vc_start = 0;
    for(int i = 0; i<config.GetInt( "message_classes"); i++){
      vc_start+=vc_per_message;
      _vc_sel_last[i] = vc_start-1 ;
    }
  }
}

BufferState::~BufferState( )
{

  //depending on network channel latency, curr_occupied may not be all zero upon simulation completion
  //simulations stops after the last outstanding flit received, not the last outstanding credit.
  delete [] _in_use;
  delete [] _tail_sent;
  delete [] _cur_occupied;
}

void BufferState::ProcessCredit( Credit *c )
{
  assert( c );

  for ( int v = 0; v < c->vc_cnt; ++v ) {
    assert( ( c->vc[v] >= 0 ) && ( c->vc[v] < _vcs ) );

    if ( ( _wait_for_tail_credit ) && 
	 ( !_in_use[c->vc[v]] ) ) {
      Error( "Received credit for idle buffer" );
    }

    if ( _cur_occupied[c->vc[v]] > 0 ) {
      --_cur_occupied[c->vc[v]];

      if ( ( _cur_occupied[c->vc[v]] == 0 ) && 
	   ( _tail_sent[c->vc[v]] ) ) {
	_in_use[c->vc[v]] = false;
      }
    } else {
      cout << "VC = " << c->vc[v] << endl;
      Error( "Buffer occupancy fell below zero" );
    }
  }
}


void BufferState::SendingFlit( Flit *f )
{
  assert( f && ( f->vc >= 0 ) && ( f->vc < _vcs ) );

  if ( _cur_occupied[f->vc] < _buf_size ) {
    ++_cur_occupied[f->vc];

    if ( f->tail ) {
      _tail_sent[f->vc] = true;

      if ( !_wait_for_tail_credit ) {
	_in_use[f->vc] = false;
      }
    }
  } else {
    Error( "Flit sent to full buffer" );
  }
}

void BufferState::TakeBuffer( int vc )
{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );

  if ( _in_use[vc] ) {
    cout << "TakeBuffer( " << vc << " )" << endl;
    Display( );
    Error( "Buffer taken while in use" );
  }
  _in_use[vc]    = true;
  _tail_sent[vc] = false;
}

bool BufferState::IsFullFor( int vc  ) const
{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  return _cur_occupied[vc] == _buf_size;
}

bool BufferState::IsAvailableFor( int vc ) const
{
 
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  return !_in_use[vc] && (!_vc_busy_when_full || !IsFullFor(vc));
}

int BufferState::FindAvailable( Flit::FlitType type )
{
  for (int v = 1; v <= BufferState::_vc_range_size[type]; ++v) {
    int vc = BufferState::_vc_range_begin[type] + (_vc_sel_last[type] + v) % BufferState::_vc_range_size[type];
    if ( IsAvailableFor(vc) && !IsFullFor(vc)) {
      _vc_sel_last[type] = vc;
      return vc;
    }
  }
}

  int BufferState::FindAvailable( int type )
{
  for (int v = 1; v <= BufferState::_vc_range_size[type]; ++v) {
    int vc = BufferState::_vc_range_begin[type] + (_vc_sel_last[type] + v) % BufferState::_vc_range_size[type];
    if ( IsAvailableFor(vc) && !IsFullFor(vc)) {
    _vc_sel_last[type] = vc;
    return vc;
  }
}

return -1;
}

int BufferState::Size(int vc) const{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );

  return  _cur_occupied[vc];
}

void BufferState::Display( ) const
{
  cout << FullName() << " :" << endl;
  for ( int v = 0; v < _vcs; ++v ) {
    cout << "  buffer class " << v << endl;
    cout << "    in_use = " << _in_use[v] 
	 << " tail_sent = " << _tail_sent[v] << endl;
    cout << "    occupied = " << _cur_occupied[v] << endl;
  }

  for ( int f = 0; f < Flit::NUM_FLIT_TYPES; ++f) {
    cout << "vc_range[" << f << "] = [" << BufferState::_vc_range_begin[f] 
	 << "," <<  BufferState::_vc_range_begin[f] + BufferState::_vc_range_size[f] - 1 << "]" << endl;
  }
}
