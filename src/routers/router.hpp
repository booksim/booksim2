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

#ifndef _ROUTER_HPP_
#define _ROUTER_HPP_

#include <string>
#include <vector>

#include "timed_module.hpp"
#include "flit.hpp"
#include "credit.hpp"
#include "flitchannel.hpp"
#include "channel.hpp"
#include "config_utils.hpp"

typedef Channel<Credit> CreditChannel;

class Router : public TimedModule {

protected:

  static int const STALL_BUFFER_BUSY;
  static int const STALL_BUFFER_CONFLICT;
  static int const STALL_BUFFER_FULL;
  static int const STALL_BUFFER_RESERVED;
  static int const STALL_CROSSBAR_CONFLICT;

  int _id;
  
  int _inputs;
  int _outputs;
  
  int _input_speedup;
  int _output_speedup;
  
  double _internal_speedup;
  double _partial_internal_cycles;

  int _crossbar_delay;
  int _credit_delay;
  
  vector<FlitChannel *>   _input_channels;
  vector<CreditChannel *> _input_credits;
  vector<FlitChannel *>   _output_channels;
  vector<CreditChannel *> _output_credits;
  vector<bool>            _channel_faults;

#ifdef TRACK_FLOWS
  vector<int> _received_flits;
  vector<int> _stored_flits;
  vector<int> _sent_flits;
  vector<int> _active_packets;
#endif

#ifdef TRACK_STALLS
  int _buffer_busy_stalls;
  int _buffer_conflict_stalls;
  int _buffer_full_stalls;
  int _buffer_reserved_stalls;
  int _crossbar_conflict_stalls;
#endif

  virtual void _InternalStep() = 0;

public:
  Router( const Configuration& config,
	  Module *parent, const string & name, int id,
	  int inputs, int outputs );

  static Router *NewRouter( const Configuration& config,
			    Module *parent, const string & name, int id,
			    int inputs, int outputs );

  void AddInputChannel( FlitChannel *channel, CreditChannel *backchannel );
  void AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel );
 
  inline FlitChannel * GetInputChannel( int input ) const {
    assert((input >= 0) && (input < _inputs));
    return _input_channels[input];
  }
  inline FlitChannel * GetOutputChannel( int output ) const {
    assert((output >= 0) && (output < _outputs));
    return _output_channels[output];
  }

  virtual void ReadInputs( ) = 0;
  virtual void Evaluate( );
  virtual void WriteOutputs( ) = 0;

  void OutChannelFault( int c, bool fault = true );
  bool IsFaultyOutput( int c ) const;

  inline int GetID( ) const {return _id;}


  virtual int GetUsedCredit(int out, int vc_begin = -1, int vc_end = -1 ) const = 0;
  virtual int GetBuffer(int i = -1) const = 0;
  virtual double GetROC(int out, int vc_begin = -1, int vc_end = -1) const {return 0.0;}
  virtual double GetDrain(int out, int vc_begin = -1, int vc_end = -1) const {return 0.0;}
  virtual double GetArrival(int out, int vc_begin = -1, int vc_end = -1) const {return 0.0;}



#ifdef TRACK_FLOWS
  inline vector<int> const & GetReceivedFlits() const {
    return _received_flits;
  }
  inline vector<int> const & GetStoredFlits() const {
    return _stored_flits;
  }
  inline vector<int> const & GetSentFlits() const {
    return _sent_flits;
  }
  
  inline vector<int> const & GetActivePackets() const {
    return _active_packets;
  }

  inline void ResetFlowStats() {
    _received_flits.assign(_received_flits.size(), 0);
    _sent_flits.assign(_sent_flits.size(), 0);
  }
#endif

#ifdef TRACK_STALLS
  inline int GetBufferBusyStalls() const {
    return _buffer_busy_stalls;
  }
  inline int GetBufferConflictStalls() const {
    return _buffer_conflict_stalls;
  }
  inline int GetBufferFullStalls() const {
    return _buffer_full_stalls;
  }
  inline int GetBufferReservedStalls() const {
    return _buffer_reserved_stalls;
  }
  inline int GetCrossbarConflictStalls() const {
    return _crossbar_conflict_stalls;
  }

  inline void ResetStallStats() {
    _buffer_busy_stalls = 0;
    _buffer_conflict_stalls = 0;
    _buffer_full_stalls = 0;
    _buffer_reserved_stalls = 0;
    _crossbar_conflict_stalls = 0;
  }
#endif

  inline int NumOutputs() const {return _outputs;}
};

#endif
