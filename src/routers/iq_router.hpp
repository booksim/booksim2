// $Id: iq_router.hpp 938 2008-12-12 03:06:32Z dub $

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
#include <queue>

#include "module.hpp"
#include "router.hpp"
#include "vc.hpp"
#include "allocator.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer_state.hpp"
#include "pipefifo.hpp"

class SwitchMonitor {
  int  _cycles ;
  int  _inputs ;
  int  _outputs ;
  int* _event ;
  int index( int input, int output, int flitType ) const ;
public:
  SwitchMonitor( int inputs, int outputs ) ;
  void cycle() ;
  int* GetActivity(){return _event;}
  int NumInputs(){return _inputs;}
  int NumOutputs(){return _outputs;}
  void traversal( int input, int output, Flit* flit ) ;
  friend ostream& operator<<( ostream& os, const SwitchMonitor& obj ) ;
  
} ;

class BufferMonitor {
  int  _cycles ;
  int  _inputs ;
  int* _reads ;
  int* _writes ;
  int index( int input, int flitType ) const ;
public:
  BufferMonitor( int inputs ) ;
  void cycle() ;
  void write( int input, Flit* flit ) ;
  void read( int input, Flit* flit ) ;
  int* GetReads(){return _reads;}
  int* GetWrites(){return _writes;}
  int NumInputs(){return _inputs;}
  friend ostream& operator<<( ostream& os, const BufferMonitor& obj ) ;
} ;

class IQRouter : public Router {
  int  _vcs ;
  int  _vc_size ;
  int  _speculative ;
  int  _filter_spec_grants ;

  VC          **_vc;
  BufferState *_next_vcs;

  Allocator *_vc_allocator;
  Allocator *_sw_allocator;
  Allocator *_spec_sw_allocator;

  int *_sw_rr_offset;

  tRoutingFunction   _rf;

  PipelineFIFO<Flit>   *_crossbar_pipe;
  PipelineFIFO<Credit> *_credit_pipe;

  queue<Flit *> *_input_buffer;
  queue<Flit *> *_output_buffer;

  queue<Credit *> *_in_cred_buffer;
  queue<Credit *> *_out_cred_buffer;

  int _hold_switch_for_packet;
  int *_switch_hold_in;
  int *_switch_hold_out;
  int *_switch_hold_vc;

  void _ReceiveFlits( );
  void _ReceiveCredits( );

  void _InputQueuing( );
  void _Route( );
  void _VCAlloc( );
  void _SWAlloc( );
  void _OutputQueuing( );

  void _SendFlits( );
  void _SendCredits( );
  
  void _AddVCRequests( VC* cur_vc, int input_index, bool watch = false );
  // ----------------------------------------
  //
  //   Router Power Modelling
  //
  // ----------------------------------------
public:
  SwitchMonitor switchMonitor ;
  
public:

  BufferMonitor bufferMonitor ;
  
public:
  IQRouter( const Configuration& config,
	    Module *parent, string name, int id,
	    int inputs, int outputs );
  
  virtual ~IQRouter( );
  
  virtual void ReadInputs( );
  virtual void InternalStep( );
  virtual void WriteOutputs( );
  
  void Display( ) const;
  virtual int GetCredit(int out, int vc_begin, int vc_end ) const;
  virtual int GetBuffer(int i) const;
  
  SwitchMonitor* GetSwitchMonitor(){return &switchMonitor;}
  BufferMonitor* GetBufferMonitor(){return &bufferMonitor;}
};

#endif
