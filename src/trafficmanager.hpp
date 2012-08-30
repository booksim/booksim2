// $Id: trafficmanager.hpp 1101 2009-02-19 02:16:32Z qtedq $

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

#ifndef _TRAFFICMANAGER_HPP_
#define _TRAFFICMANAGER_HPP_

#include <list>
#include<map>

#include "module.hpp"
#include "config_utils.hpp"
#include "network.hpp"
#include "flit.hpp"
#include "buffer_state.hpp"
#include "stats.hpp"
#include "traffic.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "injection.hpp"
#include <assert.h>

//register the requests to a node
struct Packet_Reply {
  int source;
  int time;
  Flit::FlitType type;
};

class TrafficManager : public Module {
protected:
  int _sources;
  int _dests;

  Network **_net;

  // ============ Message priorities ============ 

  enum ePriority { class_based, age_based, none };

  ePriority _pri_type;
  int       _classes;

  // ============ Injection VC states  ============ 

  BufferState ***_buf_states;

  // ============ Injection queues ============ 

  int          _voqing;
  int          **_qtime;
  bool         **_qdrained;
  list<Flit *> ***_partial_packets;

  int                 _measured_in_flight;
  int                 _total_in_flight;
  map<int,bool> _in_flight;
  bool                _empty_network;
  bool _use_lagging;

  // ============ sub-networks and deadlock ==========

  short duplicate_networks;
  unsigned char deadlock_counter;

  // ============ batch mode ==========================
  int *_packets_sent;
  int _batch_size;
  list<int>* _repliesPending;
  map<int,Packet_Reply*> _repliesDetails;
  int * _requestsOutstanding;
  int _maxOutstanding;

  // ============voq mode =============================
  list<Flit*> ** _voq;
  list<int>* _active_list;
  bool** _active_vc;
  
  // ============ Statistics ============

  Stats **_latency_stats;     
  Stats **_overall_latency;  

  Stats **_pair_latency;
  Stats *_hop_stats;

  Stats **_accepted_packets;
  Stats *_overall_accepted;
  Stats *_overall_accepted_min;

  // ============ Simulation parameters ============ 

  enum eSimState { warming_up, running, draining, done };
  eSimState _sim_state;

  enum eSimMode { latency, throughput, batch };
  eSimMode _sim_mode;

  int   _limit; //any higher clients do not generate packets

  int   _warmup_time;
  int   _drain_time;

  float _load;
  float _flit_rate;

  int   _packet_size;
  int _read_request_size;
  int _read_reply_size;
  int _write_request_size;
  int _write_reply_size;

  int   _total_sims;
  int   _sample_period;
  int   _max_samples;
  int   _warmup_periods;

  short sub_network;

  int   _include_queuing;

  double _latency_thres;

  float _internal_speedup;
  float *_partial_internal_cycles;

  int _cur_id;
  int _time;

  list<Flit *> _used_flits;
  list<Flit *> _free_flits;

  tTrafficFunction  _traffic_function;
  tRoutingFunction  _routing_function;
  tInjectionProcess _injection_process;

  map<int,bool> flits_to_watch;

  bool _print_csv_results;
  string _traffic;
  bool _drain_measured_only;

  // ============ Internal methods ============ 
protected:
  virtual Flit *_NewFlit( );
  virtual void _RetireFlit( Flit *f, int dest );

  virtual void _FirstStep( );
  void _NormalInject();
  void _BatchInject();
  void _Step( );

  bool _PacketsOutstanding( ) const;
  
  virtual int  _IssuePacket( int source, int cl ) const;
  virtual void _GeneratePacket( int source, int size, int cl, int time );

  void _ClearStats( );

  virtual int  _ComputeAccepted( double *avg, double *min ) const;

  virtual bool _SingleSim( );

  int DivisionAlgorithm(int packet_type);

  void _DisplayRemaining( ) const;
  
  void _LoadWatchList();

public:
  TrafficManager( const Configuration &config, Network **net );
  ~TrafficManager( );

  bool Run( );

  void DisplayStats();

  const Stats * GetOverallLatency(int c) { return _overall_latency[c]; }
  const Stats * GetAccepted() { return _overall_accepted; }
  const Stats * GetAcceptedMin() { return _overall_accepted_min; }
  const Stats * GetHops() { return _hop_stats; }

  int getTime() { return _time;}
};

#endif
