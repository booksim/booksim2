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

#ifndef _IQ_ROUTER_HPP_
#define _IQ_ROUTER_HPP_

#include <string>
#include <deque>
#include <queue>
#include <set>
#include <map>

#include "output_buffer.hpp"
#include "router.hpp"
#include "routefunc.hpp"
#include "large_roundrobin_arb.hpp"
using namespace std;

class VC;
class Flit;
class Credit;
class Buffer;
class BufferState;
class Allocator;
class SwitchMonitor;
class BufferMonitor;


class VCTag;


class IQRouter : public Router {

  //adaptive routing related
  bool _remove_credit_rtt;
  bool _track_routing_commitment;
  int* _current_bandwidth_commitment;
  int* _next_bandwidth_commitment;
  
  bool _cut_through; //true

  //voq
  bool _ecn_use_voq_size;
  bool _voq; //true
  bool _spec_voq;
  vector<pair<int,int> > _voq_pid;

  //remembering a vc is marked for drop
  vector<bool> _res_voq_drop;

  int _ctrl_vcs; //control
  int _special_vcs; // vcs that do not voq
  int _spec_vcs;// this is only used to set output buffer to spec
  int _data_vcs; //vcs that voq
  int _num_vcs; //same as config file?
  int _vcs; //vc including voq
  int _classes;

  bool _speculative;
  bool _spec_check_elig;
  bool _spec_mask_by_reqs;
  
  bool _active;

  int _dead_lock;

  int _routing_delay;
  int _vc_alloc_delay;
  int _sw_alloc_delay;
  int** dropped_pid;
  
  vector<bool> _output_hysteresis;
  vector<bool> _credit_hysteresis;


  multimap<int, Flit *> _in_queue_flits;

  deque<pair<int, pair<Credit *, int> > > _proc_credits;

  deque<VCTag* > _route_vcs;
  deque<VCTag* > _vc_alloc_vcs;  
  deque<VCTag* > _sw_hold_vcs;
  deque<VCTag* > _sw_alloc_vcs;

  deque<pair<int, pair<Flit *, pair<int, int> > > > _crossbar_flits;

  map<int, Credit *> _out_queue_credits;


  vector<Buffer *> _buf;
  vector<BufferState *> _next_buf;

  LargeRoundRobinArbiter** _VOQArbs;
  Allocator *_vc_allocator;
  Allocator *_sw_allocator;
  Allocator *_spec_sw_allocator;
  
  vector<int> _vc_rr_offset;
  vector<int> _sw_rr_offset;

  tRoutingFunction   _rf;

  vector<OutputBuffer*> _output_buffer;

  vector<queue<Credit *> > _credit_buffer;
  vector<  multimap<int, Credit *> > _credit_delay_queue;

  bool _hold_switch_for_packet;
  vector<bool> _switch_hold_in_skip;
  vector<bool> _switch_hold_out_skip;
  vector<int> _switch_hold_in;
  vector<int> _switch_hold_out;
  vector<int> _switch_hold_vc;

  vector<int> _input_head_time ;




  Flit* _ExpirationCheck(Flit* f, int input);
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

  
  void _UpdateCommitment(int input, int vc, const Flit* f, const OutputSet * route_set);
  // helper function for voq
  bool is_control_vc(int vc);
  bool is_voq_vc(int vc);
  int vc2voq(int vc, int output=-1);
  int voq2vc(int vvc, int output);
  int voqport(int vc);  

  // ----------------------------------------
  //
  //   Router Power Modellingyes
  //
  // ----------------------------------------

  SwitchMonitor * _switchMonitor ;
  BufferMonitor * _bufferMonitor ;
  
public:
  long* debug_delay_map;
  int* _crt_delay;
  int* _crt;
  bool* _global_table;
  int * _global_array;
  int _global_average;

  IQRouter( Configuration const & config,
	    Module *parent, string const & name, int id,
	    int inputs, int outputs );
  
  virtual ~IQRouter( );
  
  virtual void ReadInputs( );
  virtual void WriteOutputs( );
  
  void Display( ostream & os = cout ) const;

  virtual bool GetCongest(int c) const{
    return _global_table[c];
  }
  virtual int GetCredit(int out, int vc_begin=-1, int vc_end=-1 ) const;
  virtual int GetCommit(int out, int vc=-1) const;
  virtual int GetCreditArray(int out, int* vcs, int vc_count, bool rtt, bool commit) const;
  virtual int GetBuffer(int i = -1) const;
  virtual vector<int> GetBuffers(int i = -1) const;

  SwitchMonitor const * const GetSwitchMonitor() const {return _switchMonitor;}
  BufferMonitor const * const GetBufferMonitor() const {return _bufferMonitor;}

};

//replaces the router _vcs queue clusterF
class VCTag{
public:
  int _time;
  int _input;
  int _vc;
  int _output;
  const OutputSet::sSetElement*  _iset;

  bool _use;
  static VCTag* New(int t, int i, int v, int o);
  static VCTag* New();
  static VCTag* New(VCTag* old);
  static void FreeAll();
  void Free();
  void Reset();
  void ResetTO();
  void Set(int t, int i, int v, int o);
private:
  VCTag();
  ~VCTag(){}
  static stack<VCTag *> _all;
  static stack<VCTag *> _free;
};

#endif
