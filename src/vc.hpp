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

#ifndef _VC_HPP_
#define _VC_HPP_

#include <deque>

#include "flit.hpp"
#include "outputset.hpp"
#include "routefunc.hpp"
#include "config_utils.hpp"

class VCRouter;

class VC : public Module {
public:
  enum eVCState { state_min = 0, idle = state_min, routing, vc_alloc, active, 
		  vc_spec, vc_spec_grant, state_max = vc_spec_grant };
  struct state_info_t {
    int cycles;
  };
  static const char * const VCSTATE[];
  
private:
  int _size;

  deque<Flit *> _buffer;
  
  eVCState _state;
  int      _state_time;
  
  OutputSet *_route_set;
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

public:
  
  VC() {}; // jbalfour: hack for GCC 3.4.4+
  void _Init( const Configuration& config, int outputs );

  VC( const Configuration& config, int outputs );
  VC( const Configuration& config, int outputs,
      Module *parent, const string& name );
  ~VC( );

  bool AddFlit( Flit *f );
  Flit *FrontFlit( );
  Flit *RemoveFlit( );
  
  
  inline bool Empty( ) const
  {
    return _buffer.empty( );
  }

  inline bool Full( ) const
  {
    return (int)_buffer.size( ) == _size;
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

  // ==== Debug functions ====

  void SetWatch( bool watch = true );
  bool IsWatched( ) const;
  int GetSize() const;
  void Display( ) const;
  static void DisplayStats( bool print_csv = false );
};

#endif 
