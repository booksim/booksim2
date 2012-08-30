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

  vector<int> _received_flits;
  vector<int> _stored_flits;
  vector<int> _sent_flits;

  vector<int> _active_packets;

 
  virtual void _InternalStep() = 0;

public:

  map<int, bool> _port_congest_check;
  vector<double> _port_congestness;
  vector<bool>_vc_ecn;

  //stats
  vector<int> _vc_congested;
  vector<int> _ECN_activated;
  vector<int> _input_request;
  vector<int> _input_grant;
  vector<long> _vc_congested_sum;
  vector<int> _vc_activity;
  int _holds;
  int _hold_cancels;

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

  virtual bool GetCongest(int i) const = 0;
  virtual int GetCredit(int out, int vc_begin=-1, int vc_end=-1 ) const = 0;
  virtual int GetCommit(int out, int vc=-1) const = 0;
  virtual int GetCreditArray(int out, int* vcs, int vc_count, bool rtt, bool commit) const=0;
  virtual int GetBuffer(int i = -1) const = 0;

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

  inline void ResetStats() {
    _received_flits.assign(_received_flits.size(), 0);
    _sent_flits.assign(_sent_flits.size(), 0);
  }

  inline int NumOutputs() const {return _outputs;}
};

#endif
