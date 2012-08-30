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

#ifndef _TRAFFICMANAGER_HPP_
#define _TRAFFICMANAGER_HPP_

#include <list>
#include <map>
#include <set>
#include <stack>
#include <cassert>

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

//register the requests to a node
class PacketReplyInfo;

class TrafficManager : public Module {
protected:
  int _sources;
  int _dests;
  int _routers;

  vector<BSNetwork *> _net;
  vector<vector<Router *> > _router_map;

  // ============ Traffic ============ 

  int    _classes;

  vector<double> _load;

  vector<int>    _packet_size;

  /*false means all packet types are the same length "const_flits_per_packet"
   *All packets uses all VCS
   *packet types are generated randomly, essentially making it only 1 type
   *of packet in the network
   *
   *True means only request packets are generated and replies are generated
   *as a response to the requests, packets are now difference length, correspond
   *to "read_request_size" etc. 
   */
  vector<int> _use_read_write;

  vector<int> _read_request_size;
  vector<int> _read_reply_size;
  vector<int> _write_request_size;
  vector<int> _write_reply_size;

  vector<string> _traffic;

  vector<int> _class_priority;

  map<int, pair<int, vector<int> > > _class_prio_map;

  vector<tTrafficFunction> _traffic_function;
  vector<tInjectionProcess> _injection_process;

  // ============ Message priorities ============ 

  enum ePriority { class_based, age_based, network_age_based, local_age_based, queue_length_based, hop_count_based, sequence_based, none };

  ePriority _pri_type;

  // ============ Injection VC states  ============ 

  vector<vector<BufferState *> > _buf_states;
  vector<vector<vector<int> > > _last_vc;

  // ============ Routing ============ 

  tRoutingFunction _rf;

  // ============ Injection queues ============ 

  vector<vector<int> > _qtime;
  vector<vector<bool> > _qdrained;
  vector<vector<list<Flit *> > > _partial_packets;

  vector<map<int, Flit *> > _total_in_flight_flits;
  vector<map<int, Flit *> > _measured_in_flight_flits;
  vector<map<int, Flit *> > _retired_packets;
  bool _empty_network;

  // ============ physical sub-networks ==========

  int _subnets;

  vector<int> _subnet_map;

  // ============ deadlock ==========

  int _deadlock_timer;
  int _deadlock_warn_timeout;

  // ============ batch mode ==========================

  vector<int> _packets_sent;
  int _batch_size;
  int _batch_count;
  vector<list<int> > _repliesPending;
  map<int, PacketReplyInfo*> _repliesDetails;
  vector<int> _requestsOutstanding;
  int _maxOutstanding;
  bool _replies_inherit_priority;

  int _last_id;
  int _last_pid;

  // ============voq mode =============================

  vector<vector<list<Flit*> > > _voq;
  vector<list<int> > _active_list;
  vector<vector<bool> > _active_vc;
  
  // ============ Statistics ============

  vector<Stats *> _latency_stats;     
  vector<Stats *> _overall_min_latency;  
  vector<Stats *> _overall_avg_latency;  
  vector<Stats *> _overall_max_latency;  

  vector<Stats *> _tlat_stats;     
  vector<Stats *> _overall_min_tlat;  
  vector<Stats *> _overall_avg_tlat;  
  vector<Stats *> _overall_max_tlat;  

  vector<Stats *> _frag_stats;
  vector<Stats *> _overall_min_frag;
  vector<Stats *> _overall_avg_frag;
  vector<Stats *> _overall_max_frag;

  vector<vector<Stats *> > _pair_latency;
  vector<vector<Stats *> > _pair_tlat;
  vector<Stats *> _hop_stats;

  vector<vector<Stats *> > _sent_flits;
  vector<vector<Stats *> > _accepted_flits;
  vector<Stats *> _overall_accepted;
  vector<Stats *> _overall_accepted_min;
  
  Stats * _batch_time;
  Stats * _overall_batch_time;

  vector<int> _injected_flow;
  vector<int> _ejected_flow;
  vector<int> _received_flow;
  vector<int> _sent_flow;

  vector<int> _slowest_flit;

  map<string, Stats *> _stats;

  // ============ Simulation parameters ============ 

  enum eSimState { warming_up, running, draining, done };
  eSimState _sim_state;

  enum eSimMode { latency, throughput, batch };
  eSimMode _sim_mode;
  
  //batched time-mode, know what you are doing
  bool _timed_mode;

  int   _limit; //any higher clients do not generate packets

  int   _warmup_time;
  int   _drain_time;

  int   _total_sims;
  int   _sample_period;
  int   _max_samples;
  int   _warmup_periods;

  int   _include_queuing;

  vector<int> _measure_stats;

  vector<double> _latency_thres;

  vector<double> _stopping_threshold;
  vector<double> _acc_stopping_threshold;

  vector<double> _warmup_threshold;
  vector<double> _acc_warmup_threshold;

  int _cur_id;
  int _cur_pid;
  int _cur_tid;
  int _time;

  set<int> _flits_to_watch;
  set<int> _packets_to_watch;
  set<int> _transactions_to_watch;

  bool _print_csv_results;
  bool _print_vc_stats;
  bool _drain_measured_only;

  //flits to watch
  ostream * _stats_out;
  ostream * _flow_out;

  // ============ Internal methods ============ 
protected:
  virtual void _RetireFlit( Flit *f, int dest );

  void _Inject();
  void _Step( );

  bool _PacketsOutstanding( ) const;
  
  virtual int  _IssuePacket( int source, int cl );
  virtual void _GeneratePacket( int source, int size, int cl, int time );

  void _ClearStats( );

  int  _ComputeStats( const vector<Stats *> & stats, double *avg, double *min ) const;

  virtual bool _SingleSim( );

  void _DisplayRemaining( ) const;
  
  void _LoadWatchList(const string & filename);

public:
  TrafficManager( const Configuration &config, const vector<BSNetwork *> & net );
  ~TrafficManager( );

  bool Run( );

  virtual void DisplayStats();
  virtual void printPartialStats(int t, int i){}

  const Stats * GetOverallLatency(int c = 0) { return _overall_avg_latency[c]; }
  const Stats * GetAccepted(int c = 0) { return _overall_accepted[c]; }
  const Stats * GetAcceptedMin(int c = 0) { return _overall_accepted_min[c]; }
  const Stats * GetHops(int c = 0) { return _hop_stats[c]; }

  inline int getTime() { return _time;}
  Stats * getStats(const string & name) { return _stats[name]; }

};

#endif
