// $Id$

/*
 Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
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

#ifndef _FLIT_HPP_
#define _FLIT_HPP_

#include <iostream>
#include <stack>

#include "booksim.hpp"
#include "outputset.hpp"
#include "wkld_msg.hpp"

class Flit {

public:

  const static int NUM_FLIT_TYPES = 5;
  enum FlitType { READ_REQUEST  = 0,
      READ_REPLY    = 1,
      WRITE_REQUEST = 2,
      WRITE_REPLY   = 3,
      ANY_TYPE      = 4,
      CTRL_TYPE     = 5,
      RGET_REQUEST  = 6,
      RGET_GET_REQUEST = 7,
      RGET_GET_REPLY = 8,
      WRITE_REQUEST_NOOP = 9,
      NUM_TYPES = 10
  };
  FlitType type;

  int vc;

  int cl;

  bool head;
  bool tail;

  int64_t  ctime;
  int64_t  itime;
  int64_t  first_itime;
  int64_t  atime;

  int64_t  id;
  int64_t  pid;

  bool record;

  int  src;
  int  dest;
  int debug_dest;

  int  pri;

  int  hops;
  bool watch;
  int  subnetwork;

  bool non_duplicate_ack;

  bool ecn_congestion_detected;

  // Need size so the dest can ensure entire packet was received.
  int size;
  int packet_seq_num;
  int ack_seq_num;
  int nack_seq_num;
  int response_to_seq_num;
  int transmit_attempts;
  int read_requested_data_size;
  bool ack_received;
  bool response_received;
  int expire_time;
  int ack_received_time;

  // Selective ACK
  bool sack;
  unsigned int sack_vec;

  // intermediate destination (if any)
  mutable int intm;

  // phase in multi-phase algorithms
  mutable int ph;

  // Fields for arbitrary data
  //void* data ;
  WorkloadMessagePtr data; // for swm

  // Lookahead route info
  OutputSet la_route_set;

  void Reset();

  static Flit * New();
  void Free();
  static void FreeAll();

  void copy(Flit * flit);
  void copy_target(Flit * flit);

private:

  Flit();
  ~Flit() {}

  static stack<Flit *> _all;
  static stack<Flit *> _free;

};

ostream& operator<<( ostream& os, const Flit& f );

#endif
