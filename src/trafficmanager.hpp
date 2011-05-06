// $Id$

/*
Copyright (c) 2007-2011, Trustees of The Leland Stanford Junior University
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
  int _nodes;
  int _routers;

  vector<Network *> _net;
  vector<vector<Router *> > _router;

  // ============ Traffic ============ 

  int    _classes;

  vector<double> _load;

  int _subnets;

  vector<int> _subnet;

  vector<int> _packet_size;

  vector<int> _reply_class;
  vector<int> _request_class;

  vector<string> _traffic;

  vector<int> _class_priority;

  vector<int> _last_class;

  vector<TrafficPattern *> _traffic_pattern;
  vector<vector<InjectionProcess *> > _injection_process;

  // ============ Message priorities ============ 

  enum ePriority { class_based, age_based, trans_age_based, network_age_based, local_age_based, queue_length_based, hop_count_based, sequence_based, none };

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

  // ============ deadlock ==========

  int _deadlock_timer;
  int _deadlock_warn_timeout;

  // ============ batch mode ==========================

  vector<vector<int> > _sent_packets;
  vector<int> _batch_size;
  int _batch_count;
  vector<vector<int> > _requests_outstanding;
  vector<int> _max_outstanding;

  int _last_id;
  int _last_pid;

  // ============ Statistics ============

  vector<Stats *> _plat_stats;     
  vector<Stats *> _overall_min_plat;  
  vector<Stats *> _overall_avg_plat;  
  vector<Stats *> _overall_max_plat;  

  vector<Stats *> _tlat_stats;     
  vector<Stats *> _overall_min_tlat;  
  vector<Stats *> _overall_avg_tlat;  
  vector<Stats *> _overall_max_tlat;  

  vector<Stats *> _frag_stats;
  vector<Stats *> _overall_min_frag;
  vector<Stats *> _overall_avg_frag;
  vector<Stats *> _overall_max_frag;

  vector<vector<Stats *> > _pair_plat;
  vector<vector<Stats *> > _pair_tlat;
  vector<Stats *> _hop_stats;

  vector<vector<Stats *> > _sent_flits;
  vector<vector<Stats *> > _accepted_flits;
  vector<Stats *> _overall_accepted;
  vector<Stats *> _overall_accepted_min;
  
  Stats * _batch_time;
  Stats * _overall_batch_time;

  vector<int> _slowest_flit;

  map<string, Stats *> _stats;

  // ============ Simulation parameters ============ 

  enum eSimState { warming_up, running, draining, done };
  eSimState _sim_state;

  enum eSimMode { latency, throughput, batch };
  eSimMode _sim_mode;
  
  //batched time-mode, know what you are doing
  bool _timed_mode;

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
  bool _drain_measured_only;

  //flits to watch
  ostream * _stats_out;

  bool _flow_out;

  ostream * _sent_packets_out;
  ostream * _active_packets_out;
  ostream * _injected_flits_out;
  ostream * _ejected_flits_out;
  ostream * _received_flits_out;
  ostream * _sent_flits_out;
  ostream * _stored_flits_out;

  // ============ Internal methods ============ 
protected:
  void _RetireFlit( Flit *f, int dest );

  void _Inject();
  void _Step( );

  bool _PacketsOutstanding( ) const;
  
  bool _IssuePacket( int source, int cl );
  void _GeneratePacket( int source, int dest, int size, int cl, int time, int tid, int ttime );

  void _ClearStats( );

  int  _ComputeStats( const vector<Stats *> & stats, double *avg, double *min ) const;

  bool _SingleSim( );

  void _DisplayRemaining( ostream & os = cout ) const;
  
  void _LoadWatchList(const string & filename);

public:
  TrafficManager( const Configuration &config, const vector<Network *> & net );
  ~TrafficManager( );

  bool Run( );

  void UpdateOverallStats();

  void DisplayStats( ostream & os = cout ) const ;
  void DisplayOverallStats( ostream & os = cout ) const ;

  const Stats * GetOverallLatency(int c = 0) { return _overall_avg_plat[c]; }
  const Stats * GetAccepted(int c = 0) { return _overall_accepted[c]; }
  const Stats * GetAcceptedMin(int c = 0) { return _overall_accepted_min[c]; }
  const Stats * GetHops(int c = 0) { return _hop_stats[c]; }

  inline int getTime() { return _time;}
  Stats * getStats(const string & name) { return _stats[name]; }

};

template<class T>
ostream & operator<<(ostream & os, const vector<T> & v) {
  for(size_t i = 0; i < v.size() - 1; ++i) {
    os << v[i] << ",";
  }
  os << v[v.size()-1];
  return os;
}

#endif
