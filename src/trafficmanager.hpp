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

#ifndef _TRAFFICMANAGER_HPP_
#define _TRAFFICMANAGER_HPP_

#include <list>
#include <map>
#include <set>

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
  int ttime;
  bool record;
  Flit::FlitType type;
};

class TrafficManager : public Module {
protected:
  unsigned int _sources;
  unsigned int _dests;
  unsigned int _routers;

  vector<Network *> _net;
  vector<vector<Router *> > _router_map;

  vector <Flit *> _flit_pool;

  // ============ Message priorities ============ 

  enum ePriority { class_based, age_based, network_age_based, local_age_based, queue_length_based, hop_count_based, sequence_based, none };

  ePriority _pri_type;
  int       _classes;

  // ============ Injection VC states  ============ 

  vector<vector<BufferState *> > _buf_states;

  // ============ Injection queues ============ 

  int          _voqing;
  vector<vector<int> > _qtime;
  vector<vector<bool> > _qdrained;
  vector<vector<vector<list<Flit *> > > > _partial_packets;

  map<int, Flit *> _measured_in_flight_flits;
  multimap<int, Flit *> _measured_in_flight_packets;
  map<int, Flit *> _total_in_flight_flits;
  multimap<int, Flit *> _total_in_flight_packets;
  bool                _empty_network;
  bool _use_lagging;

  // ============ sub-networks and deadlock ==========

  short _duplicate_networks;
  unsigned char _deadlock_counter;

  // ============ batch mode ==========================
  vector<int> _packets_sent;
  int _batch_size;
  int _batch_count;
  vector<list<int> > _repliesPending;
  map<int, Packet_Reply*> _repliesDetails;
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

  vector<Stats *> _pair_latency;
  vector<Stats *> _pair_tlat;
  Stats * _hop_stats;

  vector<Stats *> _sent_flits;
  vector<Stats *> _accepted_flits;
  Stats * _overall_accepted;
  Stats * _overall_accepted_min;
  
  Stats * _batch_time;
  Stats * _overall_batch_time;

  vector<unsigned int> _injected_flow;
  vector<unsigned int> _ejected_flow;
  vector<unsigned int> _received_flow;
  vector<unsigned int> _sent_flow;

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

  float _load;
  float _flit_rate;

  int   _packet_size;

  /*false means all packet types are the same length "gConstantsize"
   *All packets uses all VCS
   *packet types are generated randomly, essentially making it only 1 type
   *of packet in the network
   *
   *True means only request packets are generated and replies are generated
   *as a response to the requests, packets are now difference length, correspond
   *to "read_request_size" etc. 
   */
  bool _use_read_write;

  int _read_request_size;
  int _read_reply_size;
  int _write_request_size;
  int _write_reply_size;

  int   _total_sims;
  int   _sample_period;
  int   _max_samples;
  int   _warmup_periods;
  vector<vector<short> > _class_array;
  short _sub_network;

  int   _include_queuing;

  double _latency_thres;
  double _stopping_threshold;
  double _acc_stopping_threshold;
  double _warmup_threshold;

  float _internal_speedup;
  vector<float> _partial_internal_cycles;

  int _cur_id;
  int _cur_pid;
  int _time;

  list<Flit *> _used_flits;
  list<Flit *> _free_flits;

  tTrafficFunction  _traffic_function;
  tRoutingFunction  _routing_function;
  tInjectionProcess _injection_process;

  set<int> _flits_to_watch;
  set<int> _packets_to_watch;

  bool _print_csv_results;
  bool _print_vc_stats;
  string _traffic;
  bool _drain_measured_only;

  //flits to watch
  ostream * _stats_out;
  ostream * _flow_out;

  // ============ Internal methods ============ 
protected:
  virtual Flit *_NewFlit( );
  virtual void _RetireFlit( Flit *f, int dest );

  void _FirstStep( );
  void _NormalInject();
  void _BatchInject();
  void _Step( );

  bool _PacketsOutstanding( ) const;
  
  virtual int  _IssuePacket( int source, int cl );
  virtual void _GeneratePacket( int source, int size, int cl, int time );

  void _ClearStats( );

  int  _ComputeStats( const vector<Stats *> & stats, double *avg, double *min ) const;

  virtual bool _SingleSim( );

  int DivisionAlgorithm(int packet_type);

  void _DisplayRemaining( ) const;
  
  void _LoadWatchList(const string & filename);

public:
  TrafficManager( const Configuration &config, const vector<Network *> & net );
  ~TrafficManager( );

  bool Run( );

  void DisplayStats();

  const Stats * GetOverallLatency(int c) { return _overall_avg_latency[c]; }
  const Stats * GetAccepted() { return _overall_accepted; }
  const Stats * GetAcceptedMin() { return _overall_accepted_min; }
  const Stats * GetHops() { return _hop_stats; }

  inline int getTime() { return _time;}
  Stats * getStats(const string & name) { return _stats[name]; }

};

#endif
