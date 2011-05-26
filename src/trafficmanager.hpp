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

#include "module.hpp"
#include "config_utils.hpp"
#include "network.hpp"
#include "flit.hpp"
#include "buffer_state.hpp"
#include "stats.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"

class TrafficManager : public Module {

protected:

  int _nodes;
  int _routers;

  vector<Network *> _net;
  vector<vector<Router *> > _router;

  // ============ Traffic ============ 

  int _classes;

  int _subnets;
  vector<int> _subnet;

  vector<int> _class_priority;

  vector<int> _last_class;

  // ============ Message priorities ============ 

  enum ePriority { class_based, age_based, trans_age_based, network_age_based, local_age_based, queue_length_based, hop_count_based, sequence_based, none };

  ePriority _pri_type;

  // ============ Injection VC states  ============ 

  vector<vector<BufferState *> > _buf_states;
  vector<vector<vector<int> > > _last_vc;

  // ============ Routing ============ 

  tRoutingFunction _rf;

  // ============ Injection queues ============ 

  vector<vector<list<Flit *> > > _partial_packets;

  vector<map<int, Flit *> > _total_in_flight_flits;
  vector<map<int, Flit *> > _measured_in_flight_flits;
  vector<map<int, Flit *> > _retired_packets;

  bool _empty_network;

  // ============ deadlock ==========

  int _deadlock_timer;
  int _deadlock_warn_timeout;

  // ============ request & replies ==========================

  vector<vector<int> > _sent_packets;
  vector<vector<int> > _requests_outstanding;

  // ============ Statistics ============

  vector<int> _generated_flit_count;
  vector<int> _injected_flit_count;
  vector<int> _ejected_flit_count;

  vector<Stats *> _plat_stats;     
  vector<Stats *> _overall_min_plat;  
  vector<Stats *> _overall_avg_plat;  
  vector<Stats *> _overall_max_plat;  

  vector<Stats *> _frag_stats;
  vector<Stats *> _overall_min_frag;
  vector<Stats *> _overall_avg_frag;
  vector<Stats *> _overall_max_frag;

  vector<vector<Stats *> > _pair_plat;

  vector<Stats *> _hop_stats;
  vector<Stats *> _overall_hop_stats;

  vector<vector<Stats *> > _offered_flits;
  vector<Stats *> _overall_min_offered;
  vector<Stats *> _overall_avg_offered;
  vector<Stats *> _overall_max_offered;
  vector<vector<Stats *> > _sent_flits;
  vector<Stats *> _overall_min_sent;
  vector<Stats *> _overall_avg_sent;
  vector<Stats *> _overall_max_sent;
  vector<vector<Stats *> > _accepted_flits;
  vector<Stats *> _overall_min_accepted;
  vector<Stats *> _overall_avg_accepted;
  vector<Stats *> _overall_max_accepted;
  
  vector<int> _slowest_flit;

  map<string, Stats *> _stats;

  // ============ Simulation parameters ============ 

  enum eSimState { warming_up, running, draining, done };
  eSimState _sim_state;

  int   _drain_time;

  int   _total_sims;

  int   _include_queuing;

  vector<int> _measure_stats;

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

#ifdef TRACK_FLOWS
  ostream * _active_packets_out;
  ostream * _generated_flits_out;
  ostream * _injected_flits_out;
  ostream * _ejected_flits_out;
  ostream * _received_flits_out;
  ostream * _sent_flits_out;
  ostream * _stored_flits_out;
#endif


  // ============ Internal methods ============ 

  virtual void _RetireFlit( Flit *f, int dest );
  virtual void _RetirePacket( Flit * head, Flit * tail, int dest );

  virtual void _Inject() = 0;
  
  void _Step( );

  virtual bool _PacketsOutstanding( ) const;
  
  virtual bool _IssuePacket( int source, int cl ) = 0;

  void _GeneratePacket( int source, int dest, int size, int cl, int time, int tid = -1, int ttime = -1 );

  virtual void _ResetSim( );

  virtual void _ClearStats( );

  bool _ComputeStats( const vector<Stats *> & stats, double *avg, double *min = NULL, double *max = NULL, int *min_pos = NULL, int *max_pos = NULL ) const;

  virtual bool _SingleSim( ) = 0;

  void _DisplayRemaining( ostream & os = cout ) const;
  
  void _LoadWatchList(const string & filename);

  virtual void _UpdateOverallStats();

  virtual string _OverallStatsHeaderCSV() const;
  virtual string _OverallClassStatsCSV(int c) const;

  virtual void _DisplayClassStats( int c, ostream & os ) const ;
  virtual void _WriteClassStats( int c, ostream & os ) const ;
  virtual void _DisplayOverallClassStats( int c, ostream & os ) const ;

  TrafficManager( const Configuration &config, const vector<Network *> & net );

public:

  virtual ~TrafficManager( );

  static TrafficManager * NewTrafficManager(Configuration const & config, 
					    vector<Network *> const & net);

  bool Run( );

  void DisplayStats(ostream & os = cout) const;
  void WriteStats(ostream & os = cout) const;
  void DisplayOverallStats(ostream & os = cout) const;
  void DisplayOverallStatsCSV(ostream & os = cout) const;

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
