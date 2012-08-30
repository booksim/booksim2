// $Id$

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

#include "reservation.hpp"
#include "globals.hpp"
#include "booksim.hpp"
#include "buffer_state.hpp"
#include "random_utils.hpp"

BufferState::SharingPolicy::SharingPolicy(BufferState const * const buffer_state)
: _buffer_state(buffer_state)
{
  
}

BufferState::SharingPolicy * BufferState::SharingPolicy::NewSharingPolicy(Configuration const & config, BufferState const * const buffer_state)
{
  SharingPolicy * sp = NULL;
  string sharing_policy = config.GetStr("sharing_policy");
  if(sharing_policy == "unrestricted") {
    sp = new UnrestrictedSharingPolicy(buffer_state);
  } else if(sharing_policy == "variable") {
    sp = new VariableSharingPolicy(buffer_state);
  } else {
    cout << "Unknown sharing policy: " << sharing_policy << endl;
  }
  return sp;
}

BufferState::UnrestrictedSharingPolicy::UnrestrictedSharingPolicy(BufferState const * const buffer_state)
  : SharingPolicy(buffer_state)
{

}

BufferState::VariableSharingPolicy::VariableSharingPolicy(BufferState const * const buffer_state)
  : SharingPolicy(buffer_state)
{
  _max_slots = _GetSharedBufSize();
}

BufferState::BufferState( const Configuration& config, 
			  Module *parent, const string& name ) : 
Module( parent, name ), _shared_occupied(0), _active_vcs(0)
{
  _vc_buf_size     = config.GetInt( "vc_buf_size" );
  _spec_vc_buf_size = _vc_buf_size;
  _shared_buf_size = config.GetInt( "shared_buf_size" );
  _vcs             = config.GetInt( "num_vcs" );
  
  assert(_shared_buf_size==0);
  _sharing_policy = SharingPolicy::NewSharingPolicy(config, this);

  _wait_for_tail_credit = config.GetInt( "wait_for_tail_credit" );
  _vc_busy_when_full = config.GetInt( "vc_busy_when_full" );
  _cut_through = (config.GetInt("cut_through")==1);

  _in_use.resize(_vcs, false);
  _tail_sent.resize(_vcs, false);
  _cur_occupied.resize(_vcs, 0);
  _last_id.resize(_vcs, -1);
  _last_pid.resize(_vcs, -1);

}

BufferState::~BufferState()
{
  delete _sharing_policy;
}

void BufferState::ProcessCredit( Credit const * const c )
{
  assert( c );
  vector<int>::const_iterator iter = c->vc.begin();
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
      cout<< "Buffer occupancy fell below zero" ;
      assert(false);
      //      Error( "Buffer occupancy fell below zero" );
    }
    ++iter;
  }
  _sharing_policy->ProcessCredit(c);
}


void BufferState::SendingFlit( Flit const * const f )
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
  _sharing_policy->SendingFlit(f);
}

void BufferState::TakeBuffer( int vc )
{
  if(!( ( vc >= 0 ) && ( vc < _vcs ) )){
    cout<<vc<<endl;
  }
  assert( ( vc >= 0 ) && ( vc < _vcs ) );

  if ( _in_use[vc] ) {
    Error( "Buffer taken while in use" );
  }
  _in_use[vc]    = true;
  _tail_sent[vc] = false;
  assert(_active_vcs < _vcs);
  ++_active_vcs;
  _sharing_policy->TakeBuffer(vc);
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

bool BufferState::IsAvailableFor( int vc,int size) const
{
  assert(size!=-1);
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  assert(_vc_busy_when_full);
  assert(_cut_through);
  if(_in_use[vc]){
    return false;
  } else {

    return( _cur_occupied[vc]+size <= _vc_buf_size ) ;
  }

}


bool BufferState::IsAvailableFor( int vc ) const
{
 
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  if(_in_use[vc]){
    return false;
  } else {
    if(!_vc_busy_when_full)
      return true;
    else {
      if(_cut_through){
	return !IsFullFor(vc);
      } else {
	return !IsFullFor(vc);
      }
    }
  }
  return !_in_use[vc] && (!_vc_busy_when_full || !IsFullFor(vc));
}

bool BufferState::HasCreditFor( int vc ) const
{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );

  return ( ( _cur_occupied[vc] < _vc_buf_size ) ||
	   ( ( _shared_occupied < _shared_buf_size ) && 
	     ( ( _cur_occupied[vc] - _vc_buf_size ) < 
	       _sharing_policy->MaxSharedSlots(vc) ) ) );
}

void BufferState::Display( ostream & os ) const
{
  os << FullName() << " :" << endl;
  os << " shared_occupied = " << _shared_occupied << endl;
  for ( int v = 0; v < _vcs; ++v ) {
    os << "  buffer class " << v << endl;
    os << "    in_use = " << _in_use[v] 
       << " tail_sent = " << _tail_sent[v] << endl;
    os << "    occupied = " << _cur_occupied[v] << endl;
  }
}
