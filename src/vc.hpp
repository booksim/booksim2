// $Id: vc.hpp 938 2008-12-12 03:06:32Z dub $

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

#include <queue>

#include "flit.hpp"
#include "outputset.hpp"
#include "routefunc.hpp"
#include "config_utils.hpp"

class VCRouter;

class VC : public Module {
public:
  enum eVCState { idle, routing, vc_alloc, active, 
		  vc_spec, vc_spec_grant  };

private:
  int _size;

  queue<Flit *> _buffer;
  
  eVCState _state;
  int      _state_time;
  
  OutputSet *_route_set;
  int _out_port, _out_vc;

  int _occupied_cnt;
  int _total_cycles;
  int _vc_alloc_cycles;
  int _active_cycles;
  int _idle_cycles;

  int _pri;


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

  bool Empty( ) const;

  eVCState GetState( ) const;
  int      GetStateTime( ) const;
  void     SetState( eVCState s );

  const OutputSet *GetRouteSet( ) const;

  void SetOutput( int port, int vc );
  int  GetOutputPort( ) const;
  int  GetOutputVC( ) const;

  int  GetPriority( ) const;

  void Route( tRoutingFunction rf, const Router* router, const Flit* f, int in_channel );

  void AdvanceTime( );

  // ==== Debug functions ====

  void SetWatch( bool watch = true );
  bool IsWatched( ) const;
  int GetSize() const;
  void Display( ) const;
};

#endif 
