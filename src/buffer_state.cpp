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

#include "booksim.hpp"
#include "buffer_state.hpp"
#include "random_utils.hpp"

BufferState::BufferPolicy::BufferPolicy(Configuration const & config, BufferState * parent, const string & name)
: Module(parent, name), _buffer_state(parent), _active_vcs(0)
{
  _vcs = config.GetInt("num_vcs");
  _vc_occupancy.resize(_vcs, 0);
}

void BufferState::BufferPolicy::AllocVC(int vc)
{
  assert((vc >= 0) && (vc < _vcs));
  ++_active_vcs;
  if(_active_vcs > _vcs) {
    Error("Number of active VCs is too large.");
  }
}

void BufferState::BufferPolicy::FreeVC(int vc)
{
  assert((vc >= 0) && (vc < _vcs));
  --_active_vcs;
  if(_active_vcs < 0) {
    Error("Number of active VCs fell below zero.");
  }
}

void BufferState::BufferPolicy::AllocSlotFor(int vc)
{
  assert((vc >= 0) && (vc < _vcs));
  ++_vc_occupancy[vc];
}

void BufferState::BufferPolicy::FreeSlotFor(int vc)
{
  assert((vc >= 0) && (vc < _vcs));
  --_vc_occupancy[vc];
  if(_vc_occupancy[vc] < 0) {
    ostringstream err;
    err << "Buffer occupancy fell below zero for VC " << vc;
    Error(err.str());
  }
}

BufferState::BufferPolicy * BufferState::BufferPolicy::NewBufferPolicy(Configuration const & config, BufferState * parent, const string & name)
{
  BufferPolicy * sp = NULL;
  string buffer_policy = config.GetStr("buffer_policy");
  if(buffer_policy == "private") {
    sp = new PrivateBufferPolicy(config, parent, name);
  } else if(buffer_policy == "shared") {
    sp = new SharedBufferPolicy(config, parent, name);
  } else if(buffer_policy == "variable") {
    sp = new VariableBufferPolicy(config, parent, name);
  } else {
    cout << "Unknown buffer policy: " << buffer_policy << endl;
  }
  return sp;
}

BufferState::PrivateBufferPolicy::PrivateBufferPolicy(Configuration const & config, BufferState * parent, const string & name)
  : BufferPolicy(config, parent, name)
{
  _vc_buf_size = config.GetInt("buf_size") / _vcs;
  if(_vc_buf_size <= 0) {
    _vc_buf_size = config.GetInt("vc_buf_size");
  }
  assert(_vc_buf_size > 0);
}

void BufferState::PrivateBufferPolicy::AllocSlotFor(int vc)
{
  BufferPolicy::AllocSlotFor(vc);
  if(_vc_occupancy[vc] > _vc_buf_size) {
    ostringstream err;
    err << "Buffer overflow for VC " << vc;
    Error(err.str());
  }
}

bool BufferState::PrivateBufferPolicy::IsFullFor(int vc) const
{
  assert((vc >= 0) && (vc < _vcs));
  return (_buffer_state->Occupancy(vc) >= _vc_buf_size);
}

BufferState::SharedBufferPolicy::SharedBufferPolicy(Configuration const & config, BufferState * parent, const string & name)
  : BufferPolicy(config, parent, name), _shared_buf_occupancy(0)
{
  int num_private_bufs = config.GetInt("private_bufs");
  if(num_private_bufs < 0) {
    num_private_bufs = _vcs;
  }
  
  _private_buf_occupancy.resize(num_private_bufs, 0);

  int buf_size = config.GetInt("buf_size");

  _private_buf_size = config.GetIntArray("private_buf_size");
  if(_private_buf_size.empty()) {
    int const bs = config.GetInt("private_buf_size");
    if(bs < 0) {
      _private_buf_size.push_back(buf_size / num_private_bufs);
    } else {
      _private_buf_size.push_back(bs);
    }
  }
  _private_buf_size.resize(num_private_bufs, _private_buf_size.back());
  
  vector<int> start_vc = config.GetIntArray("private_buf_start_vc");
  if(start_vc.empty()) {
    int const sv = config.GetInt("private_buf_start_vc");
    if(sv < 0) {
      start_vc.resize(num_private_bufs);
      for(int i = 0; i < num_private_bufs; ++i) {
	start_vc[i] = i * _vcs / num_private_bufs;
      }
    } else {
      start_vc.push_back(sv);
    }
  }
  
  vector<int> end_vc = config.GetIntArray("private_buf_end_vc");
  if(end_vc.empty()) {
    int const ev = config.GetInt("private_buf_end_vc");
    if(ev < 0) {
      end_vc.resize(num_private_bufs);
      for(int i = 0; i < num_private_bufs; ++i) {
	end_vc[i] = (i + 1) * _vcs / num_private_bufs - 1;
      }
    } else {
      end_vc.push_back(ev);
    }
  }

  _private_buf_vc_map.resize(_vcs, -1);
  _shared_buf_size = buf_size;
  for(int i = 0; i < num_private_bufs; ++i) {
    _shared_buf_size -= _private_buf_size[i];
    assert(start_vc[i] <= end_vc[i]);
    for(int v = start_vc[i]; v <= end_vc[i]; ++v) {
      assert(_private_buf_vc_map[v] < 0);
      _private_buf_vc_map[v] = i;
    }
  }
  assert(_shared_buf_size >= 0);

  _reserved_slots.resize(_vcs, 0);
}

void BufferState::SharedBufferPolicy::ProcessFreeSlot(int vc)
{
  int i = _private_buf_vc_map[vc];
  --_private_buf_occupancy[i];
  if(_private_buf_occupancy[i] < 0) {
    ostringstream err;
    err << "Private buffer occupancy fell below zero for buffer " << i;
    Error(err.str());
  } else if(_private_buf_occupancy[i] >= _private_buf_size[i]) {
    --_shared_buf_occupancy;
    if(_shared_buf_occupancy < 0) {
      Error("Shared buffer occupancy fell below zero.");
    }
  }
}

void BufferState::SharedBufferPolicy::FreeVC(int vc)
{
  BufferPolicy::FreeVC(vc);
  assert((_vc_occupancy[vc] > 0) || (_reserved_slots[vc] > 0));
  while(_reserved_slots[vc]) {
    --_reserved_slots[vc];
    ProcessFreeSlot(vc);
  }
}

void BufferState::SharedBufferPolicy::AllocSlotFor(int vc)
{
  BufferPolicy::AllocSlotFor(vc);
  if(_reserved_slots[vc] > 0) {
    --_reserved_slots[vc];
  } else {
    int i = _private_buf_vc_map[vc];
    ++_private_buf_occupancy[i];
    if(_private_buf_occupancy[i] > _private_buf_size[i]) {
      ++_shared_buf_occupancy;
      if(_shared_buf_occupancy > _shared_buf_size) {
	Error("Shared buffer overflow.");
      }
    }
  }
}

void BufferState::SharedBufferPolicy::FreeSlotFor(int vc)
{
  BufferPolicy::FreeSlotFor(vc);
  if(_vc_occupancy[vc] == 0) {
    ++_reserved_slots[vc];
  } else {
    ProcessFreeSlot(vc);
  }
}

bool BufferState::SharedBufferPolicy::IsFullFor(int vc) const
{
  int i = _private_buf_vc_map[vc];
  return ((_reserved_slots[vc] == 0) &&
	  (_private_buf_occupancy[i] >= _private_buf_size[i]) &&
	  (_shared_buf_occupancy >= _shared_buf_size));
}

BufferState::VariableBufferPolicy::VariableBufferPolicy(Configuration const & config, BufferState * parent, const string & name)
  : SharedBufferPolicy(config, parent, name)
{
  _max_shared_slots = _shared_buf_size;
}

void BufferState::VariableBufferPolicy::AllocVC(int vc)
{
  BufferPolicy::AllocVC(vc);
  assert(_active_vcs);
  _max_shared_slots = _shared_buf_size / _active_vcs;
}

void BufferState::VariableBufferPolicy::FreeVC(int vc)
{
  SharedBufferPolicy::FreeVC(vc);
  if(_active_vcs) {
    _max_shared_slots = _shared_buf_size / _active_vcs;
  }
}

bool BufferState::VariableBufferPolicy::IsFullFor(int vc) const
{
  int i = _private_buf_vc_map[vc];
  return ((_reserved_slots[vc] == 0) &&
	  (((_private_buf_occupancy[i] >= _private_buf_size[i]) &&
	    (_shared_buf_occupancy >= _shared_buf_size)) ||
	   (_private_buf_occupancy[i] - _private_buf_size[i] >= 
	    _max_shared_slots)));
}

BufferState::BufferState( const Configuration& config, Module *parent, const string& name ) : 
  Module( parent, name ), _occupancy(0)
{
  _vcs = config.GetInt( "num_vcs" );
  _size = config.GetInt("buf_size");
  if(_size < 0) {
    _size = _vcs * config.GetInt("vc_buf_size");
  }

  _buffer_policy = BufferPolicy::NewBufferPolicy(config, this, "policy");

  _wait_for_tail_credit = config.GetInt( "wait_for_tail_credit" );
  _vc_busy_when_full = config.GetInt( "vc_busy_when_full" );

  _in_use.resize(_vcs, false);
  _tail_sent.resize(_vcs, false);

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
    _buffer_policy->FreeSlotFor(*iter);

    if(_wait_for_tail_credit && IsEmptyFor(*iter) && _tail_sent[*iter]) {
      assert(_in_use[*iter]);
      _in_use[*iter] = false;
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

  _buffer_policy->AllocSlotFor(f->vc);
  
  if ( f->tail ) {
    _tail_sent[f->vc] = true;
    
    if ( !_wait_for_tail_credit ) {
      assert(_in_use[f->vc]);
      _in_use[f->vc] = false;
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
  _buffer_policy->AllocVC(vc);
}

void BufferState::Display( ostream & os ) const
{
  os << FullName() << " :" << endl;
  os << " occupied = " << _occupancy << endl;
  for ( int v = 0; v < _vcs; ++v ) {
    os << "  VC " << v << ": ";
    os << "in_use = " << _in_use[v] 
       << ", tail_sent = " << _tail_sent[v]
       << ", occupied = " << _buffer_policy->Occupancy(v) << endl;
  }
}
