// $Id: creditchannel.hpp 1144 2009-03-06 07:13:09Z mebauer $

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

//////////////////////////////////////////////////////////////////////
//
//  File Name: creditchannel.hpp
//  Author: James Balfour, Rebecca Schultz
//
//  The CreditChannel models a credit channel with a multi-cycle 
//   transmission delay. The channel latency can be specified as 
//   an integer number of simulator cycles.
//
/////
#ifndef CREDITCHANNEL_HPP
#define CREDITCHANNEL_HPP

#include "credit.hpp"
#include <queue>
#include "globals.hpp"
 //#include "lfqueue.hpp"
 //#include "normqueue.hpp"
#include <pthread.h>
using namespace std;

class CreditChannel {
public:
  enum CreditChannelType {SEND  = 1,
			RECEIVE = 2,
			NORMAL = 0};
  CreditChannel();
  ~CreditChannel();
  // Physical Parameters
  void SetLatency( int cycles );
  // Send credit 
  void SendCredit( Credit* credit );
  
  // Receive Credit
  Credit* ReceiveCredit( ); 

  // Peek at Credit
  Credit* PeekCredit( );

  //multithreading
  void SetShared(CreditChannelType t,int src, int sin);

private:
  int            _delay;
  queue<Credit*> _queue;

  //multithreading
  bool shared;
  int valid;
  CreditChannelType type;

  ////////////////////////////////////////
  //
  // Power Models
  //
  ////////////////////////////////////////


  // Statistics for Activity Factors
  int    _active;
  int    _idle;

  int send_buf_pos;
  int recv_buf_pos;
  Credit send_buffer [2];
  MPI_Request send_req [2];
  Credit recv_buffer [2];
  int send_align;
  int recv_align;
  bool outstanding_req;
public:
  int id;
  int sink_id;
  int source_id;
};

#endif
