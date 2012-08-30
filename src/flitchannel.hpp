// $Id: flitchannel.hpp 1144 2009-03-06 07:13:09Z mebauer $

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

// ----------------------------------------------------------------------
//
//  File Name: flitchannel.hpp
//
//  The FlitChannel models a flit channel with a multi-cycle 
//   transmission delay. The channel latency can be specified as 
//   an integer number of simulator cycles.
// ----------------------------------------------------------------------

#ifndef FLITCHANNEL_HPP
#define FLITCHANNEL_HPP

// ----------------------------------------------------------------------
//  $Author: jbalfour $
//  $Date: 2007/06/27 23:10:17 $
//  $Id: flitchannel.hpp 1144 2009-03-06 07:13:09Z mebauer $
// ----------------------------------------------------------------------

#include "flit.hpp"
#include "globals.hpp"
#include <queue>
using namespace std;
class Router ;

class FlitChannel {

public:
  enum FlitChannelType {SEND  = 1,
		    RECEIVE = 2,
		    NORMAL = 0};
  FlitChannel();
  ~FlitChannel();

  void SetSource( Router* router ) ;
  int GetSource();
  void SetSink( Router* router ) ;
  int GetSink();

  //multithreading
  void SetShared(FlitChannelType t,int src, int sin);
  
  // Phsyical Parameters
  void SetLatency( int cycles ) ;
  int GetLatency() { return _delay ; }
  int* GetActivity(){return _active;}

  // Check for flit on input. Used for tracking channel use
  bool InUse();

  // Send flit 
  void SendFlit( Flit* flit );

  // Receive Flit
  Flit* ReceiveFlit( ); 

  // Peek at Flit
  Flit* PeekFlit( );

private:
  //multithreading
  bool shared;
  int valid;
  FlitChannelType type;

  int          _delay;
  queue<Flit*> _queue;
  
  ////////////////////////////////////////
  //
  // Power Models OBSOLETE
  //
  ////////////////////////////////////////

  int _routerSource ;
  int _routerSink ;
  

  // Statistics for Activity Factors
  int          _active[Flit::NUM_FLIT_TYPES];
  int          _idle; 

  int send_buf_pos;
  int recv_buf_pos;
  Flit send_buffer [2];
  MPI_Request send_req [2];
  Flit recv_buffer [2];
  int send_align;
  int recv_align;
public:
  int          _cookie;
  int id;
  int sink_id;
  int source_id;
};

#endif
