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

#ifndef _VC_HPP_
#define _VC_HPP_

#include <deque>

#include "flit.hpp"
#include "outputset.hpp"
#include "routefunc.hpp"
#include "config_utils.hpp"

class VC : public Module {
public:
  enum eVCState { state_min = 0, idle = state_min, routing, vc_alloc, active, 
		  state_max = active };
  struct state_info_t {
    int cycles;
  };
  static const char * const VCSTATE[];
  
private:

  class VCData{
  public:
    bool _use;
    deque<int> _time_stamp;
    deque<Flit *> _flit;
    deque<OutputSet *> _route_set;
    static VC::VCData * New();
    void Free();
  private:
    VCData() {_use=false;}
    ~VCData() {}
    static stack<VC::VCData *> _all;
    static stack<VC::VCData *> _free;
  };

  int _max_size;
  int _cur_size;
  
  VCData* _buffer;
  
  eVCState _state;
  int      _state_time;
  
  int _out_port, _out_vc;

  int _total_cycles;
  int _vc_alloc_cycles;
  int _active_cycles;
  int _idle_cycles;
  int _routing_cycles;
  
  static int total_cycles;
  static state_info_t state_info[];
  static int occupancy;
  
  enum ePrioType { local_age_based, queue_length_based, hop_count_based, none, other };

  ePrioType _pri_type;

  int _pri;

  int _priority_donation;

  bool _watched;

  int _expected_pid;

  int _last_id;
  int _last_pid;

  bool _drop;

  bool _special_vc;
public:
  
  VC( const Configuration& config, int outputs,
      Module *parent, const string& name , bool special=false);
  ~VC();
  
  bool SubstituteFrontFlit(Flit*f);

  bool AddFlit( Flit *f , OutputSet* o);
  inline Flit *FrontFlit( ) const
  {
    return (_buffer==NULL||_buffer->_flit.empty()) ? NULL : _buffer->_flit.front();
  }
  
  Flit *RemoveFlit( );
  
  inline void ResetExpected(){
    _expected_pid = -1;
  }
  
  inline int TimeStamp() const{
    return _buffer->_time_stamp.front();
  }

  inline bool Empty( ) const
  {
    return _buffer==NULL||_buffer->_flit.empty( );
  }

  inline bool Full( ) const
  {
    return _buffer==NULL?false: _cur_size == _max_size;
  }

  inline VC::eVCState GetState( ) const
  {
    return _state;
  }

  inline int GetStateTime( ) const
  {
    return _state_time;
  }


  void     SetState( eVCState s );

  const OutputSet *GetRouteSet( ) const;

  void SetOutput( int port, int vc );

  inline int GetOutputPort( ) const
  {
    return _out_port;
  }


  inline int GetOutputVC( ) const
  {
    return _out_vc;
  }

  void UpdatePriority();
 
  inline int GetPriority( ) const
  {
    return _pri;
  }
  void Route( tRoutingFunction rf, const Router* router, const Flit* f, int in_channel );

  void AdvanceTime( );

  inline int GetSize() const
  {
    return _buffer==NULL?0:_cur_size;
  }

    
  inline void SetDrop(){
    _drop=true;
  }
  inline bool GetDrop(){
    return _drop;
  }

  // ==== Debug functions ====

  void SetWatch( bool watch = true );
  bool IsWatched( ) const;
  void Display( ostream & os = cout ) const;
  static void DisplayStats( bool print_csv = false, ostream & os = cout );
};

#endif 
