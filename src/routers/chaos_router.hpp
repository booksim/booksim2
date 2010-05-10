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

#ifndef _CHAOS_ROUTER_HPP_
#define _CHAOS_ROUTER_HPP_

#include <string>
#include <queue>

#include "module.hpp"
#include "router.hpp"
#include "allocator.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer_state.hpp"
#include "pipefifo.hpp"
#include "vc.hpp"

class ChaosRouter : public Router {

  tRoutingFunction   _rf;

  OutputSet **_input_route, **_mq_route;

  enum eQState {
    empty,         //            input avail
    filling,       //    >**H    ready to send
    full,          //  T****H    ready to send
    leaving,       //    T***>   input avail
    cut_through,   //    >***>
    shared         // >**HT**>
  };

  PipelineFIFO<Flit>   *_crossbar_pipe;

  int _multi_queue_size;
  int _buffer_size;

  queue<Flit *> *_input_frame;
  queue<Flit *> *_output_frame;
  queue<Flit *> *_multi_queue;

  int *_next_queue_cnt;

  queue<Credit *> *_credit_queue;

  eQState *_input_state;
  eQState *_multi_state;

  int *_input_output_match, *_input_mq_match, *_multi_match;

  int *_mq_age;

  bool *_output_matched;
  bool *_mq_matched;

  int _cur_channel;
  int _read_stall;

  bool _IsInjectionChan( int chan ) const;
  bool _IsEjectionChan( int chan ) const;

  bool _InputReady( int input ) const;
  bool _OutputFull( int out ) const;
  bool _OutputAvail( int out ) const;
  bool _MultiQueueFull( int mq ) const;

  int  _InputForOutput( int output ) const;
  int  _MultiQueueForOutput( int output ) const;
  int  _FindAvailMultiQueue( ) const;

  void _NextInterestingChannel( );
  void _OutputAdvance( );
  void _SendFlits( );
  void _SendCredits( );

public:
  ChaosRouter( const Configuration& config,
	    Module *parent, const string & name, int id,
	    int inputs, int outputs );

  virtual ~ChaosRouter( );

  virtual void ReadInputs( );
  virtual void InternalStep( );
  virtual void WriteOutputs( );

  virtual int GetCredit(int out, int vc_begin, int vc_end ) const {return 0;}
  virtual int GetBuffer(int i = -1) const {return 0;}
  virtual int GetReceivedFlits(int i = -1) const {return 0;}
  virtual int GetSentFlits(int i = -1) const {return 0;}
  virtual void ResetFlitStats() {}

  void Display( ) const;
};

#endif
