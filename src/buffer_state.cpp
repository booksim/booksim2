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

BufferState::BufferPolicy::BufferPolicy(Configuration const & config, BufferState * parent, const string & name)
: Module(parent, name), _buffer_state(parent)
{
  
}

BufferState::BufferPolicy * BufferState::BufferPolicy::NewBufferPolicy(Configuration const & config, BufferState * parent, const string & name)
{
  BufferPolicy * sp = NULL;
  string buffer_policy = config.GetStr("buffer_policy");
  if(buffer_policy == "shared") {
    sp = new SharedBufferPolicy(config, parent, name);
  } else if(buffer_policy == "variable") {
    sp = new VariableBufferPolicy(config, parent, name);
  } else {
    cout << "Unknown buffer policy: " << buffer_policy << endl;
  }
  return sp;
}

BufferState::SharedBufferPolicy::SharedBufferPolicy(Configuration const & config, BufferState * parent, const string & name)
  : BufferPolicy(config, parent, name), _shared_occupied(0)
{
  _vc_buf_size = config.GetInt("vc_buf_size");
  _shared_buf_size = config.GetInt("shared_buf_size");
}

void BufferState::SharedBufferPolicy::AllocSlotFor(int vc)
{
  if(_buffer_state->Size(vc) > _vc_buf_size) {
    ++_shared_occupied;
    if(_shared_occupied > _shared_buf_size) {
      Error("Buffer overflow.");
    }
  }
}

void BufferState::SharedBufferPolicy::FreeSlotFor(int vc)
{
  if(_buffer_state->Size(vc) >= _vc_buf_size) {
    --_shared_occupied;
    assert(_shared_occupied >= 0);
  }
}

bool BufferState::SharedBufferPolicy::IsFullFor(int vc) const
{
  return ((_buffer_state->Size(vc) >= _vc_buf_size) &&
	  (_shared_occupied >= _shared_buf_size));
}

BufferState::VariableBufferPolicy::VariableBufferPolicy(Configuration const & config, BufferState * parent, const string & name)
  : SharedBufferPolicy(config, parent, name)
{
  _max_slots_per_vc = _shared_buf_size;
}

void BufferState::VariableBufferPolicy::AllocVC(int vc)
{
  assert(_buffer_state->ActiveVCs());
  _max_slots_per_vc = _shared_buf_size / _buffer_state->ActiveVCs();
}

void BufferState::VariableBufferPolicy::FreeVC(int vc)
{
  int active_vcs = _buffer_state->ActiveVCs();
  if(active_vcs) {
    _max_slots_per_vc = _shared_buf_size / active_vcs;
  }
}

bool BufferState::VariableBufferPolicy::IsFullFor(int vc) const
{
  return (SharedBufferPolicy::IsFullFor(vc) ||
	  ((_buffer_state->Size(vc) >= _vc_buf_size) &&
	   (_buffer_state->Size(vc) - _vc_buf_size >= _max_slots_per_vc)));
}

BufferState::BufferState( const Configuration& config, Module *parent, const string& name ) : 
  Module( parent, name ), _occupancy(0), _active_vcs(0)
{
  _vcs = config.GetInt( "num_vcs" );
  _size = _vcs * config.GetInt("vc_buf_size") + config.GetInt("shared_buf_size");

  _buffer_policy = BufferPolicy::NewBufferPolicy(config, this, "policy");

  _wait_for_tail_credit = config.GetInt( "wait_for_tail_credit" );
  _vc_busy_when_full = config.GetInt( "vc_busy_when_full" );

  _in_use.resize(_vcs, false);
  _tail_sent.resize(_vcs, false);
  _vc_occupancy.resize(_vcs, 0);
  _last_id.resize(_vcs, -1);
  _last_pid.resize(_vcs, -1);
}

BufferState::~BufferState()
{
  delete _buffer_policy;
}

void BufferState::ProcessCredit( Credit const * const c )
{
  assert( c );

  set<int>::iterator iter = c->vc.begin();
  while(iter != c->vc.end()) {

    assert( ( *iter >= 0 ) && ( *iter < _vcs ) );

    if ( ( _wait_for_tail_credit ) && 
	 ( !_in_use[*iter] ) ) {
      ostringstream err;
      err << "Received credit for idle VC " << *iter;
      Error( err.str() );
    }
    --_occupancy;
    if(_occupancy < 0) {
      Error("Buffer occupancy fell below zero.");
    }
    --_vc_occupancy[*iter];
    if(_vc_occupancy[*iter] < 0) {
      ostringstream err;
      err << "Buffer occupancy fell below zero for VC " << *iter;
      Error( err.str() );
    }
    _buffer_policy->FreeSlotFor(*iter);

    if(_wait_for_tail_credit && (_vc_occupancy[*iter] == 0) && (_tail_sent[*iter])) {
      assert(_in_use[*iter]);
      _in_use[*iter] = false;
      assert(_active_vcs > 0);
      --_active_vcs;
      _buffer_policy->FreeVC(*iter);
    }
    ++iter;
  }
}


void BufferState::SendingFlit( Flit const * const f )
{
  assert( f && ( f->vc >= 0 ) && ( f->vc < _vcs ) );

  ++_occupancy;
  if(_occupancy > _size) {
    Error("Buffer overflow.");
  }
  
  ++_vc_occupancy[f->vc];
  _buffer_policy->AllocSlotFor(f->vc);
  
  if ( f->tail ) {
    _tail_sent[f->vc] = true;
    
    if ( !_wait_for_tail_credit ) {
      assert(_in_use[f->vc]);
      _in_use[f->vc] = false;
      assert(_active_vcs > 0);
      --_active_vcs;
      _buffer_policy->FreeVC(f->vc);
    }
  }
  _last_id[f->vc] = f->id;
  _last_pid[f->vc] = f->pid;
}

void BufferState::TakeBuffer( int vc )
{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );

  if ( _in_use[vc] ) {
    ostringstream err;
    err << "Buffer taken while in use for VC " << vc;
    Error( err.str() );
  }
  _in_use[vc]    = true;
  _tail_sent[vc] = false;
  assert(_active_vcs < _vcs);
  ++_active_vcs;
  _buffer_policy->AllocVC(vc);
}

bool BufferState::IsEmptyFor( int vc  ) const
{
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  return ( _vc_occupancy[vc] == 0 );
}

bool BufferState::IsAvailableFor( int vc ) const
{
 
  assert( ( vc >= 0 ) && ( vc < _vcs ) );
  return !_in_use[vc] && (!_vc_busy_when_full || !IsFullFor(vc));
}

void BufferState::Display( ostream & os ) const
{
  os << FullName() << " :" << endl;
  os << " occupied = " << _occupancy << endl;
  for ( int v = 0; v < _vcs; ++v ) {
    os << "  VC " << v << ": ";
    os << "in_use = " << _in_use[v] 
       << ", tail_sent = " << _tail_sent[v]
       << ", occupied = " << _vc_occupancy[v] << endl;
  }
}
