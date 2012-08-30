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

#include <sstream>
#include <cmath>
#include <fstream>
#include <limits>
#include <cstdlib>

#include "booksim.hpp"
#include "booksim_config.hpp"
#include "trafficmanager.hpp"
#include "batchtrafficmanager.hpp"
#include "random_utils.hpp" 
#include "vc.hpp"
#include "packet_reply_info.hpp"


int NOTIFICATION_TIME_THRESHOLD=0;



TrafficManager * TrafficManager::NewTrafficManager(Configuration const & config,
						   vector<Network *> const & net)
{
cout<<"size of "<<sizeof(Flit)<<endl;
  TrafficManager * result = NULL;
  string sim_type = config.GetStr("sim_type");
  if((sim_type == "latency") || (sim_type == "throughput")) {
    result = new TrafficManager(config, net);
  } else if(sim_type == "batch") {
    result = new BatchTrafficManager(config, net);
  } else {
    cerr << "Unknown simulation type: " << sim_type << endl;
  } 
  return result;
}

TrafficManager::TrafficManager( const Configuration &config, const vector<Network *> & net )
: Module( 0, "traffic_manager" ), _net(net), _empty_network(false), _deadlock_timer(0), _warmup_time(-1), _drain_time(-1), _cur_id(0), _cur_pid(0), _cur_tid(0), _time(0)
{

  NOTIFICATION_TIME_THRESHOLD = config.GetInt("notification_time_threshold");
  _nodes = _net[0]->NumNodes( );
  _routers = _net[0]->NumRouters( );

  _subnets = config.GetInt("subnets");
 
  _subnet.resize(Flit::NUM_FLIT_TYPES);
  _subnet[Flit::READ_REQUEST] = config.GetInt("read_request_subnet");
  _subnet[Flit::READ_REPLY] = config.GetInt("read_reply_subnet");
  _subnet[Flit::WRITE_REQUEST] = config.GetInt("write_request_subnet");
  _subnet[Flit::WRITE_REPLY] = config.GetInt("write_reply_subnet");

  // ============ Message priorities ============ 

  string priority = config.GetStr( "priority" );

  if ( priority == "class" ) {
    _pri_type = class_based;
  } else if ( priority == "age" ) {
    _pri_type = age_based;
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
  } else if ( priority == "notification"){
    _pri_type = forward_note;
  } else if ( priority == "none" ) {
    _pri_type = none;
  } else {
    Error( "Unkown priority value: " + priority );
  }

  _replies_inherit_priority = config.GetInt("replies_inherit_priority");

  // ============ Routing ============ 

  string rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
  map<string, tRoutingFunction>::const_iterator rf_iter = gRoutingFunctionMap.find(rf);
  if(rf_iter == gRoutingFunctionMap.end()) {
    Error("Invalid routing function: " + rf);
  }
  _rf = rf_iter->second;

  // ============ Traffic ============ 

  _classes = config.GetInt("classes");

  _use_read_write = config.GetIntArray("use_read_write");
  if(_use_read_write.empty()) {
    _use_read_write.push_back(config.GetInt("use_read_write"));
  }
  _use_read_write.resize(_classes, _use_read_write.back());

  _read_request_size = config.GetIntArray("read_request_size");
  if(_read_request_size.empty()) {
    _read_request_size.push_back(config.GetInt("read_request_size"));
  }
  _read_request_size.resize(_classes, _read_request_size.back());

  _read_reply_size = config.GetIntArray("read_reply_size");
  if(_read_reply_size.empty()) {
    _read_reply_size.push_back(config.GetInt("read_reply_size"));
  }
  _read_reply_size.resize(_classes, _read_reply_size.back());

  _write_request_size = config.GetIntArray("write_request_size");
  if(_write_request_size.empty()) {
    _write_request_size.push_back(config.GetInt("write_request_size"));
  }
  _write_request_size.resize(_classes, _write_request_size.back());

  _write_reply_size = config.GetIntArray("write_reply_size");
  if(_write_reply_size.empty()) {
    _write_reply_size.push_back(config.GetInt("write_reply_size"));
  }
  _write_reply_size.resize(_classes, _write_reply_size.back());

  _packet_size = config.GetIntArray( "const_flits_per_packet" );
  if(_packet_size.empty()) {
    _packet_size.push_back(config.GetInt("const_flits_per_packet"));
  }
  _packet_size.resize(_classes, _packet_size.back());
  
  for(int c = 0; c < _classes; ++c)
    if(_use_read_write[c])
      _packet_size[c] = (_read_request_size[c] + _read_reply_size[c] +
			 _write_request_size[c] + _write_reply_size[c]) / 2;

  _load = config.GetFloatArray("injection_rate"); 
  if(_load.empty()) {
    _load.push_back(config.GetFloat("injection_rate"));
  }
  _load.resize(_classes, _load.back());

  if(config.GetInt("injection_rate_uses_flits")) {
    for(int c = 0; c < _classes; ++c)
      _load[c] /= (double)_packet_size[c];
  }

  _traffic = config.GetStrArray("traffic");
  _traffic.resize(_classes, _traffic.back());

  _traffic_pattern.resize(_classes);

  _class_priority = config.GetIntArray("class_priority"); 
  if(_class_priority.empty()) {
    _class_priority.push_back(config.GetInt("class_priority"));
  }
  _class_priority.resize(_classes, _class_priority.back());

  for(int c = 0; c < _classes; ++c) {
    _traffic_pattern[c] = TrafficPattern::New(_traffic[c], _nodes, config);

    int const & prio = _class_priority[c];
    if(_class_prio_map.count(prio) > 0) {
      _class_prio_map.find(prio)->second.second.push_back(c);
    } else {
      _class_prio_map.insert(make_pair(prio, make_pair(-1, vector<int>(1, c))));
    }
  }

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
      _last_vc[source][subnet].resize(_classes, -1);
    }
  }

  // ============ Injection queues ============ 

  _qtime.resize(_nodes);
  _qdrained.resize(_nodes);
  _partial_packets.resize(_nodes);

  for ( int s = 0; s < _nodes; ++s ) {
    _qtime[s].resize(_classes);
    _qdrained[s].resize(_classes);
    _partial_packets[s].resize(_classes);
  }

  _total_in_flight_flits.resize(_classes);
  _measured_in_flight_flits.resize(_classes);
  _retired_packets.resize(_classes);

  _sent_packets.resize(_nodes);
  _repliesPending.resize(_nodes);
  _requestsOutstanding.resize(_nodes);
  _maxOutstanding = config.GetInt ("max_outstanding_requests");  

  // ============ Statistics ============ 

  if(_pri_type == forward_note){
    _forward_note_source_stats.resize(_nodes);
    _forward_note_dest_stats.resize(_nodes);
    for(int i = 0; i<_nodes; i++){
      _forward_note_source_stats[i] = new Stats(this, "lol", 
					  1.0, 16);
      _forward_note_dest_stats[i] = new Stats(this, "lol", 
					  1.0, 16);
    }
  }

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

  _slowest_flit.resize(_classes, -1);

  // ============ Simulation parameters ============ 

  _total_sims = config.GetInt( "sim_count" );

  _router.resize(_subnets);
  for (int i=0; i < _subnets; ++i) {
    _router[i] = _net[i]->GetRouters();
  }

  //seed the network
  RandomSeed(config.GetInt("seed"));

  _measure_latency = (config.GetStr("sim_type") == "latency");

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

  PacketReplyInfo::FreeAll();
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
		 << ", frag = " << (f->atime - head->atime) - (f->id - head->id) // NB: In the spirit of solving problems using ugly hacks, we compute the packet length by taking advantage of the fact that the IDs of flits within a packet are contiguous.
		 << ", src = " << head->src 
		 << ", dest = " << head->dest
		 << ")." << endl;
    }

    //code the source of request, look carefully, its tricky ;)
    if (f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST) {
      PacketReplyInfo* rinfo = PacketReplyInfo::New();
      rinfo->source = f->src;
      rinfo->time = f->atime;
      rinfo->ttime = f->ttime;
      rinfo->record = f->record;
      rinfo->type = f->type;
      _repliesDetails[f->id] = rinfo;
      _repliesPending[dest].push_back(f->id);
    } else {
      if ( f->watch ) { 
	*gWatchOut << GetSimTime() << " | "
		   << "node" << dest << " | "
		   << "Completing transation " << f->tid
		   << " (lat = " << f->atime - head->ttime
		   << ", src = " << head->src 
		   << ", dest = " << head->dest
		   << ")." << endl;
      }
      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
	_requestsOutstanding[dest]--;
      } else if(f->type == Flit::ANY_TYPE) {
	_requestsOutstanding[f->src]--;
      }
      
    }

    // Only record statistics once per packet (at tail)
    // and based on the simulation state
    if ( ( _sim_state == warming_up ) || f->record ) {
      
      _hop_stats[f->cl]->AddSample( f->hops );

      if((_slowest_flit[f->cl] < 0) ||
	 (_plat_stats[f->cl]->Max() < (f->atime - f->time)))
	_slowest_flit[f->cl] = f->id;
      
      if(_pri_type == forward_note){
	_forward_note_source_stats[head->src]->AddSample(head->next_notification);
	_forward_note_dest_stats[head->dest]->AddSample(head->next_notification);
      }

      _plat_stats[f->cl]->AddSample( f->atime - f->time);
      _frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );
      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY || f->type == Flit::ANY_TYPE)
	_tlat_stats[f->cl]->AddSample( f->atime - f->ttime );
   
      _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->time );
      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY)
	_pair_tlat[f->cl][dest*_nodes+f->src]->AddSample( f->atime - f->ttime );
      else if(f->type == Flit::ANY_TYPE)
	_pair_tlat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->ttime );
      
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

int TrafficManager::_IssuePacket( int source, int cl )
{
  int result;
  if(_use_read_write[cl]){ //use read and write
    //check queue for waiting replies.
    //check to make sure it is on time yet
    int pending_time = numeric_limits<int>::max(); //reset to maxtime+1
    if (!_repliesPending[source].empty()) {
      result = _repliesPending[source].front();
      pending_time = _repliesDetails.find(result)->second->time;
    }
    if (pending_time<=_qtime[source][cl]) {
      result = _repliesPending[source].front();
      _repliesPending[source].pop_front();
    } else if((_maxOutstanding > 0) && 
	      (_requestsOutstanding[source] >= _maxOutstanding)) {
      result = 0;
    } else {
      
      //produce a packet
      if(_injection_process[source][cl]->test()) {
	
	//coin toss to determine request type.
	result = (RandomFloat() < 0.5) ? -2 : -1;
	
      } else {
	result = 0;
      }
    } 
  } else { //normal mode
    result = _injection_process[source][cl]->test() ? 1 : 0;
  } 
  if(result != 0) {
    _sent_packets[source]++;
    _requestsOutstanding[source]++;
  }
  return result;
}

void TrafficManager::_GeneratePacket( int source, int stype, 
				      int cl, int time )
{
  assert(stype!=0);

  Flit::FlitType packet_type = Flit::ANY_TYPE;
  int size = _packet_size[cl]; //input size 
  int ttime = time;
  int pid = _cur_pid++;
  assert(_cur_pid);
  int tid = _cur_tid;
  int packet_destination = _traffic_pattern[cl]->dest(source);
  bool record = false;
  bool watch = gWatchOut && ((_packets_to_watch.count(pid) > 0) ||
			     (_transactions_to_watch.count(tid) > 0));
  if(_use_read_write[cl]){
    if(stype < 0) {
      if (stype ==-1) {
	packet_type = Flit::READ_REQUEST;
	size = _read_request_size[cl];
      } else if (stype == -2) {
	packet_type = Flit::WRITE_REQUEST;
	size = _write_request_size[cl];
      } else {
	ostringstream err;
	err << "Invalid packet type: " << packet_type;
	Error( err.str( ) );
      }
      if ( watch ) { 
	*gWatchOut << GetSimTime() << " | "
		   << "node" << source << " | "
		   << "Beginning transaction " << tid
		   << " at time " << time
		   << "." << endl;
      }
      ++_cur_tid;
      assert(_cur_tid);
    } else  {
      map<int, PacketReplyInfo*>::iterator iter = _repliesDetails.find(stype);
      PacketReplyInfo* rinfo = iter->second;
      
      if (rinfo->type == Flit::READ_REQUEST) {//read reply
	size = _read_reply_size[cl];
	packet_type = Flit::READ_REPLY;
      } else if(rinfo->type == Flit::WRITE_REQUEST) {  //write reply
	size = _write_reply_size[cl];
	packet_type = Flit::WRITE_REPLY;
      } else {
	ostringstream err;
	err << "Invalid packet type: " << rinfo->type;
	Error( err.str( ) );
      }
      packet_destination = rinfo->source;
      tid = rinfo->tid;
      time = rinfo->time;
      ttime = rinfo->ttime;
      record = rinfo->record;
      _repliesDetails.erase(iter);
      rinfo->Free();
    }
  } else {
    ++_cur_tid;
    assert(_cur_tid);
  }

  if ((packet_destination <0) || (packet_destination >= _nodes)) {
    ostringstream err;
    err << "Incorrect packet destination " << packet_destination
	<< " for stype " << packet_type;
    Error( err.str( ) );
  }

  if ( ( _sim_state == running ) ||
       ( ( _sim_state == draining ) && ( time < _drain_time ) ) ) {
    record = _measure_stats[cl];
  }

  int subnetwork = ((packet_type == Flit::ANY_TYPE) ? 
		    RandomInt(_subnets-1) :
		    _subnet[packet_type]);
  
  if ( watch ) { 
    *gWatchOut << GetSimTime() << " | "
		<< "node" << source << " | "
		<< "Enqueuing packet " << pid
		<< " at time " << time
		<< "." << endl;
  }
  
  for ( int i = 0; i < size; ++i ) {
    Flit * f  = Flit::New();
    f->id     = _cur_id++;
    assert(_cur_id);
    f->pid    = pid;
    f->tid    = tid;
    f->watch  = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
    f->subnetwork = subnetwork;
    f->src    = source;
    f->time   = time;
    f->ttime  = ttime;
    f->record = record;
    f->cl     = cl;

    //watchwatch
    if(f->id == -1){
      f->watch = true;
    }

    _total_in_flight_flits[f->cl].insert(make_pair(f->id, f));
    if(record) {
      _measured_in_flight_flits[f->cl].insert(make_pair(f->id, f));
    }
    
    if(gTrace){
      cout<<"New Flit "<<f->src<<endl;
    }
    f->type = packet_type;

    if ( i == 0 ) { // Head flit
      f->head = true;
      //packets are only generated to nodes smaller or equal to limit
      f->dest = packet_destination;
    } else {
      f->head = false;
      f->dest = -1;
    }
    switch( _pri_type ) {
    case class_based:
      f->pri = _class_priority[cl];
      assert(f->pri >= 0);
      break;
    case age_based:
      f->pri = numeric_limits<int>::max() - (_replies_inherit_priority ? ttime : time);
      assert(f->pri >= 0);
      break;
    case sequence_based:
      f->pri = numeric_limits<int>::max() - _sent_packets[source];
      assert(f->pri >= 0);
      break;
    default:
      f->pri = 0;
    }
    if ( i == ( size - 1 ) ) { // Tail flit
      f->tail = true;
    } else {
      f->tail = false;
    }
    
    f->vc  = -1;

    if ( f->watch ) { 
      *gWatchOut << GetSimTime() << " | "
		  << "node" << source << " | "
		  << "Enqueuing flit " << f->id
		  << " (packet " << f->pid
		  << ") at time " << time
		  << "." << endl;
    }

    _partial_packets[source][cl].push_back( f );
  }
}

void TrafficManager::_Inject(){

  for ( int input = 0; input < _nodes; ++input ) {
    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (input,class)
      // that is currently empty
      if ( _partial_packets[input][c].empty() ) {
	bool generated = false;
	  
	if ( !_empty_network ) {
	  while( !generated && ( _qtime[input][c] <= _time ) ) {
	    int stype = _IssuePacket( input, c );

	    if ( stype != 0 ) { //generate a packet
	      _GeneratePacket( input, stype, c, 
			       _include_queuing==1 ? 
			       _qtime[input][c] : _time );
	      generated = true;
	    }
	    //this is not a request packet
	    //don't advance time
	    if(!_use_read_write[c] || (stype <= 0)){
	      ++_qtime[input][c];
	    }
	  }
	  
	  if ( ( _sim_state == draining ) && 
	       ( _qtime[input][c] > _drain_time ) ) {
	    _qdrained[input][c] = true;
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
  if(flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)){
    _deadlock_timer = 0;
    cout << "WARNING: Possible network deadlock.\n";
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

  vector<int> injected_flits(_subnets*_nodes);

  for(int source = 0; source < _nodes; ++source) {
    
    vector<int> flits_sent_by_class(_classes);
    vector<int> flits_sent_by_subnet(_subnets);
    
    for(map<int, pair<int, vector<int> > >::reverse_iterator iter = _class_prio_map.rbegin();
	iter != _class_prio_map.rend();
	++iter) {
      
      int const & base = iter->second.first;
      vector<int> const & classes = iter->second.second;
      int const count = classes.size();
      
      for(int j = 1; j <= count; ++j) {
	
	int const offset = (base + j) % count;
	int const c = classes[offset];
	
	if(!_partial_packets[source][c].empty()) {
	  Flit * f = _partial_packets[source][c].front();
	  assert(f);

	  int const subnet = f->subnetwork;
	  if(flits_sent_by_subnet[subnet] > 0) {
	    continue;
	  }

	  BufferState * const dest_buf = _buf_states[source][subnet];

	  if(f->head && f->vc == -1) { // Find first available VC
	    
	    OutputSet route_set;
	    _rf(NULL, f, 0, &route_set, true);
	    set<OutputSet::sSetElement> const & os = route_set.GetSet();
	    assert(os.size() == 1);
	    OutputSet::sSetElement const & se = *os.begin();
	    assert(se.output_port == 0);
	    int const & vc_start = se.vc_start;
	    int const & vc_end = se.vc_end;
	    int const vc_count = vc_end - vc_start + 1;
	    for(int i = 1; i <= vc_count; ++i) {
	      int const vc = vc_start + (_last_vc[source][subnet][c] + i) % vc_count;
	      assert((vc >= vc_start) && (vc <= vc_end));
	      if(dest_buf->IsAvailableFor(vc) && !dest_buf->IsFullFor(vc)) {
		f->vc = vc;
		break;
	      }
	    }
	    if(f->vc == -1)
	      continue;

	    dest_buf->TakeBuffer(f->vc);
	    //retarded congestion inidcator
	    if(_pri_type == forward_note  && _qtime[source][c]<_time-NOTIFICATION_TIME_THRESHOLD){
	      f->next_notification = 1;
	    }	     
	    _last_vc[source][subnet][c] = f->vc - vc_start;
	  }
	  
	  assert(f->vc != -1);

	  if(!dest_buf->IsFullFor(f->vc)) {
	    
	    _partial_packets[source][c].pop_front();
	    dest_buf->SendingFlit(f);
	    
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
			 << "." << endl;
	    }
	    
	    // Pass VC "back"
	    if(!_partial_packets[source][c].empty() && !f->tail) {
	      Flit * nf = _partial_packets[source][c].front();
	      nf->vc = f->vc;
	    }
	    
	    ++flits_sent_by_class[c];
	    ++flits_sent_by_subnet[subnet];
	    if(_flow_out) ++injected_flits[subnet*_nodes+source];

	    _net[subnet]->WriteFlit(f, source);
	    iter->second.first = offset;
	    
	  }
	}
      }
    }
    if((_sim_state == warming_up) || (_sim_state == running)) {
      for(int c = 0; c < _classes; ++c) {
	_sent_flits[c][source]->AddSample(flits_sent_by_class[c]);
      }
    }
  }

  vector<int> ejected_flits(_subnets*_nodes);

  for(int subnet = 0; subnet < _subnets; ++subnet) {
    for(int dest = 0; dest < _nodes; ++dest) {
      map<int, Flit *>::const_iterator iter = flits[subnet].find(dest);
      if(iter != flits[subnet].end()) {
	Flit * const & f = iter->second;
	if(_flow_out) ++ejected_flits[subnet*_nodes+dest];
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

    vector<vector<int> > received_flits(_subnets*_routers);
    vector<vector<int> > sent_flits(_subnets*_routers);
    vector<vector<int> > stored_flits(_subnets*_routers);
    vector<vector<int> > active_packets(_subnets*_routers);

    for (int subnet = 0; subnet < _subnets; ++subnet) {
      for(int router = 0; router < _routers; ++router) {
	Router * r = _router[subnet][router];
	received_flits[subnet*_routers+router] = r->GetReceivedFlits();
	sent_flits[subnet*_routers+router] = r->GetSentFlits();
	stored_flits[subnet*_routers+router] = r->GetStoredFlits();
	active_packets[subnet*_routers+router] = r->GetActivePackets();
	r->ResetStats();
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
  for ( int c = 0; c < _classes; ++c ) {
    if ( _measure_stats[c] ) {
      if ( _measured_in_flight_flits[c].empty() ) {
	
	for ( int s = 0; s < _nodes; ++s ) {
	  if ( !_qdrained[s][c] ) {
#ifdef DEBUG_DRAIN
	    cout << "waiting on queue " << s << " class " << c;
	    cout << ", time = " << _time << " qtime = " << _qtime[s][c] << endl;
#endif
	    return true;
	  }
	}
      } else {
#ifdef DEBUG_DRAIN
	cout << "in flight = " << _measured_in_flight_flits[c].size() << endl;
#endif
	return true;
      }
    }
  }
  return false;
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
      _accepted_flits[c][i]->Clear( );
      
      for ( int j = 0; j < _nodes; ++j ) {
	_pair_plat[c][i*_nodes+j]->Clear( );
	_pair_tlat[c][i*_nodes+j]->Clear( );
      }
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
  
  //once warmed up, we require 3 converging runs to end the simulation 
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
    double lat_exc_value = 0.0;
    double lat_exc_accpted = 0.0;
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

      double latency =(double)_plat_stats[c]->Sum() ;
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
	lat_exc_value = (latency / count) ;
	lat_exc_accpted = min;
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
    if ( _measure_latency && ( lat_exc_class >= 0 ) ) {
      
      cout << "Average latency for class " << lat_exc_class << " omgomg " << lat_exc_value<<" exceeded " << _latency_thres[lat_exc_class] << " cycles. Aborting simulation." << endl;
      cout << "unstable node "<<	lat_exc_accpted <<endl;
      converged = 0; 
      _sim_state = warming_up;
      break;
      
    }
    
    if ( _sim_state == warming_up ) {
      if ( ( _warmup_periods > 0 ) ? 
	   ( total_phases + 1 >= _warmup_periods ) :
	   ( ( !_measure_latency || ( lat_chg_exc_class < 0 ) ) &&
	     ( acc_chg_exc_class < 0 ) ) ) {
	cout << "Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
	clear_last = true;
	_sim_state = running;
      }
    } else if(_sim_state == running) {
      if ( ( !_measure_latency || ( lat_chg_exc_class < 0 ) ) &&
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
    
    if ( _measure_latency ) {
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
  
  return ( converged > 0 );
}

bool TrafficManager::Run( )
{
  for ( int sim = 0; sim < _total_sims; ++sim ) {

    _time = 0;

    //remove any pending request from the previous simulations
    _requestsOutstanding.assign(_nodes, 0);
    for (int i=0;i<_nodes;i++) {
      _repliesPending[i].clear();
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

    _UpdateOverallStats();
  }
  
  if(_print_csv_results)
    DisplayOverallStatsCSV();
  else
    DisplayOverallStats();
  
  return true;
}

void TrafficManager::_UpdateOverallStats() {
  for ( int c = 0; c < _classes; ++c ) {
    
    if(_measure_stats[c] == 0) {
      continue;
    }
    
    _overall_min_plat[c]->AddSample( _plat_stats[c]->Min( ) );
    _overall_avg_plat[c]->AddSample( _plat_stats[c]->Average( ) );
    _overall_max_plat[c]->AddSample( _plat_stats[c]->Max( ) );
    _overall_min_tlat[c]->AddSample( _tlat_stats[c]->Min( ) );
    _overall_avg_tlat[c]->AddSample( _tlat_stats[c]->Average( ) );
    _overall_max_tlat[c]->AddSample( _tlat_stats[c]->Max( ) );
    _overall_min_frag[c]->AddSample( _frag_stats[c]->Min( ) );
    _overall_avg_frag[c]->AddSample( _frag_stats[c]->Average( ) );
    _overall_max_frag[c]->AddSample( _frag_stats[c]->Max( ) );
    
    double min, avg;
    _ComputeStats( _accepted_flits[c], &avg, &min );
    _overall_accepted[c]->AddSample( avg );
    _overall_accepted_min[c]->AddSample( min );
    
  }
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

      //statstat
      vector<Router *> rrr = _net[0]->GetRouters();

      for(int i = 0; i<rrr.size(); i++){
	*_stats_out<<"input_grant(:,"<<i+1<<")=[";
	*_stats_out<<rrr[i]->_input_grant_stat;
	*_stats_out<<"];\n";
      }
      for(int i = 0; i<rrr.size(); i++){
	*_stats_out<<"input_request(:,"<<i+1<<")=[";
	*_stats_out<<rrr[i]->_input_request_stat;
	*_stats_out<<"];\n";
      }
      for(int i = 0; i<rrr.size(); i++){
	*_stats_out<<"input_vc_grant(:,"<<i+1<<")=[";
	*_stats_out<<rrr[i]->_input_vc_grant_stat;
	*_stats_out<<"];\n";
      }
      for(int i = 0; i<rrr.size(); i++){
	*_stats_out<<"input_vc_request(:,"<<i+1<<")=[";
	*_stats_out<<rrr[i]->_input_vc_request_stat;
	*_stats_out<<"];\n";
      }
      for(int i = 0; i<rrr.size(); i++){
	*_stats_out<<"output_grant(:,"<<i+1<<")=[";
	*_stats_out<<rrr[i]->_output_grant_stat;
	*_stats_out<<"];\n";
      }

      if(_pri_type == forward_note){
	for(int i = 0; i<_nodes; i++){
	  *_stats_out<<"forward_note_source (:,"<<i+1<<")=[";
	  *_stats_out<<_forward_note_source_stats[i]->Average()<<" ";
	  *_stats_out<<*_forward_note_source_stats[i];
	  *_stats_out<<"];\n";
	}
	for(int i = 0; i<_nodes; i++){
	  *_stats_out<<"forward_note_dest (:,"<<i+1<<")=[";
	  *_stats_out<<_forward_note_dest_stats[i]->Average()<<" ";
	  *_stats_out<<*_forward_note_dest_stats[i];
	  *_stats_out<<"];\n";
	}

      }
    }
  }    
}

void TrafficManager::DisplayOverallStats( ostream & os ) const {

  for ( int c = 0; c < _classes; ++c ) {

    if(_measure_stats[c] == 0) {
      continue;
    }

    os << "====== Traffic class " << c << " ======" << endl;
    
    os << "Overall minimum latency = " << _overall_min_plat[c]->Average( )
       << " (" << _overall_min_plat[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall average latency = " << _overall_avg_plat[c]->Average( )
       << " (" << _overall_avg_plat[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall maximum latency = " << _overall_max_plat[c]->Average( )
       << " (" << _overall_max_plat[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall minimum transaction latency = " << _overall_min_tlat[c]->Average( )
       << " (" << _overall_min_tlat[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall average transaction latency = " << _overall_avg_tlat[c]->Average( )
       << " (" << _overall_avg_tlat[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall maximum transaction latency = " << _overall_max_tlat[c]->Average( )
       << " (" << _overall_max_tlat[c]->NumSamples( ) << " samples)" << endl;
    
    os << "Overall minimum fragmentation = " << _overall_min_frag[c]->Average( )
       << " (" << _overall_min_frag[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall average fragmentation = " << _overall_avg_frag[c]->Average( )
       << " (" << _overall_avg_frag[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall maximum fragmentation = " << _overall_max_frag[c]->Average( )
       << " (" << _overall_max_frag[c]->NumSamples( ) << " samples)" << endl;

    os << "Overall average accepted rate = " << _overall_accepted[c]->Average( )
       << " (" << _overall_accepted[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall min accepted rate = " << _overall_accepted_min[c]->Average( )
       << " (" << _overall_accepted_min[c]->NumSamples( ) << " samples)" << endl;
    
    os << "Average hops = " << _hop_stats[c]->Average( )
       << " (" << _hop_stats[c]->NumSamples( ) << " samples)" << endl;

    os << "Slowest flit = " << _slowest_flit[c] << endl;
  
    
  }
  //this is a rough approximate  from updatepriority()
  cout<<"inversion "<<double(VC::invert_cycles)/VC::total_cycles<<endl;
 
  
}

void TrafficManager::DisplayOverallStatsCSV(ostream & os) const {
  for(int c = 0; c <= _classes; ++c) {
    os << "results:"
       << c
       << "," << _traffic[c]
       << "," << _use_read_write[c]
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
