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

#ifndef _BUFFER_STATE_HPP_
#define _BUFFER_STATE_HPP_

#include <vector>
#include <algorithm>

#include "module.hpp"
#include "flit.hpp"
#include "credit.hpp"
#include "config_utils.hpp"

class BufferState : public Module {
  
  friend class SharingPolicy;

  class SharingPolicy {
  protected:
    BufferState const * const _buffer_state;
    inline int _GetSharedBufSize() const { return _buffer_state->_shared_buf_size; }
  public:
    SharingPolicy(BufferState const * const buffer_state);
    virtual void ProcessCredit(Credit const * const c) = 0;
    virtual void SendingFlit(Flit const * const f) = 0;
    virtual void TakeBuffer(int vc = 0) = 0;
    virtual int MaxSharedSlots(int vc = 0) const = 0;
    static SharingPolicy * NewSharingPolicy(Configuration const & config, 
					    BufferState const * const buffer_state);
  };

  class UnrestrictedSharingPolicy : public SharingPolicy {
  public:
    UnrestrictedSharingPolicy(BufferState const * const buffer_state);
    virtual void ProcessCredit(Credit const * const c) {}
    virtual void SendingFlit(Flit const * const f) {}
    virtual void TakeBuffer(int vc = 0) {}
    virtual int MaxSharedSlots(int vc = 0) const { 
      return _GetSharedBufSize();
    }
  };

  friend class VariableSharingPolicy;

  class VariableSharingPolicy : public SharingPolicy {
  private:
    int _max_slots;
  protected:
    inline int _GetActiveVCs() const { return _buffer_state->_active_vcs; }
    inline void _UpdateMaxSlots() { 
      _max_slots = _GetSharedBufSize() / max(_GetActiveVCs(), 1);
    }
  public:
    VariableSharingPolicy(BufferState const * const buffer_state);
    virtual void ProcessCredit(Credit const * const c) {_UpdateMaxSlots(); }
    virtual void SendingFlit(Flit const * const f) { _UpdateMaxSlots(); }
    virtual void TakeBuffer(int vc = 0) { _UpdateMaxSlots(); }
    virtual int MaxSharedSlots(int vc = 0) const { return _max_slots; }
  };
    
  int  _wait_for_tail_credit;
  int  _vc_busy_when_full;
  int  _vc_buf_size;
  int  _spec_vc_buf_size;
  int  _shared_buf_size;
  int  _shared_occupied;
  int  _vcs;
  int  _active_vcs;
  bool _cut_through;

  SharingPolicy * _sharing_policy;
  
  vector<bool> _in_use;
  vector<bool> _tail_sent;
  vector<int> _cur_occupied;
  vector<int> _last_id;
  vector<int> _last_pid;

public:

  BufferState( const Configuration& config, 
	       Module *parent, const string& name );

  ~BufferState();

  void ProcessCredit( Credit const * const c );
  void SendingFlit( Flit const * const f );

  void TakeBuffer( int vc = 0 );

  bool IsFullFor( int vc = 0 ) const;
  bool IsEmptyFor( int vc = 0 ) const;
  bool IsAvailableFor( int vc,int size) const;
  bool IsAvailableFor( int vc = 0 ) const;
  bool HasCreditFor( int vc = 0 ) const;
  
  inline int Size (int vc = 0) const {
    assert((vc >= 0) && (vc < _vcs));
    return  _cur_occupied[vc];
  }
  
  void Display( ostream & os = cout ) const;
};

#endif 
