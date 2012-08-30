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

#ifndef _IQ_ROUTER_HPP_
#define _IQ_ROUTER_HPP_

#include <string>
#include <deque>
#include <queue>
#include <set>
#include <map>

#include "router.hpp"
#include "routefunc.hpp"

#include "shared_allocator.hpp"

using namespace std;

class VC;
class Flit;
class Credit;
class Buffer;
class BufferState;
class Allocator;
class SwitchMonitor;
class BufferMonitor;

class IQRouter : public Router {

  int _inj_request;
  int _inj_grant;

  int _cross_request;
  int _cross_grant;


  int _vcs;
  int _classes;

  bool _speculative;
  bool _spec_check_elig;
  bool _spec_mask_by_reqs;
  
  bool _active;

  int _routing_delay;
  int _vc_alloc_delay;
  int _sw_alloc_delay;
  

  
  SharedAllocator* shared_sw_allocator;

  map<int, Flit *> _in_queue_flits;

  deque<pair<int, pair<Credit *, int> > > _proc_credits;

  deque<pair<int, pair<int, int> > > _route_vcs;
  deque<pair<int, pair<pair<int, int>, int> > > _vc_alloc_vcs;  
  deque<pair<int, pair<pair<int, int>, int> > > _sw_hold_vcs;
  deque<pair<int, pair<pair<int, int>, int> > > _sw_alloc_vcs;

  deque<pair<int, pair<Flit *, pair<int, int> > > > _crossbar_flits;

  map<int, Credit *> _out_queue_credits;

  vector< int> _output_notifications;
  vector< int> _port_note;
  vector<Buffer *> _buf;
  vector<BufferState *> _next_buf;

  Allocator *_vc_allocator;
  Allocator *_sw_allocator;
  Allocator *_spec_sw_allocator;
  
  vector<int> _vc_rr_offset;
  vector<int> _sw_rr_offset;

  tRoutingFunction   _rf;

  vector<queue<Flit *> > _output_buffer;

  vector<queue<Credit *> > _credit_buffer;

  bool _hold_switch_for_packet;
  vector<int> _switch_hold_in;
  vector<int> _switch_hold_out;
  vector<int> _switch_hold_vc;

  bool _ReceiveFlits( );
  bool _ReceiveCredits( );

  virtual void _InternalStep( );

  bool _SWAllocAddReq(int input, int vc, int output);

  void _InputQueuing( );

  void _RouteEvaluate( );
  void _VCAllocEvaluate( );
  void _SWHoldEvaluate( );
  void _SWAllocEvaluate( );
  void _SwitchEvaluate( );

  void _RouteUpdate( );
  void _VCAllocUpdate( );
  void _SWHoldUpdate( );
  void _SWAllocUpdate( );
  void _SwitchUpdate( );

  void _OutputQueuing( );

  void _SendFlits( );
  void _SendCredits( );
  
  // ----------------------------------------
  //
  //   Router Power Modellingyes
  //
  // ----------------------------------------

  SwitchMonitor * _switchMonitor ;
  BufferMonitor * _bufferMonitor ;
  
public:

  IQRouter( Configuration const & config,
	    Module *parent, string const & name, int id,
	    int inputs, int outputs );
  
  virtual ~IQRouter( );
  
  virtual void ReadInputs( );
  virtual void WriteOutputs( );
  
  void Display( ostream & os = cout ) const;

  virtual int GetCredit(int out, int vc_begin, int vc_end ) const;
  virtual int GetBuffer(int i = -1) const;
  virtual vector<int> GetBuffers(int i = -1) const;

  SwitchMonitor const * const GetSwitchMonitor() const {return _switchMonitor;}
  BufferMonitor const * const GetBufferMonitor() const {return _bufferMonitor;}

};

#endif
