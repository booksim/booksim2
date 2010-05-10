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

#ifndef _EVENT_ROUTER_HPP_
#define _EVENT_ROUTER_HPP_

#include <string>
#include <queue>

#include "module.hpp"
#include "router.hpp"
#include "vc.hpp"
#include "prio_arb.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "pipefifo.hpp"

class EventNextVCState : public Module {
public:
  enum eNextVCState { idle, busy, tail_pending };

  struct tWaiting {
    int  input;
    int  vc;
    int  id;
    int  pres;
    bool watch;
  };

private:
  int _buf_size;
  int _vcs;

  int *_credits;
  int *_presence;
  int *_input;
  int *_inputVC;

  list<tWaiting *> *_waiting;
 
  eNextVCState *_state;

public:
  EventNextVCState() { };
  void _Init( const Configuration& config );

  EventNextVCState( const Configuration& config );
  EventNextVCState( const Configuration& config, 
		    Module *parent, const string& name );
  ~EventNextVCState( );

  eNextVCState GetState( int vc ) const;
  int GetPresence( int vc ) const;
  int GetCredits( int vc ) const;
  int GetInput( int vc ) const;
  int GetInputVC( int vc ) const;

  bool IsWaiting( int vc ) const;
  bool IsInputWaiting( int vc, int w_input, int w_vc ) const;

  void PushWaiting( int vc, tWaiting *w );
  void IncrWaiting( int vc, int w_input, int w_vc );
  tWaiting *PopWaiting( int vc );

  void SetState( int vc, eNextVCState state );
  void SetCredits( int vc, int value );
  void SetPresence( int vc, int value );
  void SetInput( int vc, int input );
  void SetInputVC( int vc, int in_vc );
};

class EventRouter : public Router {
  int _vcs;
  int _vc_size;

  int _vct;

  VC  **_vc;

  tRoutingFunction   _rf;

  EventNextVCState *_output_state;

  PipelineFIFO<Flit>   *_crossbar_pipe;
  PipelineFIFO<Credit> *_credit_pipe;

  queue<Flit *> *_input_buffer;
  queue<Flit *> *_output_buffer;

  queue<Credit *> *_in_cred_buffer;
  queue<Credit *> *_out_cred_buffer;

  struct tArrivalEvent {
    int  input;
    int  output;
    int  src_vc;
    int  dst_vc;
    bool head;
    bool tail;
    
    int  id;    // debug
    bool watch; // debug
  };

  PipelineFIFO<tArrivalEvent> *_arrival_pipe;
  queue<tArrivalEvent *>      *_arrival_queue;
  PriorityArbiter             **_arrival_arbiter;

  struct tTransportEvent {
    int  input;
    int  src_vc;
    int  dst_vc;

    int  id;    // debug
    bool watch; // debug
  };

  queue<tTransportEvent *> *_transport_queue;
  PriorityArbiter          **_transport_arbiter;

  bool *_transport_free;
  int  *_transport_match;

  void _ReceiveFlits( );
  void _ReceiveCredits( );

  void _IncomingFlits( );
  void _ArrivalRequests( int input );
  void _ArrivalArb( int output );
  void _SendTransport( int input, int output, tArrivalEvent *aevt );
  void _ProcessWaiting( int output, int out_vc );
  void _TransportRequests( int output );
  void _TransportArb( int input );
  void _OutputQueuing( );

  void _SendFlits( );
  void _SendCredits( );

public:
  EventRouter( const Configuration& config,
	       Module *parent, const string & name, int id,
	       int inputs, int outputs );
  virtual ~EventRouter( );

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
