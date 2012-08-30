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
#include <math.h>
#include <vector>
#include <queue>


#include "globals.hpp"
#include "booksim.hpp"
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
    vector<double> _vc_rate_of_arrival;
    vector<int> _vc_arrival_last_update;
    vector<double> _vc_rate_of_drain;
    vector<int> _vc_drain_last_update;
    double _ewma_ratio;
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
    inline double ArrivalRate(int vc = 0)  {
      assert((vc >= 0 ) && (vc < _vcs));
      //call to get rate triggers decay update
      int cur_time = GetSimTime();
      if(cur_time!= _vc_arrival_last_update[vc]){
	_vc_rate_of_arrival[vc] = pow(_ewma_ratio, cur_time-_vc_arrival_last_update[vc] )*_vc_rate_of_arrival[vc] ;
      }
      _vc_arrival_last_update[vc] = cur_time;
      return _vc_rate_of_arrival[vc];
    }
    inline double DrainRate(int vc = 0)  {
      assert((vc >= 0 ) && (vc < _vcs));
      //call to get rate triggers decay update
      int cur_time = GetSimTime();

      if(cur_time!= _vc_drain_last_update[vc] && _vc_drain_last_update[vc]!=-1){
	_vc_rate_of_drain[vc] = pow(_ewma_ratio, cur_time-_vc_drain_last_update[vc] )*_vc_rate_of_drain[vc] ;
      }
      _vc_drain_last_update[vc] = cur_time;
      return _vc_rate_of_drain[vc];
    }

    static BufferPolicy * New(Configuration const & config, 
			      BufferState * parent, const string & name);
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
    int _buf_size;
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

  class LimitedSharedBufferPolicy : public SharedBufferPolicy {
  protected:
    int _max_held_slots;
  public:
    LimitedSharedBufferPolicy(Configuration const & config, 
			      BufferState * parent,
			      const string & name);
    virtual bool IsFullFor(int vc = 0) const;
  };
    
  class DynamicLimitedSharedBufferPolicy : public LimitedSharedBufferPolicy {
  public:
    DynamicLimitedSharedBufferPolicy(Configuration const & config, 
				     BufferState * parent,
				     const string & name);
    virtual void AllocVC(int vc = 0);
    virtual void FreeVC(int vc = 0);
  };
  
  class ShiftingDynamicLimitedSharedBufferPolicy : public DynamicLimitedSharedBufferPolicy {
  public:
    ShiftingDynamicLimitedSharedBufferPolicy(Configuration const & config, 
					     BufferState * parent,
					     const string & name);
    virtual void AllocVC(int vc = 0);
    virtual void FreeVC(int vc = 0);
  };
  
  class FeedbackSharedBufferPolicy : public SharedBufferPolicy {
  protected:
    vector<int> _occupancy_limit;
    vector<int> _round_trip_time;
    queue<int> _flit_sent_time;
    int _min_round_trip_time;
    int _total_mapped_size;
    int _aging_scale;
    int _offset;
  public:
    FeedbackSharedBufferPolicy(Configuration const & config, 
			       BufferState * parent, const string & name);
    virtual void AllocSlotFor(int vc = 0);
    virtual void FreeSlotFor(int vc = 0);
    virtual bool IsFullFor(int vc = 0) const;
  };
  
  bool _wait_for_tail_credit;
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

  inline bool IsFull() const {
    return (_occupancy < _size);
  }
  inline bool IsFullFor( int vc = 0 ) const {
    return _buffer_policy->IsFullFor(vc);
  }
  inline bool IsEmptyFor( int vc = 0 ) const {
    return _buffer_policy->IsEmptyFor(vc);
  }
  inline bool IsAvailableFor( int vc = 0 ) const {
    assert( ( vc >= 0 ) && ( vc < _vcs ) );
    return !_in_use[vc];
  }
  
  inline int Occupancy(int vc = 0) const {
    return  _buffer_policy->Occupancy(vc);
  }
  
  inline double ArrivalRate(int vc = 0) const {
    return _buffer_policy->ArrivalRate(vc);
  }
  inline double DrainRate(int vc = 0) const {
    return _buffer_policy->DrainRate(vc);
  }
  void Display( ostream & os = cout ) const;
};

#endif 
