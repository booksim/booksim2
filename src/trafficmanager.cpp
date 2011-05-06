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

#include <sstream>
#include <cmath>
#include <fstream>
#include <limits>
#include <cstdlib>

#include "booksim.hpp"
#include "booksim_config.hpp"
#include "trafficmanager.hpp"
#include "random_utils.hpp" 
#include "vc.hpp"


TrafficManager::TrafficManager( const Configuration &config, const vector<Network *> & net )
: Module( 0, "traffic_manager" ), _net(net), _empty_network(false), _deadlock_timer(0), _last_id(-1), _last_pid(-1), _timed_mode(false), _warmup_time(-1), _drain_time(-1), _cur_id(0), _cur_pid(0), _cur_tid(0), _time(0)
{

  _nodes = _net[0]->NumNodes( );
  _routers = _net[0]->NumRouters( );

  _subnets = config.GetInt("subnets");
 
  // ============ Message priorities ============ 

  string priority = config.GetStr( "priority" );

  if ( priority == "class" ) {
    _pri_type = class_based;
  } else if ( priority == "age" ) {
    _pri_type = age_based;
  } else if ( priority == "trans_age" ) {
    _pri_type = trans_age_based;
  } else if ( priority == "network_age" ) {
    _pri_type = network_age_based;
  } else if ( priority == "local_age" ) {
    _pri_type = local_age_based;
  } else if ( priority == "queue_length" ) {
    _pri_type = queue_length_based;
  } else if ( priority == "hop_count" ) {
    _pri_type = hop_count_based;
  } else if ( priority == "sequence" ) {
    _pri_type = sequence_based;
  } else if ( priority == "none" ) {
    _pri_type = none;
  } else {
    Error( "Unkown priority value: " + priority );
  }

  // ============ Routing ============ 

  string rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
  map<string, tRoutingFunction>::const_iterator rf_iter = gRoutingFunctionMap.find(rf);
  if(rf_iter == gRoutingFunctionMap.end()) {
    Error("Invalid routing function: " + rf);
  }
  _rf = rf_iter->second;

  // ============ Traffic ============ 

  _classes = config.GetInt("classes");

  _packet_size = config.GetIntArray( "packet_size" );
  if(_packet_size.empty()) {
    _packet_size.push_back(config.GetInt("packet_size"));
  }
  _packet_size.resize(_classes, _packet_size.back());
  
  _load = config.GetFloatArray("injection_rate"); 
  if(_load.empty()) {
    _load.push_back(config.GetFloat("injection_rate"));
  }
  _load.resize(_classes, _load.back());

  if(config.GetInt("injection_rate_uses_flits")) {
    for(int c = 0; c < _classes; ++c)
      _load[c] /= (double)_packet_size[c];
  }

  _subnet = config.GetIntArray("subnet"); 
  if(_subnet.empty()) {
    _subnet.push_back(config.GetInt("subnet"));
  }
  _subnet.resize(_classes, _subnet.back());

  _reply_class = config.GetIntArray("reply_class"); 
  if(_reply_class.empty()) {
    _reply_class.push_back(config.GetInt("reply_class"));
  }
  _reply_class.resize(_classes, _reply_class.back());

  _request_class.resize(_classes, -1);
  for(int c = 0; c < _classes; ++c) {
    int const & reply_class = _reply_class[c];
    if(reply_class >= 0) {
      assert(_request_class[reply_class] < 0);
      _request_class[reply_class] = c;
    }
  }

  _traffic = config.GetStrArray("traffic");
  _traffic.resize(_classes, _traffic.back());

  _traffic_pattern.resize(_classes);
  for(int c = 0; c < _classes; ++c) {
    _traffic_pattern[c] = TrafficPattern::New(_traffic[c], _nodes, config);
  }

  _class_priority = config.GetIntArray("class_priority"); 
  if(_class_priority.empty()) {
    _class_priority.push_back(config.GetInt("class_priority"));
  }
  _class_priority.resize(_classes, _class_priority.back());

  _last_class.resize(_nodes, -1);

  // ============ Injection VC states  ============ 

  vector<string> injection_process = config.GetStrArray("injection_process");
  injection_process.resize(_classes, injection_process.back());

  _injection_process.resize(_nodes);
  _buf_states.resize(_nodes);
  _last_vc.resize(_nodes);

  for ( int source = 0; source < _nodes; ++source ) {
    _injection_process[source].resize(_classes);
    for(int c = 0; c < _classes; ++c) {
      _injection_process[source][c] = InjectionProcess::New(injection_process[c], _load[c]);
    }
    _buf_states[source].resize(_subnets);
    _last_vc[source].resize(_subnets);
    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
      ostringstream tmp_name;
      tmp_name << "terminal_buf_state_" << source << "_" << subnet;
      _buf_states[source][subnet] = new BufferState( config, this, tmp_name.str( ) );
      _last_vc[source][subnet] = gEndVCs;
    }
  }

  // ============ Injection queues ============ 

  _qtime.resize(_nodes);
  _qdrained.resize(_nodes);
  _partial_packets.resize(_nodes);
  _sent_packets.resize(_nodes);
  _requests_outstanding.resize(_nodes);

  for ( int source = 0; source < _nodes; ++source ) {
    _qtime[source].resize(_classes);
    _qdrained[source].resize(_classes);
    _partial_packets[source].resize(_classes);
    _sent_packets[source].resize(_classes);
    _requests_outstanding[source].resize(_classes);
  }

  _total_in_flight_flits.resize(_classes);
  _measured_in_flight_flits.resize(_classes);
  _retired_packets.resize(_classes);

  _max_outstanding = config.GetIntArray("max_outstanding_requests");
  if(_max_outstanding.empty()) {
    _max_outstanding.push_back(config.GetInt("max_outstanding_requests"));
  }
  _max_outstanding.resize(_classes, _max_outstanding.back());

  _batch_size = config.GetIntArray("batch_size");
  if(_batch_size.empty()) {
    _batch_size.push_back(config.GetInt("batch_size"));
  }
  _batch_size.resize(_classes, _batch_size.back());

  _batch_count = config.GetInt( "batch_count" );

  // ============ Statistics ============ 

  _plat_stats.resize(_classes);
  _overall_min_plat.resize(_classes);
  _overall_avg_plat.resize(_classes);
  _overall_max_plat.resize(_classes);

  _tlat_stats.resize(_classes);
  _overall_min_tlat.resize(_classes);
  _overall_avg_tlat.resize(_classes);
  _overall_max_tlat.resize(_classes);

  _frag_stats.resize(_classes);
  _overall_min_frag.resize(_classes);
  _overall_avg_frag.resize(_classes);
  _overall_max_frag.resize(_classes);

  _pair_plat.resize(_classes);
  _pair_tlat.resize(_classes);
  
  _hop_stats.resize(_classes);
  
  _sent_flits.resize(_classes);
  _accepted_flits.resize(_classes);
  
  _overall_accepted.resize(_classes);
  _overall_accepted_min.resize(_classes);

  for ( int c = 0; c < _classes; ++c ) {
    ostringstream tmp_name;
    tmp_name << "plat_stat_" << c;
    _plat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _plat_stats[c];
    tmp_name.str("");

    tmp_name << "overall_min_plat_stat_" << c;
    _overall_min_plat[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_min_plat[c];
    tmp_name.str("");  
    tmp_name << "overall_avg_plat_stat_" << c;
    _overall_avg_plat[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_avg_plat[c];
    tmp_name.str("");  
    tmp_name << "overall_max_plat_stat_" << c;
    _overall_max_plat[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_max_plat[c];
    tmp_name.str("");  

    tmp_name << "tlat_stat_" << c;
    _tlat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _tlat_stats[c];
    tmp_name.str("");

    tmp_name << "overall_min_tlat_stat_" << c;
    _overall_min_tlat[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_min_tlat[c];
    tmp_name.str("");  
    tmp_name << "overall_avg_tlat_stat_" << c;
    _overall_avg_tlat[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_avg_tlat[c];
    tmp_name.str("");  
    tmp_name << "overall_max_tlat_stat_" << c;
    _overall_max_tlat[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_max_tlat[c];
    tmp_name.str("");  

    tmp_name << "frag_stat_" << c;
    _frag_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 100 );
    _stats[tmp_name.str()] = _frag_stats[c];
    tmp_name.str("");
    tmp_name << "overall_min_frag_stat_" << c;
    _overall_min_frag[c] = new Stats( this, tmp_name.str( ), 1.0, 100 );
    _stats[tmp_name.str()] = _overall_min_frag[c];
    tmp_name.str("");
    tmp_name << "overall_avg_frag_stat_" << c;
    _overall_avg_frag[c] = new Stats( this, tmp_name.str( ), 1.0, 100 );
    _stats[tmp_name.str()] = _overall_avg_frag[c];
    tmp_name.str("");
    tmp_name << "overall_max_frag_stat_" << c;
    _overall_max_frag[c] = new Stats( this, tmp_name.str( ), 1.0, 100 );
    _stats[tmp_name.str()] = _overall_max_frag[c];
    tmp_name.str("");

    tmp_name << "hop_stat_" << c;
    _hop_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 20 );
    _stats[tmp_name.str()] = _hop_stats[c];
    tmp_name.str("");

    _pair_plat[c].resize(_nodes*_nodes);
    _pair_tlat[c].resize(_nodes*_nodes);

    _sent_flits[c].resize(_nodes);
    _accepted_flits[c].resize(_nodes);
    
    for ( int i = 0; i < _nodes; ++i ) {
      tmp_name << "sent_stat_" << c << "_" << i;
      _sent_flits[c][i] = new Stats( this, tmp_name.str( ) );
      _stats[tmp_name.str()] = _sent_flits[c][i];
      tmp_name.str("");    
      
      for ( int j = 0; j < _nodes; ++j ) {
	tmp_name << "pair_plat_stat_" << c << "_" << i << "_" << j;
	_pair_plat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
	_stats[tmp_name.str()] = _pair_plat[c][i*_nodes+j];
	tmp_name.str("");
	
	tmp_name << "pair_tlat_stat_" << c << "_" << i << "_" << j;
	_pair_tlat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
	_stats[tmp_name.str()] = _pair_tlat[c][i*_nodes+j];
	tmp_name.str("");
      }
    }
    
    for ( int i = 0; i < _nodes; ++i ) {
      tmp_name << "accepted_stat_" << c << "_" << i;
      _accepted_flits[c][i] = new Stats( this, tmp_name.str( ) );
      _stats[tmp_name.str()] = _accepted_flits[c][i];
      tmp_name.str("");    
    }
    
    tmp_name << "overall_acceptance_" << c;
    _overall_accepted[c] = new Stats( this, tmp_name.str( ) );
    _stats[tmp_name.str()] = _overall_accepted[c];
    tmp_name.str("");

    tmp_name << "overall_min_acceptance_" << c;
    _overall_accepted_min[c] = new Stats( this, tmp_name.str( ) );
    _stats[tmp_name.str()] = _overall_accepted_min[c];
    tmp_name.str("");
    
  }

  _batch_time = new Stats( this, "batch_time" );
  _stats["batch_time"] = _batch_time;
  
  _overall_batch_time = new Stats( this, "overall_batch_time" );
  _stats["overall_batch_time"] = _overall_batch_time;
  
  _slowest_flit.resize(_classes, -1);

  // ============ Simulation parameters ============ 

  _total_sims = config.GetInt( "sim_count" );

  _router.resize(_subnets);
  for (int i=0; i < _subnets; ++i) {
    _router[i] = _net[i]->GetRouters();
  }

  //seed the network
  RandomSeed(config.GetInt("seed"));

  string sim_type = config.GetStr( "sim_type" );

  if ( sim_type == "latency" ) {
    _sim_mode = latency;
  } else if ( sim_type == "throughput" ) {
    _sim_mode = throughput;
  }  else if ( sim_type == "batch" ) {
    _sim_mode = batch;
  }  else if (sim_type == "timed_batch"){
    _sim_mode = batch;
    _timed_mode = true;
  }
  else {
    Error( "Unknown sim_type value : " + sim_type );
  }

  _sample_period = config.GetInt( "sample_period" );
  _max_samples    = config.GetInt( "max_samples" );
  _warmup_periods = config.GetInt( "warmup_periods" );

  _measure_stats = config.GetIntArray( "measure_stats" );
  if(_measure_stats.empty()) {
    _measure_stats.push_back(config.GetInt("measure_stats"));
  }
  _measure_stats.resize(_classes, _measure_stats.back());

  _latency_thres = config.GetFloatArray( "latency_thres" );
  if(_latency_thres.empty()) {
    _latency_thres.push_back(config.GetFloat("latency_thres"));
  }
  _latency_thres.resize(_classes, _latency_thres.back());

  _warmup_threshold = config.GetFloatArray( "warmup_thres" );
  if(_warmup_threshold.empty()) {
    _warmup_threshold.push_back(config.GetFloat("warmup_thres"));
  }
  _warmup_threshold.resize(_classes, _warmup_threshold.back());

  _acc_warmup_threshold = config.GetFloatArray( "acc_warmup_thres" );
  if(_acc_warmup_threshold.empty()) {
    _acc_warmup_threshold.push_back(config.GetFloat("acc_warmup_thres"));
  }
  _acc_warmup_threshold.resize(_classes, _acc_warmup_threshold.back());

  _stopping_threshold = config.GetFloatArray( "stopping_thres" );
  if(_stopping_threshold.empty()) {
    _stopping_threshold.push_back(config.GetFloat("stopping_thres"));
  }
  _stopping_threshold.resize(_classes, _stopping_threshold.back());

  _acc_stopping_threshold = config.GetFloatArray( "acc_stopping_thres" );
  if(_acc_stopping_threshold.empty()) {
    _acc_stopping_threshold.push_back(config.GetFloat("acc_stopping_thres"));
  }
  _acc_stopping_threshold.resize(_classes, _acc_stopping_threshold.back());

  _include_queuing = config.GetInt( "include_queuing" );

  _print_csv_results = config.GetInt( "print_csv_results" );
  _deadlock_warn_timeout = config.GetInt( "deadlock_warn_timeout" );
  _drain_measured_only = config.GetInt( "drain_measured_only" );

  string watch_file = config.GetStr( "watch_file" );
  _LoadWatchList(watch_file);

  vector<int> watch_flits = config.GetIntArray("watch_flits");
  for(size_t i = 0; i < watch_flits.size(); ++i) {
    _flits_to_watch.insert(watch_flits[i]);
  }
  
  vector<int> watch_packets = config.GetIntArray("watch_packets");
  for(size_t i = 0; i < watch_packets.size(); ++i) {
    _packets_to_watch.insert(watch_packets[i]);
  }

  vector<int> watch_transactions = config.GetIntArray("watch_transactions");
  for(size_t i = 0; i < watch_transactions.size(); ++i) {
    _transactions_to_watch.insert(watch_transactions[i]);
  }

  string stats_out_file = config.GetStr( "stats_out" );
  if(stats_out_file == "") {
    _stats_out = NULL;
  } else if(stats_out_file == "-") {
    _stats_out = &cout;
  } else {
    _stats_out = new ofstream(stats_out_file.c_str());
    config.WriteMatlabFile(_stats_out);
  }
  
  _flow_out = config.GetInt("flow_out");
  if(_flow_out) {
    string sent_packets_out_file = config.GetStr( "sent_packets_out" );
    if(sent_packets_out_file == "") {
      _sent_packets_out = NULL;
    } else {
      _sent_packets_out = new ofstream(sent_packets_out_file.c_str());
    }
    string active_packets_out_file = config.GetStr( "active_packets_out" );
    if(active_packets_out_file == "") {
      _active_packets_out = NULL;
    } else {
      _active_packets_out = new ofstream(active_packets_out_file.c_str());
    }
    string injected_flits_out_file = config.GetStr( "injected_flits_out" );
    if(injected_flits_out_file == "") {
      _injected_flits_out = NULL;
    } else {
      _injected_flits_out = new ofstream(injected_flits_out_file.c_str());
    }
    string ejected_flits_out_file = config.GetStr( "ejected_flits_out" );
    if(ejected_flits_out_file == "") {
      _ejected_flits_out = NULL;
    } else {
      _ejected_flits_out = new ofstream(ejected_flits_out_file.c_str());
    }
    string received_flits_out_file = config.GetStr( "received_flits_out" );
    if(received_flits_out_file == "") {
      _received_flits_out = NULL;
    } else {
      _received_flits_out = new ofstream(received_flits_out_file.c_str());
    }
    string sent_flits_out_file = config.GetStr( "sent_flits_out" );
    if(sent_flits_out_file == "") {
      _sent_flits_out = NULL;
    } else {
      _sent_flits_out = new ofstream(sent_flits_out_file.c_str());
    }
    string stored_flits_out_file = config.GetStr( "stored_flits_out" );
    if(stored_flits_out_file == "") {
      _stored_flits_out = NULL;
    } else {
      _stored_flits_out = new ofstream(stored_flits_out_file.c_str());
    }
  }

}

TrafficManager::~TrafficManager( )
{

  for ( int source = 0; source < _nodes; ++source ) {
    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
      delete _buf_states[source][subnet];
    }
  }
  
  for ( int c = 0; c < _classes; ++c ) {
    delete _plat_stats[c];
    delete _overall_min_plat[c];
    delete _overall_avg_plat[c];
    delete _overall_max_plat[c];

    delete _tlat_stats[c];
    delete _overall_min_tlat[c];
    delete _overall_avg_tlat[c];
    delete _overall_max_tlat[c];

    delete _frag_stats[c];
    delete _overall_min_frag[c];
    delete _overall_avg_frag[c];
    delete _overall_max_frag[c];
    
    delete _hop_stats[c];
    delete _overall_accepted[c];
    delete _overall_accepted_min[c];
    
    for ( int source = 0; source < _nodes; ++source ) {
      delete _sent_flits[c][source];
      
      for ( int dest = 0; dest < _nodes; ++dest ) {
	delete _pair_plat[c][source*_nodes+dest];
	delete _pair_tlat[c][source*_nodes+dest];
      }

      delete _injection_process[source][c];
    }
    
    for ( int dest = 0; dest < _nodes; ++dest ) {
      delete _accepted_flits[c][dest];
    }
  }
  
  delete _batch_time;
  delete _overall_batch_time;
  
  if(gWatchOut && (gWatchOut != &cout)) delete gWatchOut;
  if(_stats_out && (_stats_out != &cout)) delete _stats_out;

  if(_flow_out) {
    if(_sent_packets_out) delete _sent_packets_out;
    if(_active_packets_out) delete _active_packets_out;
    if(_injected_flits_out) delete _injected_flits_out;
    if(_ejected_flits_out) delete _ejected_flits_out;
    if(_received_flits_out) delete _received_flits_out;
    if(_sent_flits_out) delete _sent_flits_out;
    if(_stored_flits_out) delete _stored_flits_out;
  }

  Flit::FreeAll();
  Credit::FreeAll();
}


void TrafficManager::_RetireFlit( Flit *f, int dest )
{
  _deadlock_timer = 0;

  assert(_total_in_flight_flits[f->cl].count(f->id) > 0);
  _total_in_flight_flits[f->cl].erase(f->id);
  
  if(f->record) {
    assert(_measured_in_flight_flits[f->cl].count(f->id) > 0);
    _measured_in_flight_flits[f->cl].erase(f->id);
  }

  if ( f->watch ) { 
    *gWatchOut << GetSimTime() << " | "
	       << "node" << dest << " | "
	       << "Retiring flit " << f->id 
	       << " (packet " << f->pid
	       << ", src = " << f->src 
	       << ", dest = " << f->dest
	       << ", hops = " << f->hops
	       << ", lat = " << f->atime - f->time
	       << ")." << endl;
  }

  _last_id = f->id;
  _last_pid = f->pid;

  if ( f->head && ( f->dest != dest ) ) {
    ostringstream err;
    err << "Flit " << f->id << " arrived at incorrect output " << dest;
    Error( err.str( ) );
  }

  if ( f->tail ) {
    Flit * head;
    if(f->head) {
      head = f;
    } else {
      map<int, Flit *>::iterator iter = _retired_packets[f->cl].find(f->pid);
      assert(iter != _retired_packets[f->cl].end());
      head = iter->second;
      _retired_packets[f->cl].erase(iter);
      assert(head->head);
      assert(f->pid == head->pid);
    }
    if ( f->watch ) { 
      *gWatchOut << GetSimTime() << " | "
		 << "node" << dest << " | "
		 << "Retiring packet " << f->pid 
		 << " (lat = " << f->atime - head->time
		 << ", frag = " << (f->atime - head->atime) - (f->id - head->id)
		 << ", src = " << head->src 
		 << ", dest = " << head->dest
		 << ")." << endl;
    }

    int const & reply_class = _reply_class[f->cl];
    assert(reply_class < _classes);

    if (reply_class < 0) {
      if ( f->watch ) { 
	*gWatchOut << GetSimTime() << " | "
		   << "node" << dest << " | "
		   << "Completing transation " << f->tid
		   << " (lat = " << f->atime - head->ttime
		   << ", src = " << head->src 
		   << ", dest = " << head->dest
		   << ")." << endl;
      }
      int const & request_class = _request_class[f->cl];
      assert(request_class < _classes);
      if(request_class < 0) {
	// single-packet transactions "magically" notify source of completion 
	// when packet arrives at destination
	_requests_outstanding[f->src][f->cl]--;
      } else {
	// request-reply transactions complete when reply arrives
	_requests_outstanding[dest][request_class]--;
      }
    } else {
      _sent_packets[dest][f->cl]++;
      _GeneratePacket( f->dest, f->src, _packet_size[reply_class], 
		       reply_class, f->atime + 1, f->tid, f->ttime );
    }

    // Only record statistics once per packet (at tail)
    // and based on the simulation state
    if ( ( _sim_state == warming_up ) || f->record ) {
      
      _hop_stats[f->cl]->AddSample( f->hops );

      if((_slowest_flit[f->cl] < 0) ||
	 (_plat_stats[f->cl]->Max() < (f->atime - f->time)))
	_slowest_flit[f->cl] = f->id;
      _plat_stats[f->cl]->AddSample( f->atime - f->time);
      _frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );
      if(reply_class < 0) {
	int const & request_class = _request_class[f->cl];
	assert(request_class < _classes);
	int const cl = (request_class < 0) ? f->cl : request_class;
	_tlat_stats[cl]->AddSample( f->atime - f->ttime );
	_pair_tlat[cl][dest*_nodes+f->src]->AddSample( f->atime - f->ttime );
      }
      _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->time );
    }
    
    if(f != head) {
      head->Free();
    }
    
  }
  
  if(f->head && !f->tail) {
    _retired_packets[f->cl].insert(make_pair(f->pid, f));
  } else {
    f->Free();
  }
}

bool TrafficManager::_IssuePacket( int source, int cl )
{
  if((_max_outstanding[cl] > 0) && 
     (_requests_outstanding[source][cl] >= _max_outstanding[cl])) {
      return false;
  }
  if((_sim_mode == batch) &&
     !_timed_mode && 
     (_sent_packets[source][cl] >= _batch_size[cl])) {
    return false;
  }
  if(!_injection_process[source][cl]->test()) {
    return false;
  }
  _requests_outstanding[source][cl]++;
  _sent_packets[source][cl]++;
  return true;
}

void TrafficManager::_GeneratePacket( int source, int dest, int size, 
				      int cl, int time, int tid, int ttime )
{
  assert(size > 0);
  assert((source >= 0) && (source < _nodes));
  assert((dest >= 0) && (dest < _nodes));

  bool begin_trans = false;

  if(tid < 0) {
    tid = _cur_tid++;
    assert(_cur_tid);
    ttime = time;
    begin_trans = true;
  }

  int pid = _cur_pid++;
  assert(_cur_pid);

  bool watch = gWatchOut && ((_packets_to_watch.count(pid) > 0) ||
			     (_transactions_to_watch.count(tid) > 0));

  if(watch) {
    if(begin_trans) {
      *gWatchOut << GetSimTime() << " | "
		 << "node" << source << " | "
		 << "Beginning transaction " << tid
		 << " at time " << time
		 << "." << endl;
    }
    *gWatchOut << GetSimTime() << " | "
	       << "node" << source << " | "
	       << "Enqueuing packet " << pid
	       << " at time " << time
	       << "." << endl;
  }
  
  int subnet = _subnet[cl];
  
  bool record = (((_sim_state == running) ||
		  ((_sim_state == draining) && (time < _drain_time))) &&
		 _measure_stats[cl]);

  for ( int i = 0; i < size; ++i ) {

    int id = _cur_id++;
    assert(_cur_id);

    Flit * f = Flit::New();

    f->id = id;
    f->pid = pid;
    f->tid = tid;
    f->watch = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
    f->subnetwork = subnet;
    f->src = source;
    f->dest = dest;
    f->time = time;
    f->ttime = ttime;
    f->record = record;
    f->cl = cl;
    f->head = (i == 0);
    f->tail = (i == (size-1));
    f->vc  = -1;

    switch(_pri_type) {
    case class_based:
      f->pri = _class_priority[cl];
      break;
    case age_based:
      f->pri = numeric_limits<int>::max() - time;
      break;
    case trans_age_based:
      f->pri = numeric_limits<int>::max() - ttime;
      break;
    case sequence_based:
      f->pri = numeric_limits<int>::max() - _sent_packets[source][cl];
      break;
    default:
      f->pri = 0;
    }
    assert(f->pri >= 0);

    _total_in_flight_flits[f->cl].insert(make_pair(f->id, f));
    if(record) {
      _measured_in_flight_flits[f->cl].insert(make_pair(f->id, f));
    }
    
    if(gTrace) {
      cout<<"New Flit "<<f->src<<endl;
    }

    if(f->watch) { 
      *gWatchOut << GetSimTime() << " | "
		  << "node" << source << " | "
		  << "Enqueuing flit " << f->id
		  << " (packet " << f->pid
		  << ") at time " << time
		  << "." << endl;
    }

    _partial_packets[source][cl].push_back(f);
  }
}

void TrafficManager::_Inject(){

  for ( int source = 0; source < _nodes; ++source ) {
    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (source,class)
      // that is currently empty
      if ( _partial_packets[source][c].empty() ) {
	if ( !_empty_network ) {
	  if(_request_class[c] >= 0) {
	    _qtime[source][c] = _time;
	  } else {
	    bool generated = false;
	    while( !generated && ( _qtime[source][c] <= _time ) ) {
	      if(_IssuePacket(source, c)) { //generate a packet
		int dest = _traffic_pattern[c]->dest(source);
		int size = _packet_size[c];
		int time = ((_include_queuing == 1) ? _qtime[source][c] : _time);
		_GeneratePacket(source, dest, size, c, time, -1, time);
		generated = true;
	      }
	      ++_qtime[source][c];
	    }
	  }
	  if((_sim_state == draining) && (_qtime[source][c] > _drain_time)) {
	    _qdrained[source][c] = true;
	  }
	}
      }
    }
  }
}

void TrafficManager::_Step( )
{
  bool flits_in_flight = false;
  for(int c = 0; c < _classes; ++c) {
    flits_in_flight |= !_total_in_flight_flits[c].empty();
  }
  if(flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)) {
    _deadlock_timer = 0;
    cout << "WARNING: Possible network deadlock." << endl;
  }

  for ( int source = 0; source < _nodes; ++source ) {
    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
      Credit * const c = _net[subnet]->ReadCredit( source );
      if ( c ) {
	_buf_states[source][subnet]->ProcessCredit(c);
	c->Free();
      }
    }
  }

  vector<map<int, Flit *> > flits(_subnets);
  
  for ( int subnet = 0; subnet < _subnets; ++subnet ) {
    for ( int dest = 0; dest < _nodes; ++dest ) {
      Flit * const f = _net[subnet]->ReadFlit( dest );
      if ( f ) {
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | "
		     << "node" << dest << " | "
		     << "Ejecting flit " << f->id
		     << " (packet " << f->pid << ")"
		     << " from VC " << f->vc
		     << "." << endl;
	}
	flits[subnet].insert(make_pair(dest, f));
      }
      if( ( _sim_state == warming_up ) || ( _sim_state == running ) ) {
	for(int c = 0; c < _classes; ++c) {
	  _accepted_flits[c][dest]->AddSample( (f && (f->cl == c)) ? 1 : 0 );
	}
      }
    }
    _net[subnet]->ReadInputs( );
  }
  
  _Inject();

  vector<int> injected_flits(_classes*_subnets*_nodes);

  for(int source = 0; source < _nodes; ++source) {
    
    vector<Flit *> flits_sent_by_subnet(_subnets);
    vector<int> flits_sent_by_class(_classes);
    
    int const last_class = _last_class[source];

    for(int i = 1; i <= _classes; ++i) {

      int const c = (last_class + i) % _classes;

      if(!_partial_packets[source][c].empty()) {

	Flit * cf = _partial_packets[source][c].front();
	assert(cf);
	assert(cf->cl == c);

	int const subnet = cf->subnetwork;
	
	Flit * & f = flits_sent_by_subnet[subnet];

	if(f && (f->pri >= cf->pri)) {
	  continue;
	}

	BufferState * const dest_buf = _buf_states[source][subnet];

	if(cf->head && cf->vc == -1) { // Find first available VC

	  OutputSet route_set;
	  _rf(NULL, cf, 0, &route_set, true);
	  set<OutputSet::sSetElement> const & os = route_set.GetSet();
	  assert(os.size() == 1);
	  OutputSet::sSetElement const & se = *os.begin();
	  assert(se.output_port == 0);
	  int const & vc_start = se.vc_start;
	  int const & vc_end = se.vc_end;
	  int const vc_count = vc_end - vc_start + 1;
	  for(int i = 1; i <= vc_count; ++i) {
	    int const vc = vc_start + (_last_vc[source][subnet][c] - vc_start + i) % vc_count;
	    assert((vc >= vc_start) && (vc <= vc_end));
	    if(dest_buf->IsAvailableFor(vc) && !dest_buf->IsFullFor(vc)) {
	      cf->vc = vc;
	      break;
	    }
	  }
	}
	  
	if((cf->vc != -1) && (!dest_buf->IsFullFor(cf->vc))) {
	  f = cf;
	  _last_class[source] = cf->cl;
	}
      }
    }

    for(int subnet = 0; subnet < _subnets; ++subnet) {
      
      Flit * & f = flits_sent_by_subnet[subnet];
      
      if(f) {
	
	int const & subnet = f->subnetwork;
	int const & c = f->cl;
	
	if(f->head) {
	  _buf_states[source][subnet]->TakeBuffer(f->vc);
	  _last_vc[source][subnet][c] = f->vc;
	}
	
	_last_class[source] = c;
	
	_partial_packets[source][c].pop_front();
	_buf_states[source][subnet]->SendingFlit(f);
	
	if(_pri_type == network_age_based) {
	  f->pri = numeric_limits<int>::max() - _time;
	  assert(f->pri >= 0);
	}
	
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | "
		     << "node" << source << " | "
		     << "Injecting flit " << f->id
		     << " into subnet " << subnet
		     << " at time " << _time
		     << " with priority " << f->pri
		     << " (packet " << f->pid
		     << ", class = " << c
		     << ", src = " << f->src 
		     << ", dest = " << f->dest
		     << ")." << endl;
	  *gWatchOut << *f;
	}
	
	// Pass VC "back"
	if(!_partial_packets[source][c].empty() && !f->tail) {
	  Flit * nf = _partial_packets[source][c].front();
	  nf->vc = f->vc;
	}
	
	++flits_sent_by_class[c];
	if(_flow_out) ++injected_flits[(c*_subnets+subnet)*_nodes+source];
	
	_net[f->subnetwork]->WriteFlit(f, source);

      }	
    }
    if((_sim_state == warming_up) || (_sim_state == running)) {
      for(int c = 0; c < _classes; ++c) {
	_sent_flits[c][source]->AddSample(flits_sent_by_class[c]);
      }
    }
  }

  vector<int> ejected_flits(_classes*_subnets*_nodes);

  for(int subnet = 0; subnet < _subnets; ++subnet) {
    for(int dest = 0; dest < _nodes; ++dest) {
      map<int, Flit *>::const_iterator iter = flits[subnet].find(dest);
      if(iter != flits[subnet].end()) {
	Flit * const & f = iter->second;
	if(_flow_out) ++ejected_flits[(f->cl*_subnets+subnet)*_nodes+dest];
	f->atime = _time;
	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | "
		     << "node" << dest << " | "
		     << "Injecting credit for VC " << f->vc 
		     << " into subnet " << subnet 
		     << "." << endl;
	}
	Credit * const c = Credit::New();
	c->vc.insert(f->vc);
	_net[subnet]->WriteCredit(c, dest);
	_RetireFlit(f, dest);
      }
    }
    flits[subnet].clear();
    _net[subnet]->Evaluate( );
    _net[subnet]->WriteOutputs( );
  }
  
  if(_flow_out) {

    vector<vector<int> > received_flits(_classes*_subnets*_routers);
    vector<vector<int> > sent_flits(_classes*_subnets*_routers);
    vector<vector<int> > stored_flits(_classes*_subnets*_routers);
    vector<vector<int> > active_packets(_classes*_subnets*_routers);

    for (int subnet = 0; subnet < _subnets; ++subnet) {
      for(int router = 0; router < _routers; ++router) {
	Router * r = _router[subnet][router];
	for(int c = 0; c < _classes; ++c) {
	  received_flits[(c*_subnets+subnet)*_routers+router] = r->GetReceivedFlits(c);
	  sent_flits[(c*_subnets+subnet)*_routers+router] = r->GetSentFlits(c);
	  stored_flits[(c*_subnets+subnet)*_routers+router] = r->GetStoredFlits(c);
	  active_packets[(c*_subnets+subnet)*_routers+router] = r->GetActivePackets(c);
	  r->ResetStats(c);
	}
      }
    }
    if(_injected_flits_out) *_injected_flits_out << injected_flits << endl;
    if(_received_flits_out) *_received_flits_out << received_flits << endl;
    if(_stored_flits_out) *_stored_flits_out << stored_flits << endl;
    if(_sent_flits_out) *_sent_flits_out << sent_flits << endl;
    if(_ejected_flits_out) *_ejected_flits_out << ejected_flits << endl;
    if(_active_packets_out) *_active_packets_out << active_packets << endl;
  }

  ++_time;
  assert(_time);
  if(gTrace){
    cout<<"TIME "<<_time<<endl;
  }

}
  
bool TrafficManager::_PacketsOutstanding( ) const
{
  bool outstanding = false;

  for ( int c = 0; c < _classes; ++c ) {
    
    if ( _measured_in_flight_flits[c].empty() ) {

      for ( int s = 0; s < _nodes; ++s ) {
	if ( _measure_stats[c] && !_qdrained[s][c] ) {
#ifdef DEBUG_DRAIN
	  cout << "waiting on queue " << s << " class " << c;
	  cout << ", time = " << _time << " qtime = " << _qtime[s][c] << endl;
#endif
	  outstanding = true;
	  break;
	}
      }
    } else {
#ifdef DEBUG_DRAIN
      cout << "in flight = " << _measured_in_flight_flits[c].size() << endl;
#endif
      outstanding = true;
    }

    if ( outstanding ) { break; }
  }

  return outstanding;
}

void TrafficManager::_ClearStats( )
{
  _slowest_flit.assign(_classes, -1);

  for ( int c = 0; c < _classes; ++c ) {

    _plat_stats[c]->Clear( );
    _tlat_stats[c]->Clear( );
    _frag_stats[c]->Clear( );
  
    for ( int i = 0; i < _nodes; ++i ) {
      _sent_flits[c][i]->Clear( );
      
      for ( int j = 0; j < _nodes; ++j ) {
	_pair_plat[c][i*_nodes+j]->Clear( );
	_pair_tlat[c][i*_nodes+j]->Clear( );
      }
    }

    for ( int i = 0; i < _nodes; ++i ) {
      _accepted_flits[c][i]->Clear( );
    }
  
    _hop_stats[c]->Clear();

  }

}

int TrafficManager::_ComputeStats( const vector<Stats *> & stats, double *avg, double *min ) const 
{
  int dmin = -1;

  *min = numeric_limits<double>::max();
  *avg = 0.0;

  int const count = stats.size();

  for ( int i = 0; i < count; ++i ) {
    double curr = stats[i]->Average( );
    if ( curr < *min ) {
      *min = curr;
      dmin = i;
    }
    *avg += curr;
  }

  *avg /= (double)count;

  return dmin;
}

void TrafficManager::_DisplayRemaining( ostream & os ) const 
{
  for(int c = 0; c < _classes; ++c) {

    map<int, Flit *>::const_iterator iter;
    int i;

    os << "Class " << c << ":" << endl;

    os << "Remaining flits: ";
    for ( iter = _total_in_flight_flits[c].begin( ), i = 0;
	  ( iter != _total_in_flight_flits[c].end( ) ) && ( i < 10 );
	  iter++, i++ ) {
      os << iter->first << " ";
    }
    if(_total_in_flight_flits[c].size() > 10)
      os << "[...] ";
    
    os << "(" << _total_in_flight_flits[c].size() << " flits)" << endl;
    
    os << "Measured flits: ";
    for ( iter = _measured_in_flight_flits[c].begin( ), i = 0;
	  ( iter != _measured_in_flight_flits[c].end( ) ) && ( i < 10 );
	  iter++, i++ ) {
      os << iter->first << " ";
    }
    if(_measured_in_flight_flits[c].size() > 10)
      os << "[...] ";
    
    os << "(" << _measured_in_flight_flits[c].size() << " flits)" << endl;
    
  }
}

bool TrafficManager::_SingleSim( )
{
  int converged = 0;

  if (_sim_mode == batch && _timed_mode) {
    _sim_state = running;
    while(_time<_sample_period) {
      _Step();
      if ( _time % 10000 == 0 ) {
	cout << _sim_state << endl;
	if(_stats_out)
	  *_stats_out << "%=================================" << endl;
	
	DisplayStats();
      }
    }
    converged = 1;

  } else if(_sim_mode == batch && !_timed_mode){//batch mode   
    int batch_index = 0;
    while(batch_index < _batch_count) {
      for (int i = 0; i < _nodes; i++) {
	_sent_packets[i].assign(_classes, 0);
      }
      _last_id = -1;
      _last_pid = -1;
      _sim_state = running;
      int start_time = _time;
      bool batch_complete;
      do {
	_Step();
	batch_complete = true;
	for(int source = 0; (source < _nodes) && batch_complete; ++source) {
	  for(int c = 0; c < _classes; ++c) {
	    if(_sent_packets[source][c] < _batch_size[c]) {
	      batch_complete = false;
	      break;
	    }
	  }
	}
	if(_sent_packets_out) {
	  *_sent_packets_out << "sent_packets(" << _time << ",:) = " << _sent_packets << ";" << endl;
	}
      } while(!batch_complete);
      cout << "Batch " << batch_index + 1 << " ("<<_batch_size  <<  " packets) sent. Time used is " << _time - start_time << " cycles." << endl;
      cout << "Draining the Network...................\n";
      _sim_state = draining;
      _drain_time = _time;
      int empty_steps = 0;

      bool packets_left = false;
      for(int c = 0; c < _classes; ++c) {
	packets_left |= !_total_in_flight_flits[c].empty();
      }

      while( packets_left ) { 
	_Step( ); 

	++empty_steps;
	
	if ( empty_steps % 1000 == 0 ) {
	  _DisplayRemaining( ); 
	  cout << ".";
	}

	packets_left = false;
	for(int c = 0; c < _classes; ++c) {
	  packets_left |= !_total_in_flight_flits[c].empty();
	}
      }
      cout << endl;
      cout << "Batch " << batch_index + 1 << " ("<<_batch_size  <<  " packets) received. Time used is " << _time - _drain_time << " cycles. Last packet was " << _last_pid << ", last flit was " << _last_id << "." <<endl;
      _batch_time->AddSample(_time - start_time);
      cout << _sim_state << endl;
      if(_stats_out)
	*_stats_out << "%=================================" << endl;

      cout << "Batch duration = " << _time - start_time << endl;

      DisplayStats();

      if(_stats_out) {
	*_stats_out << "batch_time(" << batch_index + 1 << ") = " << _time << ";" << endl;
      }

      ++batch_index;
    }
    converged = 1;
  } else { 
    //once warmed up, we require 3 converging runs
    //to end the simulation 
    vector<double> prev_latency(_classes, 0.0);
    vector<double> prev_accepted(_classes, 0.0);
    bool clear_last = false;
    int total_phases = 0;
    while( ( total_phases < _max_samples ) && 
	   ( ( _sim_state != running ) || 
	     ( converged < 3 ) ) ) {

      if ( clear_last || (( ( _sim_state == warming_up ) && ( ( total_phases % 2 ) == 0 ) )) ) {
	clear_last = false;
	_ClearStats( );
      }
      
      
      for ( int iter = 0; iter < _sample_period; ++iter )
	_Step( );
      
      cout << _sim_state << endl;
      if(_stats_out)
	*_stats_out << "%=================================" << endl;

      DisplayStats();

      int lat_exc_class = -1;
      int lat_chg_exc_class = -1;
      int acc_chg_exc_class = -1;

      for(int c = 0; c < _classes; ++c) {

	if(_measure_stats[c] == 0) {
	  continue;
	}

	double cur_latency = _plat_stats[c]->Average( );

	double min, avg;
	_ComputeStats( _accepted_flits[c], &avg, &min );
	double cur_accepted = avg;

	double latency_change = fabs((cur_latency - prev_latency[c]) / cur_latency);
	prev_latency[c] = cur_latency;

	double accepted_change = fabs((cur_accepted - prev_accepted[c]) / cur_accepted);
	prev_accepted[c] = cur_accepted;

	double latency = cur_latency;
	double count = (double)_plat_stats[c]->NumSamples();
	  
	map<int, Flit *>::const_iterator iter;
	for(iter = _total_in_flight_flits[c].begin(); 
	    iter != _total_in_flight_flits[c].end(); 
	    iter++) {
	  latency += (double)(_time - iter->second->time);
	  count++;
	}
	
	if((lat_exc_class < 0) &&
	   (_latency_thres[c] >= 0.0) &&
	   ((latency / count) > _latency_thres[c])) {
	  lat_exc_class = c;
	}
	
	cout << "latency change    = " << latency_change << endl;
	if(lat_chg_exc_class < 0) {
	  if((_sim_state == warming_up) &&
	     (_warmup_threshold[c] >= 0.0) &&
	     (latency_change > _warmup_threshold[c])) {
	    lat_chg_exc_class = c;
	  } else if((_sim_state == running) &&
		    (_stopping_threshold[c] >= 0.0) &&
		    (latency_change > _stopping_threshold[c])) {
	    lat_chg_exc_class = c;
	  }
	}
	
	cout << "throughput change = " << accepted_change << endl;
	if(acc_chg_exc_class < 0) {
	  if((_sim_state == warming_up) &&
	     (_acc_warmup_threshold[c] >= 0.0) &&
	     (accepted_change > _acc_warmup_threshold[c])) {
	    acc_chg_exc_class = c;
	  } else if((_sim_state == running) &&
		    (_acc_stopping_threshold[c] >= 0.0) &&
		    (accepted_change > _acc_stopping_threshold[c])) {
	    acc_chg_exc_class = c;
	  }
	}
	
      }

      // Fail safe for latency mode, throughput will ust continue
      if ( ( _sim_mode == latency ) && ( lat_exc_class >= 0 ) ) {

	cout << "Average latency for class " << lat_exc_class << " exceeded " << _latency_thres[lat_exc_class] << " cycles. Aborting simulation." << endl;
	converged = 0; 
	_sim_state = warming_up;
	break;

      }

      if ( _sim_state == warming_up ) {
	if ( ( _warmup_periods > 0 ) ? 
	     ( total_phases + 1 >= _warmup_periods ) :
	     ( ( ( _sim_mode != latency ) || ( lat_chg_exc_class < 0 ) ) &&
	       ( acc_chg_exc_class < 0 ) ) ) {
	  cout << "Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
	  clear_last = true;
	  _sim_state = running;
	}
      } else if(_sim_state == running) {
	if ( ( ( _sim_mode != latency ) || ( lat_chg_exc_class < 0 ) ) &&
	     ( acc_chg_exc_class < 0 ) ) {
	  ++converged;
	} else {
	  converged = 0;
	}
      }
      ++total_phases;
    }
  
    if ( _sim_state == running ) {
      ++converged;

      if ( _sim_mode == latency ) {
	cout << "Draining all recorded packets ..." << endl;
	_sim_state  = draining;
	_drain_time = _time;
	int empty_steps = 0;
	while( _PacketsOutstanding( ) ) { 
	  _Step( ); 

	  ++empty_steps;
	  
	  if ( empty_steps % 1000 == 0 ) {
	    
	    int lat_exc_class = -1;
	    
	    for(int c = 0; c < _classes; c++) {

	      double threshold = _latency_thres[c];

	      if(threshold < 0.0) {
		continue;
	      }

	      double acc_latency = _plat_stats[c]->Sum();
	      double acc_count = (double)_plat_stats[c]->NumSamples();

	      map<int, Flit *>::const_iterator iter;
	      for(iter = _total_in_flight_flits[c].begin(); 
		  iter != _total_in_flight_flits[c].end(); 
		  iter++) {
		acc_latency += (double)(_time - iter->second->time);
		acc_count++;
	      }
	      
	      if((acc_latency / acc_count) > threshold) {
		lat_exc_class = c;
		break;
	      }
	    }
	    
	    if(lat_exc_class >= 0) {
	      cout << "Average latency for class " << lat_exc_class << " exceeded " << _latency_thres[lat_exc_class] << " cycles. Aborting simulation." << endl;
	      converged = 0; 
	      _sim_state = warming_up;
	      break;
	    }
	    
	    _DisplayRemaining( ); 
	    
	  }
	}
      }
    } else {
      cout << "Too many sample periods needed to converge" << endl;
    }
  }
  
  return ( converged > 0 );
}

bool TrafficManager::Run( )
{
  for ( int sim = 0; sim < _total_sims; ++sim ) {

    _time = 0;

    //remove any pending request from the previous simulations
    for (int i=0;i<_nodes;i++) {
      _requests_outstanding[i].assign(_classes, 0);
    }

    //reset queuetime for all sources
    for ( int s = 0; s < _nodes; ++s ) {
      _qtime[s].assign(_classes, 0);
      _qdrained[s].assign(_classes, false);
    }

    // warm-up ...
    // reset stats, all packets after warmup_time marked
    // converge
    // draing, wait until all packets finish
    _sim_state    = warming_up;
  
    _ClearStats( );

    if ( !_SingleSim( ) ) {
      cout << "Simulation unstable, ending ..." << endl;
      return false;
    }

    // Empty any remaining packets
    cout << "Draining remaining packets ..." << endl;
    _empty_network = true;
    int empty_steps = 0;

    bool packets_left = false;
    for(int c = 0; c < _classes; ++c) {
      if(_drain_measured_only) {
	packets_left |= !_measured_in_flight_flits[c].empty();
      } else {
	packets_left |= !_total_in_flight_flits[c].empty();
      }
    }

    while( packets_left ) { 
      _Step( ); 

      ++empty_steps;

      if ( empty_steps % 1000 == 0 ) {
	_DisplayRemaining( ); 
      }
      
      packets_left = false;
      for(int c = 0; c < _classes; ++c) {
	if(_drain_measured_only) {
	  packets_left |= !_measured_in_flight_flits[c].empty();
	} else {
	  packets_left |= !_total_in_flight_flits[c].empty();
	}
      }
    }
    //wait until all the credits are drained as well
    while(Credit::OutStanding()!=0){
      _Step();
    }
    _empty_network = false;

    //for the love of god don't ever say "Time taken" anywhere else
    //the power script depend on it
    cout << "Time taken is " << _time << " cycles" <<endl; 

    UpdateOverallStats();
  }
  
  if(_print_csv_results)
    DisplayOverallStatsCSV();
  else
    DisplayOverallStats();
  
  return true;
}

void TrafficManager::UpdateOverallStats() {

  for ( int c = 0; c < _classes; ++c ) {
    
    if(_measure_stats[c] == 0) {
      continue;
    }
    
    if(_plat_stats[c]->NumSamples() > 0) {
      _overall_min_plat[c]->AddSample( _plat_stats[c]->Min( ) );
      _overall_avg_plat[c]->AddSample( _plat_stats[c]->Average( ) );
      _overall_max_plat[c]->AddSample( _plat_stats[c]->Max( ) );
    }
    if(_tlat_stats[c]->NumSamples() > 0) {
      _overall_min_tlat[c]->AddSample( _tlat_stats[c]->Min( ) );
      _overall_avg_tlat[c]->AddSample( _tlat_stats[c]->Average( ) );
      _overall_max_tlat[c]->AddSample( _tlat_stats[c]->Max( ) );
    }
    if(_frag_stats[c]->NumSamples() > 0) {
      _overall_min_frag[c]->AddSample( _frag_stats[c]->Min( ) );
      _overall_avg_frag[c]->AddSample( _frag_stats[c]->Average( ) );
      _overall_max_frag[c]->AddSample( _frag_stats[c]->Max( ) );
    }
    
    double min, avg;
    _ComputeStats( _accepted_flits[c], &avg, &min );
    _overall_accepted[c]->AddSample( avg );
    _overall_accepted_min[c]->AddSample( min );
    

  }
  
  if(_sim_mode == batch)
    _overall_batch_time->AddSample(_batch_time->Sum( ));
  
}

void TrafficManager::DisplayStats(ostream & os) const {
  
  for(int c = 0; c < _classes; ++c) {
    
    if(_measure_stats[c] == 0) {
      continue;
    }
    
    cout << "Class " << c << ":" << endl;
    
    cout << "Minimum latency = " << _plat_stats[c]->Min() << endl;
    cout << "Average latency = " << _plat_stats[c]->Average() << endl;
    cout << "Maximum latency = " << _plat_stats[c]->Max() << endl;
    cout << "Average fragmentation = " << _frag_stats[c]->Average() << endl;

    double min, avg;
    int dmin = _ComputeStats(_accepted_flits[c], &avg, &min);
    cout << "Accepted packets = " << min
	 << " at node " << dmin
	 << " (avg = " << avg << ")"
	 << endl;
    
    cout << "Total in-flight flits = " << _total_in_flight_flits[c].size()
	 << " (" << _measured_in_flight_flits[c].size() << " measured)"
	 << endl;
    
    //c+1 due to matlab array starting at 1
    if(_stats_out) {
      *_stats_out << "lat(" << c+1 << ") = " << _plat_stats[c]->Average()
		  << ";" << endl
		  << "lat_hist(" << c+1 << ",:) = " << *_plat_stats[c]
		  << ";" << endl
		  << "frag_hist(" << c+1 << ",:) = " << *_frag_stats[c]
		  << ";" << endl
		  << "pair_sent(" << c+1 << ",:) = [ ";
      for(int i = 0; i < _nodes; ++i) {
	for(int j = 0; j < _nodes; ++j) {
	  *_stats_out << _pair_plat[c][i*_nodes+j]->NumSamples() << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "pair_lat(" << c+1 << ",:) = [ ";
      for(int i = 0; i < _nodes; ++i) {
	for(int j = 0; j < _nodes; ++j) {
	  *_stats_out << _pair_plat[c][i*_nodes+j]->Average( ) << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "pair_lat(" << c+1 << ",:) = [ ";
      for(int i = 0; i < _nodes; ++i) {
	for(int j = 0; j < _nodes; ++j) {
	  *_stats_out << _pair_tlat[c][i*_nodes+j]->Average( ) << " ";
	}
      }
      *_stats_out << "];" << endl
		  << "sent(" << c+1 << ",:) = [ ";
      for ( int d = 0; d < _nodes; ++d ) {
	*_stats_out << _sent_flits[c][d]->Average( ) << " ";
      }
      *_stats_out << "];" << endl
		  << "accepted(" << c+1 << ",:) = [ ";
      for ( int d = 0; d < _nodes; ++d ) {
	*_stats_out << _accepted_flits[c][d]->Average( ) << " ";
      }
      *_stats_out << "];" << endl;
      *_stats_out << "inflight(" << c+1 << ") = " << _total_in_flight_flits[c].size() << ";" << endl;
    }
  }    
}

void TrafficManager::DisplayOverallStats( ostream & os ) const {

  for ( int c = 0; c < _classes; ++c ) {

    if(_measure_stats[c] == 0) {
      continue;
    }

    os << "====== Traffic class " << c << " ======" << endl;
    
    if(_overall_min_plat[c]->NumSamples() > 0) {
      os << "Overall minimum latency = " << _overall_min_plat[c]->Average( )
	 << " (" << _overall_min_plat[c]->NumSamples( ) << " samples)" << endl;
      assert(_overall_avg_plat[c]->NumSamples() > 0);
      os << "Overall average latency = " << _overall_avg_plat[c]->Average( )
	 << " (" << _overall_avg_plat[c]->NumSamples( ) << " samples)" << endl;
      assert(_overall_max_plat[c]->NumSamples() > 0);
      os << "Overall maximum latency = " << _overall_max_plat[c]->Average( )
	 << " (" << _overall_max_plat[c]->NumSamples( ) << " samples)" << endl;
    }
    if(_overall_min_tlat[c]->NumSamples() > 0) {
      os << "Overall minimum transaction latency = " << _overall_min_tlat[c]->Average( )
	 << " (" << _overall_min_tlat[c]->NumSamples( ) << " samples)" << endl;
      assert(_overall_avg_tlat[c]->NumSamples() > 0);
      os << "Overall average transaction latency = " << _overall_avg_tlat[c]->Average( )
	 << " (" << _overall_avg_tlat[c]->NumSamples( ) << " samples)" << endl;
      assert(_overall_max_tlat[c]->NumSamples() > 0);
      os << "Overall maximum transaction latency = " << _overall_max_tlat[c]->Average( )
	 << " (" << _overall_max_tlat[c]->NumSamples( ) << " samples)" << endl;
    }
    if(_overall_min_frag[c]->NumSamples() > 0) {
      os << "Overall minimum fragmentation = " << _overall_min_frag[c]->Average( )
	 << " (" << _overall_min_frag[c]->NumSamples( ) << " samples)" << endl;
      assert(_overall_avg_frag[c]->NumSamples() > 0);
      os << "Overall average fragmentation = " << _overall_avg_frag[c]->Average( )
	 << " (" << _overall_avg_frag[c]->NumSamples( ) << " samples)" << endl;
      assert(_overall_max_frag[c]->NumSamples() > 0);
      os << "Overall maximum fragmentation = " << _overall_max_frag[c]->Average( )
	 << " (" << _overall_max_frag[c]->NumSamples( ) << " samples)" << endl;
    }
    if(_overall_accepted[c]->NumSamples() > 0) {
      os << "Overall average accepted rate = " << _overall_accepted[c]->Average( )
	 << " (" << _overall_accepted[c]->NumSamples( ) << " samples)" << endl;
      assert(_overall_accepted_min[c]->NumSamples() > 0);
      os << "Overall min accepted rate = " << _overall_accepted_min[c]->Average( )
	 << " (" << _overall_accepted_min[c]->NumSamples( ) << " samples)" << endl;
    }
    if(_hop_stats[c]->NumSamples() > 0) {
      os << "Average hops = " << _hop_stats[c]->Average( )
	 << " (" << _hop_stats[c]->NumSamples( ) << " samples)" << endl;
    }
    
    if(_slowest_flit[c] >= 0) {
      os << "Slowest flit = " << _slowest_flit[c] << endl;
    }
  
  }
  
  if(_sim_mode == batch)
    os << "Overall batch duration = " << _overall_batch_time->Average( )
       << " (" << _overall_batch_time->NumSamples( ) << " samples)" << endl;
  
}

void TrafficManager::DisplayOverallStatsCSV(ostream & os) const {
  for(int c = 0; c < _classes; ++c) {
    os << "results:"
       << c
       << "," << _traffic[c]
       << "," << (_request_class[c] >= 0)
       << "," << _packet_size[c]
       << "," << _load[c]
       << "," << _overall_min_plat[c]->Average( )
       << "," << _overall_avg_plat[c]->Average( )
       << "," << _overall_max_plat[c]->Average( )
       << "," << _overall_min_tlat[c]->Average( )
       << "," << _overall_avg_tlat[c]->Average( )
       << "," << _overall_max_tlat[c]->Average( )
       << "," << _overall_min_frag[c]->Average( )
       << "," << _overall_avg_frag[c]->Average( )
       << "," << _overall_max_frag[c]->Average( )
       << "," << _overall_accepted[c]->Average( )
       << "," << _overall_accepted_min[c]->Average( )
       << "," << _hop_stats[c]->Average( )
       << endl;
  }
}

//read the watchlist
void TrafficManager::_LoadWatchList(const string & filename){
  ifstream watch_list;
  watch_list.open(filename.c_str());
  
  string line;
  if(watch_list.is_open()) {
    while(!watch_list.eof()) {
      getline(watch_list, line);
      if(line != "") {
	if(line[0] == 'p') {
	  _packets_to_watch.insert(atoi(line.c_str()+1));
	} else if(line[0] == 't') {
	  _transactions_to_watch.insert(atoi(line.c_str()+1));
	} else {
	  _flits_to_watch.insert(atoi(line.c_str()));
	}
      }
    }
    
  } else {
    //cout<<"Unable to open flit watch file, continuing with simulation\n";
  }
}
