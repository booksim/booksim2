// $Id$

/*
Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
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

#ifndef _BUFFER_STATE_HPP_
#define _BUFFER_STATE_HPP_

#include <vector>
#include <algorithm>

#include "module.hpp"
#include "flit.hpp"
#include "credit.hpp"
#include "config_utils.hpp"

class BufferState : public Module {
  
  class BufferPolicy : public Module {
  protected:
    BufferState const * const _buffer_state;
    int _vcs;
    int _active_vcs;
    vector<int> _vc_occupancy;
  public:
    BufferPolicy(Configuration const & config, BufferState * parent, 
		 const string & name);
    virtual void AllocVC(int vc = 0);
    virtual void FreeVC(int vc = 0);
    virtual void AllocSlotFor(int vc = 0);
    virtual void FreeSlotFor(int vc = 0);
    virtual bool IsFullFor(int vc = 0) const = 0;

    inline bool IsEmptyFor(int vc = 0) const {
      assert((vc >= 0) && (vc < _vcs));
      return (_vc_occupancy[vc] == 0);
    }
    inline int Occupancy(int vc = 0) const {
      assert((vc >= 0) && (vc < _vcs));
      return _vc_occupancy[vc];
    }

    static BufferPolicy * NewBufferPolicy(Configuration const & config, 
					  BufferState * parent, 
					  const string & name);
  };
  
  class PrivateBufferPolicy : public BufferPolicy {
  protected:
    int _vc_buf_size;
  public:
    PrivateBufferPolicy(Configuration const & config, BufferState * parent, 
			const string & name);
    virtual void AllocSlotFor(int vc = 0);
    virtual bool IsFullFor(int vc = 0) const;
  };
  
  class SharedBufferPolicy : public BufferPolicy {
  protected:
    vector<int> _private_buf_vc_map;
    vector<int> _private_buf_size;
    vector<int> _private_buf_occupancy;
    int _shared_buf_size;
    int _shared_buf_occupancy;
    vector<int> _reserved_slots;
    void ProcessFreeSlot(int vc = 0);
  public:
    SharedBufferPolicy(Configuration const & config, BufferState * parent, 
		       const string & name);
    virtual void FreeVC(int vc = 0);
    virtual void AllocSlotFor(int vc = 0);
    virtual void FreeSlotFor(int vc = 0);
    virtual bool IsFullFor(int vc = 0) const;
  };

  class VariableBufferPolicy : public SharedBufferPolicy {
  private:
    int _max_shared_slots;
  public:
    VariableBufferPolicy(Configuration const & config, BufferState * parent,
			 const string & name);
    virtual void AllocVC(int vc = 0);
    virtual void FreeVC(int vc = 0);
    virtual bool IsFullFor(int vc = 0) const;
  };
    
  bool _wait_for_tail_credit;
  bool _vc_busy_when_full;
  int  _size;
  int  _occupancy;
  int  _vcs;
  
  BufferPolicy * _buffer_policy;
  
  vector<bool> _in_use;
  vector<bool> _tail_sent;
  vector<int> _last_id;
  vector<int> _last_pid;

public:

  BufferState( const Configuration& config, 
	       Module *parent, const string& name );

  ~BufferState();

  void ProcessCredit( Credit const * const c );
  void SendingFlit( Flit const * const f );

  void TakeBuffer( int vc = 0 );

  inline bool IsFullFor( int vc = 0 ) const {
    return _buffer_policy->IsFullFor(vc);
  }
  inline bool IsEmptyFor( int vc = 0 ) const {
    return _buffer_policy->IsEmptyFor(vc);
  }
  inline bool IsAvailableFor( int vc = 0 ) const {
    assert( ( vc >= 0 ) && ( vc < _vcs ) );
    return !_in_use[vc] && (!_vc_busy_when_full || !IsFullFor(vc));
  }
  
  inline int Occupancy(int vc = 0) const {
    return  _buffer_policy->Occupancy(vc);
  }
  
  void Display( ostream & os = cout ) const;
};

#endif 
