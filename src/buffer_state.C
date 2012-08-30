// $Id: buffer_state.cpp 2388 2010-08-05 21:36:41Z qtedq $

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

/*buffer_state.cpp
 *
 * This class is the buffere state of the next router down the channel
 * tracks the credit and how much of the buffer is in use 
 */

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cassert>

#include "booksim.hpp"
#include "buffer_state.hpp"
#include "random_utils.hpp"


BufferState::BufferState( const Configuration& config, 
			  Module *parent, const string& name ) : 
Module( parent, name ), _shared_occupied(0), _active_vcs(0)
{
  _vc_buf_size     = config.GetInt( "vc_buf_size" );
  _shared_buf_size = config.GetInt( "shared_buf_size" );
  _dynamic_sharing = config.GetInt( "dynamic_sharing" );
  _vcs             = config.GetInt( "num_vcs" );
  
  _wait_for_tail_credit = config.GetInt( "wait_for_tail_credit" );
  _vc_busy_when_full = config.GetInt( "vc_busy_when_full" );


  _in_use.resize(_vcs, false);
  _tail_sent.resize(_vcs, false);
  _cur_occupied.resize(_vcs, 0);
  _last_id.resize(_vcs, -1);
  _last_pid.resize(_vcs, -1);
}

void BufferState::ProcessCredit( Credit const * c )
{
  assert( c );

  set<int>::iterator iter = c->vc.begin();
  while(iter != c->vc.end()) {

    assert( ( *iter >= 0 ) && ( *iter < _vcs ) );

    if ( ( _wait_for_tail_credit ) && 
	 ( !_in_use[*iter] ) ) {
      Error( "Received credit for idle buffer" );
    }

    if ( _cur_occupied[*iter] > 0 ) {
      if(_cur_occupied[*iter] > _vc_buf_size) {
	--_shared_occupied;
      }
      --_cur_occupied[*iter];

      if ( _wait_for_tail_credit &&
	   ( _cur_occupied[*iter] == 0 ) && 
	   ( _tail_sent[*iter] ) ) {
	assert(_in_use[*iter]);
	_in_use[*iter] = false;
	assert(_active_vcs > 0);
	--_active_vcs;
      }
    } else {
      cout << "VC = " << *iter << endl;
      Error( "Buffer occupancy fell below zero" );
    }
    ++iter;
  }
}


void BufferState::SendingFlit( Flit const * f )
{
  assert( f && ( f->vc >= 0 ) && ( f->vc < _vcs ) );

  if ( ( _shared_occupied >= _shared_buf_size ) &&
       ( _cur_occupied[f->vc] >= _vc_buf_size ) ) {
    ostringstream err;
    err << "Flit " << f->id << " sent to full buffer.";
    Error( err.str( ) );
  } else {
    if ( _cur_occupied[f->vc] >= _vc_buf_size ) {
      ++_shared_occupied;
    }
    ++_cur_occupied[f->vc];
    
    if ( f->tail ) {
      _tail_sent[f->vc] = true;
      
      if ( !_wait_for_tail_credit ) {
	assert(_in_use[f->vc]);
	_in_use[f->vc] = false;
	assert(_active_vcs > 0);
	--_active_vcs;
      }
    }
    _last_id[f->vc] = f->id;
    _last_pid[f->vc] = f->pid;
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
  assert(_active_vcs < _vcs);
  ++_active_vcs;
}

bool BufferState::IsFullFor( int vc ) const
{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  return ( ( _cur_occupied[vc] >= _vc_buf_size ) &&
	   ( _shared_occupied >= _shared_buf_size ) );
}

bool BufferState::IsEmptyFor( int vc  ) const
{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  return ( _cur_occupied[vc] == 0 );
}

bool BufferState::IsAvailableFor( int vc ) const
{
 
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  return !_in_use[vc] && (!_vc_busy_when_full || !IsFullFor(vc));
}

bool BufferState::HasCreditFor( int vc ) const
{
  
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  return ( ( _cur_occupied[vc] < _vc_buf_size ) ||
	   ( ( _shared_occupied < _shared_buf_size ) &&
	     ( !_dynamic_sharing || ( _active_vcs == 0 ) ||
	       ( ( _cur_occupied[vc] - _vc_buf_size ) < 
		 ( _shared_buf_size / _active_vcs ) ) ) ) );
}


int BufferState::Size(int vc) const{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );

  return  _cur_occupied[vc];
}

void BufferState::Display( ) const
{
  cout << FullName() << " :" << endl;
  cout << " shared_occupied = " << _shared_occupied << endl;
  for ( int v = 0; v < _vcs; ++v ) {
    cout << "  buffer class " << v << endl;
    cout << "    in_use = " << _in_use[v] 
	 << " tail_sent = " << _tail_sent[v] << endl;
    cout << "    occupied = " << _cur_occupied[v] << endl;
  }

}
