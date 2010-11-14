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
#include "packet_reply_info.hpp"

TrafficManager::TrafficManager( const Configuration &config, const vector<Network *> & net )
: Module( 0, "traffic_manager" ), _net(net), _empty_network(false), _deadlock_timer(0), _last_id(-1), _last_pid(-1), _timed_mode(false), _warmup_time(-1), _drain_time(-1), _cur_id(0), _cur_pid(0), _time(0)
{

  _sources = _net[0]->NumSources( );
  _dests   = _net[0]->NumDests( );
  _routers = _net[0]->NumRouters( );

  //nodes higher than limit do not produce or receive packets
  //for default limit = sources

  _limit = config.GetInt( "limit" );
  if(_limit == 0){
    _limit = _sources;
  }
  assert(_limit<=_sources);
 
  _duplicate_networks = config.GetInt("physical_subnetworks");
 
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
  } else if ( priority == "none" ) {
    _pri_type = none;
  } else {
    Error( "Unkown priority value: " + priority );
  }

  _replies_inherit_priority = config.GetInt("replies_inherit_priority");

  // ============ Injection VC states  ============ 

  _buf_states.resize(_sources);

  for ( int s = 0; s < _sources; ++s ) {
    ostringstream tmp_name;
    tmp_name << "terminal_buf_state_" << s;
    _buf_states[s].resize(_duplicate_networks);
    for (int a = 0; a < _duplicate_networks; ++a) {
      _buf_states[s][a] = new BufferState( config, this, tmp_name.str( ) );
    }
    tmp_name.str("");
  }
  
  string fn = config.GetStr( "routing_function" );
  if(fn.find("xyyx")!=string::npos || fn.find("xy_yx")!=string::npos ){
    _use_xyyx = true;
  } else {
    _use_xyyx = false;
  }


  // ============ Traffic ============ 

  _classes = config.GetInt("classes");

  _use_read_write = (config.GetInt("use_read_write") > 0);
  _read_request_size = config.GetInt("read_request_size");
  _read_reply_size = config.GetInt("read_reply_size");
  _write_request_size = config.GetInt("write_request_size");
  _write_reply_size = config.GetInt("write_reply_size");
  if(_use_read_write) {
    _packet_size = (_read_request_size + _read_reply_size +
		    _write_request_size + _write_reply_size) / 2;
  } else {
    _packet_size = config.GetInt( "const_flits_per_packet" );
  }

  _load = config.GetFloat( "injection_rate" ); 
  if(config.GetInt("injection_rate_uses_flits")) {
    _load /= _packet_size;
  }



  // ============ Injection queues ============ 

  _qtime.resize(_sources);
  _qdrained.resize(_sources);
  _partial_packets.resize(_sources);

  for ( int s = 0; s < _sources; ++s ) {
    _qtime[s].resize(_classes);
    _qdrained[s].resize(_classes);
    _partial_packets[s].resize(_classes);
    for (int a = 0; a < _classes; ++a)
      _partial_packets[s][a].resize(_duplicate_networks);
  }

  _total_in_flight_flits.resize(_classes);
  _measured_in_flight_flits.resize(_classes);
  _retired_packets.resize(_classes);

  _voqing = config.GetInt( "voq" );

  _packets_sent.resize(_sources);
  _batch_size = config.GetInt( "batch_size" );
  _batch_count = config.GetInt( "batch_count" );
  _repliesPending.resize(_sources);
  _requestsOutstanding.resize(_sources);
  _maxOutstanding = config.GetInt ("max_outstanding_requests");  

  if ( _voqing ) {
    _voq.resize(_sources);
    _active_list.resize(_sources);
    _active_vc.resize(_sources);

    for ( int s = 0; s < _sources; ++s ) {
      _voq[s].resize(_dests);
      _active_vc[s].resize(_dests);
    }
  }

  // ============ Statistics ============ 

  _latency_stats.resize(_classes);
  _overall_min_latency.resize(_classes);
  _overall_avg_latency.resize(_classes);
  _overall_max_latency.resize(_classes);

  _tlat_stats.resize(_classes);
  _overall_min_tlat.resize(_classes);
  _overall_avg_tlat.resize(_classes);
  _overall_max_tlat.resize(_classes);

  _frag_stats.resize(_classes);
  _overall_min_frag.resize(_classes);
  _overall_avg_frag.resize(_classes);
  _overall_max_frag.resize(_classes);

  _pair_latency.resize(_classes);
  _pair_tlat.resize(_classes);
  
  _hop_stats.resize(_classes);
  
  _sent_flits.resize(_classes);
  _accepted_flits.resize(_classes);
  
  _overall_accepted.resize(_classes);
  _overall_accepted_min.resize(_classes);

  for ( int c = 0; c < _classes; ++c ) {
    ostringstream tmp_name;
    tmp_name << "latency_stat_" << c;
    _latency_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _latency_stats[c];
    tmp_name.str("");

    tmp_name << "overall_min_latency_stat_" << c;
    _overall_min_latency[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_min_latency[c];
    tmp_name.str("");  
    tmp_name << "overall_avg_latency_stat_" << c;
    _overall_avg_latency[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_avg_latency[c];
    tmp_name.str("");  
    tmp_name << "overall_max_latency_stat_" << c;
    _overall_max_latency[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _overall_max_latency[c];
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

    _pair_latency[c].resize(_sources*_dests);
    _pair_tlat[c].resize(_sources*_dests);

    _sent_flits[c].resize(_sources);
    _accepted_flits[c].resize(_dests);
    
    for ( int i = 0; i < _sources; ++i ) {
      tmp_name << "sent_stat_" << c << "_" << i;
      _sent_flits[c][i] = new Stats( this, tmp_name.str( ) );
      _stats[tmp_name.str()] = _sent_flits[c][i];
      tmp_name.str("");    
      
      for ( int j = 0; j < _dests; ++j ) {
	tmp_name << "pair_latency_stat_" << c << "_" << i << "_" << j;
	_pair_latency[c][i*_dests+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
	_stats[tmp_name.str()] = _pair_latency[c][i*_dests+j];
	tmp_name.str("");
	
	tmp_name << "pair_tlat_stat_" << c << "_" << i << "_" << j;
	_pair_tlat[c][i*_dests+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
	_stats[tmp_name.str()] = _pair_tlat[c][i*_dests+j];
	tmp_name.str("");
      }
    }
    
    for ( int i = 0; i < _dests; ++i ) {
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
  
  _injected_flow.resize(_sources, 0);
  _ejected_flow.resize(_dests, 0);

  _received_flow.resize(_duplicate_networks*_routers, 0);
  _sent_flow.resize(_duplicate_networks*_routers, 0);

  _slowest_flit.resize(_classes, -1);

  // ============ Simulation parameters ============ 

  _total_sims = config.GetInt( "sim_count" );

  _router_map.resize(_duplicate_networks);
  for (int i=0; i < _duplicate_networks; ++i) {
    _router_map[i] = _net[i]->GetRouters();
  }

  //seed the network
  RandomSeed(config.GetInt("seed"));

  string tf = config.GetStr("traffic");
  map<string, tTrafficFunction>::iterator tf_iter = gTrafficFunctionMap.find(tf);
  if(tf_iter == gTrafficFunctionMap.end()) {
    Error("Invalid traffic function: " + tf);
  }
  _traffic_function = tf_iter->second;

  string rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
  map<string, tRoutingFunction>::iterator rf_iter = gRoutingFunctionMap.find(rf);
  if(rf_iter == gRoutingFunctionMap.end()) {
    Error("Invalid routing function: " + rf);
  }
  _routing_function  = rf_iter->second;

  map<string, tInjectionProcess>::iterator ip_iter = gInjectionProcessMap.find(config.GetStr("injection_process"));
  if(ip_iter == gInjectionProcessMap.end()) {
    Error("Invalid injection process: " + config.GetStr("injection_process"));
  }
  _injection_process = ip_iter->second;

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
  _latency_thres = config.GetFloat( "latency_thres" );
  _warmup_threshold = config.GetFloat( "warmup_thres" );
  _stopping_threshold = config.GetFloat( "stopping_thres" );
  _acc_stopping_threshold = config.GetFloat( "acc_stopping_thres" );
  _include_queuing = config.GetInt( "include_queuing" );

  _print_csv_results = config.GetInt( "print_csv_results" );
  _print_vc_stats = config.GetInt( "print_vc_stats" );
  _traffic = config.GetStr( "traffic" ) ;
  _deadlock_warn_timeout = config.GetInt( "deadlock_warn_timeout" );
  _drain_measured_only = config.GetInt( "drain_measured_only" );

  string watch_file = config.GetStr( "watch_file" );
  _LoadWatchList(watch_file);

  string watch_flits_str = config.GetStr("watch_flits");
  vector<string> watch_flits = BookSimConfig::tokenize(watch_flits_str);
  for(int i = 0; i < watch_flits.size(); ++i) {
    _flits_to_watch.insert(atoi(watch_flits[i].c_str()));
  }
  
  string watch_packets_str = config.GetStr("watch_packets");
  vector<string> watch_packets = BookSimConfig::tokenize(watch_packets_str);
  for(int i = 0; i < watch_packets.size(); ++i) {
    _packets_to_watch.insert(atoi(watch_packets[i].c_str()));
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
  
  string flow_out_file = config.GetStr( "flow_out" );
  if(flow_out_file == "") {
    _flow_out = NULL;
  } else if(flow_out_file == "-") {
    _flow_out = &cout;
  } else {
    _flow_out = new ofstream(flow_out_file.c_str());
  }


}

TrafficManager::~TrafficManager( )
{

  for ( int s = 0; s < _sources; ++s ) {
    for (int a = 0; a < _duplicate_networks; ++a) {
      delete _buf_states[s][a];
    }
  }
  
  for ( int c = 0; c < _classes; ++c ) {
    delete _latency_stats[c];
    delete _overall_min_latency[c];
    delete _overall_avg_latency[c];
    delete _overall_max_latency[c];

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
    
    for ( int i = 0; i < _sources; ++i ) {
      delete _sent_flits[c][i];
      
      for ( int j = 0; j < _dests; ++j ) {
	delete _pair_latency[c][i*_dests+j];
	delete _pair_tlat[c][i*_dests+j];
      }
    }
    
    for ( int i = 0; i < _dests; ++i ) {
      delete _accepted_flits[c][i];
    }
    
  }
  
  delete _batch_time;
  delete _overall_batch_time;
  
  if(gWatchOut && (gWatchOut != &cout)) delete gWatchOut;
  if(_stats_out && (_stats_out != &cout)) delete _stats_out;
  if(_flow_out && (_flow_out != &cout)) delete _flow_out;

  PacketReplyInfo::FreeAll();
  Flit::FreeAll();
  Credit::FreeAll();
}


// Decides which subnetwork the flit should go to.
// Is now called once per packet.
// This should change according to number of duplicate networks
int TrafficManager::DivisionAlgorithm (int packet_type) {

  if(packet_type == Flit::ANY_TYPE) {
    return RandomInt(_duplicate_networks-1); // Even distribution.
  } else {
    switch(_duplicate_networks) {
    case 1:
      return 0;
    case 2:
      switch(packet_type) {
      case Flit::WRITE_REQUEST:
      case Flit::READ_REQUEST:
        return 1;
      case Flit::WRITE_REPLY:
      case Flit::READ_REPLY:
        return 0;
      default:
	assert(false);
	return -1;
      }
    case 4:
      switch(packet_type) {
      case Flit::WRITE_REQUEST:
        return 0;
      case Flit::READ_REQUEST:
        return 1;
      case Flit::WRITE_REPLY:
        return 2;
      case Flit::READ_REPLY:
        return 3;
      default:
	assert(false);
	return -1;
      }
    default:
      assert(false);
      return -1;
    }
  }
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
		<< ", lat = " << f->atime - f->time
		<< ", src = " << f->src 
		<< ", dest = " << f->dest
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
		   << "Retiring transation " //<< (transation id) 
		   << " (lat = " << f->atime - head->ttime
		   << ", src = " << head->src 
		   << ", dest = " << head->dest
		   << ")." << endl;
      }
      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
	//received a reply
	_requestsOutstanding[dest]--;
      } else if(f->type == Flit::ANY_TYPE && _sim_mode == batch) {
	//received a reply
	_requestsOutstanding[f->src]--;
      }
      
    }

    // Only record statistics once per packet (at tail)
    // and based on the simulation state
    if ( ( _sim_state == warming_up ) || f->record ) {
      
      _hop_stats[f->cl]->AddSample( f->hops );

      if((_slowest_flit[f->cl] < 0) ||
	 (_latency_stats[f->cl]->Max() < (f->atime - f->time)))
	_slowest_flit[f->cl] = f->id;
      _latency_stats[f->cl]->AddSample( f->atime - f->time);
      _frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );
      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY || f->type == Flit::ANY_TYPE)
	_tlat_stats[f->cl]->AddSample( f->atime - f->ttime );
   
      _pair_latency[f->cl][f->src*_dests+dest]->AddSample( f->atime - f->time );
      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY)
	_pair_tlat[f->cl][dest*_dests+f->src]->AddSample( f->atime - f->ttime );
      else if(f->type == Flit::ANY_TYPE)
	_pair_tlat[f->cl][f->src*_dests+dest]->AddSample( f->atime - f->ttime );
      
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
  if(_sim_mode == batch){ //batch mode
    if(_use_read_write){ //read write packets
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
	
      } else if ((_packets_sent[source] >= _batch_size && !_timed_mode) || 
		 (_requestsOutstanding[source] >= _maxOutstanding)) {
	result = 0;
      } else {
	
	//coin toss to determine request type.
	result = (RandomFloat() < 0.5) ? -2 : -1;
	
	_packets_sent[source]++;
	_requestsOutstanding[source]++;
      } 
    } else { //normal
      if ((_packets_sent[source] >= _batch_size && !_timed_mode) || 
		 (_requestsOutstanding[source] >= _maxOutstanding)) {
	result = 0;
      } else {
	result = _packet_size;
	_packets_sent[source]++;
	//here is means, how many flits can be waiting in the queue
	_requestsOutstanding[source]++;
      } 
    } 
  } else { //injection rate mode
    if(_use_read_write){ //use read and write
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
      } else {

	//produce a packet
	if(_injection_process( source, _load )){
	
	  //coin toss to determine request type.
	  result = (RandomFloat() < 0.5) ? -2 : -1;

	} else {
	  result = 0;
	}
      } 
    } else { //normal mode
      return _injection_process( source, _load ) ? 1 : 0;
    } 
  }
  return result;
}

void TrafficManager::_GeneratePacket( int source, int stype, 
				      int cl, int time )
{
  assert(stype!=0);

  //refusing to generate packets for nodes greater than limit
  if(source >=_limit){
    return ;
  }

  Flit::FlitType packet_type = Flit::ANY_TYPE;
  int size = _packet_size; //input size 
  int ttime = time;
  int packet_destination = _traffic_function(source, _limit);
  bool record = false;
  if(_use_read_write){
    if(stype < 0) {
      if (stype ==-1) {
	packet_type = Flit::READ_REQUEST;
	size = _read_request_size;
      } else if (stype == -2) {
	packet_type = Flit::WRITE_REQUEST;
	size = _write_request_size;
      } else {
	ostringstream err;
	err << "Invalid packet type: " << packet_type;
	Error( err.str( ) );
      }
    } else  {
      map<int, PacketReplyInfo*>::iterator iter = _repliesDetails.find(stype);
      PacketReplyInfo* rinfo = iter->second;
      
      if (rinfo->type == Flit::READ_REQUEST) {//read reply
	size = _read_reply_size;
	packet_type = Flit::READ_REPLY;
      } else if(rinfo->type == Flit::WRITE_REQUEST) {  //write reply
	size = _write_reply_size;
	packet_type = Flit::WRITE_REPLY;
      } else {
	ostringstream err;
	err << "Invalid packet type: " << rinfo->type;
	Error( err.str( ) );
      }
      packet_destination = rinfo->source;
      time = rinfo->time;
      ttime = rinfo->ttime;
      record = rinfo->record;
      _repliesDetails.erase(iter);
      rinfo->Free();
    }
  }

  if ((packet_destination <0) || (packet_destination >= _dests)) {
    ostringstream err;
    err << "Incorrect packet destination " << packet_destination
	<< " for stype " << packet_type;
    Error( err.str( ) );
  }

  if ( ( _sim_state == running ) ||
       ( ( _sim_state == draining ) && ( time < _drain_time ) ) ) {
    record = true;
  }

  int subnetwork = DivisionAlgorithm(packet_type);
  
  bool watch = gWatchOut && (_packets_to_watch.count(_cur_pid) > 0);
  
  if ( watch ) { 
    *gWatchOut << GetSimTime() << " | "
		<< "node" << source << " | "
		<< "Enqueuing packet " << _cur_pid
		<< " at time " << time
		<< "." << endl;
  }
  
  for ( int i = 0; i < size; ++i ) {
    Flit * f  = Flit::New();
    f->id     = _cur_id++;
    assert(_cur_id);
    f->pid    = _cur_pid;
    f->watch  = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
    f->subnetwork = subnetwork;
    f->src    = source;
    f->time   = time;
    f->ttime  = ttime;
    f->record = record;
    f->cl     = cl;

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
      //obliviously assign a packet to xy or yx route
      if(_use_xyyx){
	if(RandomInt(1)){
	  f->x_then_y = true;
	} else {
	  f->x_then_y = false;
	}
      }
    } else {
      f->head = false;
      f->dest = -1;
    }
    switch( _pri_type ) {
    case class_based:
      f->pri = cl;
      assert(f->pri >= 0);
      break;
    case age_based:
      f->pri = numeric_limits<int>::max() - (_replies_inherit_priority ? ttime : time);
      assert(f->pri >= 0);
      break;
    case sequence_based:
      f->pri = numeric_limits<int>::max() - _packets_sent[source];
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

    _partial_packets[source][cl][subnetwork].push_back( f );
  }
  ++_cur_pid;
  assert(_cur_pid);
}

void TrafficManager::_BatchInject(){
  
  // Receive credits and inject new traffic
  for ( int input = 0; input < _sources; ++input ) {
    for (int i = 0; i < _duplicate_networks; ++i) {
      Credit * cred = _net[i]->ReadCredit( input );
      if ( cred ) {
        _buf_states[input][i]->ProcessCredit( cred );
        cred->Free();
      }
    }
    
    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (input,class)
      // that is currently empty
      if ( (_duplicate_networks > 1) || _partial_packets[input][c][0].empty() ) {
	// For multiple networks, always try. Hard to check for empty queue - many subnetworks potentially.
	bool generated = false;
	  
	if ( !_empty_network ) {
	  while( !generated && ( _qtime[input][c] <= _time ) ) {
	    int stype = _IssuePacket( input, c );

	    if ( stype != 0 ) {
	      _GeneratePacket( input, stype, c, 
			       _include_queuing==1 ? 
			       _qtime[input][c] : _time );
	      generated = true;
	    }
	    ++_qtime[input][c];
	  }
	  
	  if ( ( _sim_state == draining ) && 
	       ( _qtime[input][c] > _drain_time ) ) {
	    _qdrained[input][c] = true;
	  }
	}
      }
    }

    // Now, check partially issued packets to
    // see if they can be issued
    for (int i = 0; i < _duplicate_networks; ++i) {
      Flit * f = NULL;
      for (int c = _classes - 1; c >= 0; --c) {
        if (!_partial_packets[input][c][i].empty()) {
	  f = _partial_packets[input][c][i].front( );
	  if ( f->head && f->vc == -1) { // Find first available VC
	    
	    if(_use_xyyx){
	      f->vc = _buf_states[input][i]->FindAvailable( f->type ,f->x_then_y);
	    } else {
	      f->vc = _buf_states[input][i]->FindAvailable( f->type );
	    }
	    if ( f->vc != -1 ) {
	      _buf_states[input][i]->TakeBuffer( f->vc );
	    }
	  }
	  
	  if ( ( f->vc != -1 ) &&
	       ( !_buf_states[input][i]->IsFullFor( f->vc ) ) ) {
	    
	    _partial_packets[input][c][i].pop_front( );
	    _buf_states[input][i]->SendingFlit( f );
	    
	    if(_pri_type == network_age_based) {
	      f->pri = numeric_limits<int>::max() - _time;
	      assert(f->pri >= 0);
	    }
	    
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | "
			 << "node" << input << " | "
			 << "Injecting flit " << f->id
			 << " at time " << _time
			 << " with priority " << f->pri
			 << "." << endl;
	    }
	    
	    // Pass VC "back"
	    if ( !_partial_packets[input][c][i].empty( ) && !f->tail ) {
	      Flit * nf = _partial_packets[input][c][i].front( );
	      nf->vc = f->vc;
	    }
	    
	    ++_injected_flow[input];

	    break;

	  } else {
	    f = NULL;
	  }
	}
      }
      _net[i]->WriteFlit( f, input );
      if( _sim_state == running )
	for(int c = 0; c < _classes; ++c) {
	  _sent_flits[c][input]->AddSample((f && (f->cl == c)) ? 1 : 0);
	}
    }
  }
}


void TrafficManager::_NormalInject(){

  // Receive credits and inject new traffic
  for ( int input = 0; input < _sources; ++input ) {
    for (int i = 0; i < _duplicate_networks; ++i) {
      Credit * cred = _net[i]->ReadCredit( input );
      if ( cred ) {
        _buf_states[input][i]->ProcessCredit( cred );
        cred->Free();
      }
    }
    
    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (input,class)
      // that is currently empty
      if ( (_duplicate_networks > 1) || _partial_packets[input][c][0].empty() ) {
      // For multiple networks, always flip coin because now you have multiple send buffers so you can't choose one only to check.
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
	    if(_use_read_write && (stype > 0)){
	      
	    } else {
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

    // Now, check partially issued packets to
    // see if they can be issued
    for (int i = 0; i < _duplicate_networks; ++i) {
      Flit * f = NULL;
      for (int c = _classes - 1; c >= 0; --c) {
	if ( !_partial_packets[input][c][i].empty( ) ) {
	  f = _partial_packets[input][c][i].front( );
	  if ( f->head && f->vc == -1) { // Find first available VC
	    
	    if ( _voqing ) {
	      if ( _buf_states[input][i]->IsAvailableFor( f->dest ) ) {
		f->vc = f->dest;
	      }
	    } else {
	      
	      if(_use_xyyx){
		f->vc = _buf_states[input][i]->FindAvailable( f->type ,f->x_then_y);
	      } else {
		f->vc = _buf_states[input][i]->FindAvailable( f->type );
	      }
	    }
	    
	    if ( f->vc != -1 ) {
	      _buf_states[input][i]->TakeBuffer( f->vc );
	    }
	  }
	  
	  if ( ( f->vc != -1 ) &&
	       ( !_buf_states[input][i]->IsFullFor( f->vc ) ) ) {
	    
	    _partial_packets[input][c][i].pop_front( );
	    _buf_states[input][i]->SendingFlit( f );
	    
	    if(_pri_type == network_age_based) {
	      f->pri = numeric_limits<int>::max() - _time;
	      assert(f->pri >= 0);
	    }
	    
	    if(f->watch) {
	      *gWatchOut << GetSimTime() << " | "
			 << "node" << input << " | "
			 << "Injecting flit " << f->id
			 << " at time " << _time
			 << " with priority " << f->pri
			 << "." << endl;
	    }
	    
	    // Pass VC "back"
	    if ( !_partial_packets[input][c][i].empty( ) && !f->tail ) {
	      Flit * nf = _partial_packets[input][c][i].front( );
	      nf->vc = f->vc;
	    }
	    
	    ++_injected_flow[input];

	    break;

	  } else {
	    f = NULL;
	  }
	}
      }
      _net[i]->WriteFlit( f, input );
      if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	for(int c = 0; c < _classes; ++c) {
	  _sent_flits[c][input]->AddSample((f && (f->cl == c)) ? 1 : 0);
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

  if(_sim_mode == batch){
    _BatchInject();
  } else {
    _NormalInject();
  }

  //advance networks
  for (int i = 0; i < _duplicate_networks; ++i) {
    _net[i]->Evaluate( );
  }

  for (int i = 0; i < _duplicate_networks; ++i) {
    _net[i]->Update( );
  }
  


  for (int i = 0; i < _duplicate_networks; ++i) {
    // Eject traffic and send credits
    for ( int output = 0; output < _dests; ++output ) {
      Flit * f = _net[i]->ReadFlit( output );

      if ( f ) {
	++_ejected_flow[output];
	f->atime = _time;
        if ( f->watch ) {
	  *gWatchOut << GetSimTime() << " | "
		      << "node" << output << " | "
		      << "Ejecting flit " << f->id
		      << " (packet " << f->pid << ")"
		      << " from VC " << f->vc
		      << "." << endl;
	  *gWatchOut << GetSimTime() << " | "
		      << "node" << output << " | "
		      << "Injecting credit for VC " << f->vc << "." << endl;
        }
      
        Credit * cred = Credit::New(1);
        cred->vc[0] = f->vc;
        cred->vc_cnt = 1;
	cred->dest_router = f->from_router;
        _net[i]->WriteCredit( cred, output );
      
        if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	  for(int c = 0; c < _classes; ++c) {
	    _accepted_flits[c][output]->AddSample( (f && (f->cl == c)) ? 1 : 0 );
	  }

        _RetireFlit( f, output );

      } else {
        _net[i]->WriteCredit( 0, output );
        if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	  for(int c = 0; c < _classes; ++c) {
	    _accepted_flits[c][output]->AddSample( 0 );
	  }
      }
    }

    for(int j = 0; j < _routers; ++j) {
      _received_flow[i*_routers+j] += _router_map[i][j]->GetReceivedFlits();
      _sent_flow[i*_routers+j] += _router_map[i][j]->GetSentFlits();
      _router_map[i][j]->ResetFlitStats();
    }
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

      for ( int s = 0; s < _sources; ++s ) {
	if ( !_qdrained[s][c] ) {
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

  for ( int c = 0; c < _classes; ++c ) {

    _latency_stats[c]->Clear( );
    _tlat_stats[c]->Clear( );
    _frag_stats[c]->Clear( );
    _slowest_flit[c] = -1;
  
    for ( int i = 0; i < _sources; ++i ) {
      _sent_flits[c][i]->Clear( );
      
      for ( int j = 0; j < _dests; ++j ) {
	_pair_latency[c][i*_dests+j]->Clear( );
	_pair_tlat[c][i*_dests+j]->Clear( );
      }
    }

    for ( int i = 0; i < _dests; ++i ) {
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

  for ( int d = 0; d < _dests; ++d ) {
    double curr = stats[d]->Average( );
    if ( curr < *min ) {
      *min = curr;
      dmin = d;
    }
    *avg += curr;
  }

  *avg /= (double)_dests;

  return dmin;
}

void TrafficManager::_DisplayRemaining( ) const 
{
  for(int c = 0; c < _classes; ++c) {

    map<int, Flit *>::const_iterator iter;
    int i;

    cout << "Class " << c << ":" << endl;

    cout << "Remaining flits: ";
    for ( iter = _total_in_flight_flits[c].begin( ), i = 0;
	  ( iter != _total_in_flight_flits[c].end( ) ) && ( i < 10 );
	  iter++, i++ ) {
      cout << iter->first << " ";
    }
    if(_total_in_flight_flits[c].size() > 10)
      cout << "[...] ";
    
    cout << "(" << _total_in_flight_flits[c].size() << " flits)" << endl;
    
    cout << "Measured flits: ";
    for ( iter = _measured_in_flight_flits[c].begin( ), i = 0;
	  ( iter != _measured_in_flight_flits[c].end( ) ) && ( i < 10 );
	  iter++, i++ ) {
      cout << iter->first << " ";
    }
    if(_measured_in_flight_flits[c].size() > 10)
      cout << "[...] ";
    
    cout << "(" << _measured_in_flight_flits[c].size() << " flits)" << endl;
    
  }
}

bool TrafficManager::_SingleSim( )
{
  _time = 0;
  //remove any pending request from the previous simulations
  for (int i=0;i<_sources;i++) {
    _requestsOutstanding[i] = 0;

    while (!_repliesPending[i].empty()) {
      _repliesPending[i].pop_front();
    }
  }

  //reset queuetime for all sources
  for ( int s = 0; s < _sources; ++s ) {
    for ( int c = 0; c < _classes; ++c  ) {
      _qtime[s][c]    = 0;
      _qdrained[s][c] = false;
    }
  }

  // warm-up ...
  // reset stats, all packets after warmup_time marked
  // converge
  // draing, wait until all packets finish
  _sim_state    = warming_up;
  
  _ClearStats( );

  bool clear_last = false;
  int total_phases  = 0;
  int converged = 0;

  if (_sim_mode == batch && _timed_mode){
    _sim_state = running;
    while(_time<_sample_period){
      _Step();
      if ( _time % 10000 == 0 ) {
	cout << _sim_state << endl;
	if(_stats_out)
	  *_stats_out << "%=================================" << endl;
	
	for(int c = 0; c < _classes; ++c) {

	  double cur_latency = _latency_stats[c]->Average( );
	  double min, avg;
	  int dmin = _ComputeStats( _accepted_flits[c], &avg, &min );
	  
	  cout << "Class " << c << ":" << endl;

	  cout << "Minimum latency = " << _latency_stats[c]->Min( ) << endl;
	  cout << "Average latency = " << cur_latency << endl;
	  cout << "Maximum latency = " << _latency_stats[c]->Max( ) << endl;
	  cout << "Average fragmentation = " << _frag_stats[c]->Average( ) << endl;
	  cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;

	  cout << "Total in-flight flits = " << _total_in_flight_flits[c].size() << " (" << _measured_in_flight_flits[c].size() << " measured)" << endl;

	  if(_stats_out)
	    *_stats_out << "lat(" << c << ") = " << cur_latency << ";" << endl
			<< "lat_hist(" << c << ",:) = " << *_latency_stats[c] << ";" << endl
			<< "frag_hist(" << c << ",:) = " << *_frag_stats[c] << ";" << endl;
	} 
      }
    }
    converged = 1;

  } else if(_sim_mode == batch && !_timed_mode){//batch mode   
    while(total_phases < _batch_count) {
      for (int i = 0; i < _sources; i++)
	_packets_sent[i] = 0;
      _last_id = -1;
      _last_pid = -1;
      _sim_state = running;
      int start_time = _time;
      int min_packets_sent = 0;
      while(min_packets_sent < _batch_size){
	_Step();
	min_packets_sent = _packets_sent[0];
	for(int i = 1; i < _sources; ++i) {
	  if(_packets_sent[i] < min_packets_sent)
	    min_packets_sent = _packets_sent[i];
	}
	if(_flow_out) {
	  *_flow_out << "injected_flow(" << _time << ",:) = " << _injected_flow << ";" << endl;
	  *_flow_out << "ejected_flow(" << _time << ",:) = " << _ejected_flow << ";" << endl;
	  *_flow_out << "received_flow(" << _time << ",:) = " << _received_flow << ";" << endl;
	  *_flow_out << "sent_flow(" << _time << ",:) = " << _sent_flow << ";" << endl;
	  *_flow_out << "packets_sent(" << _time << ",:) = " << _packets_sent << ";" << endl;
	}
	_injected_flow.assign(_sources, 0);
	_ejected_flow.assign(_dests, 0);
	_received_flow.assign(_duplicate_networks*_routers, 0);
	_sent_flow.assign(_duplicate_networks*_routers, 0);
      }
      cout << "Batch " << total_phases + 1 << " ("<<_batch_size  <<  " flits) sent. Time used is " << _time - start_time << " cycles." << endl;
      cout << "Draining the Network...................\n";
      _sim_state = draining;
      _drain_time = _time;
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
	if(_flow_out) {
	  *_flow_out << "injected_flow(" << _time << ",:) = " << _injected_flow << ";" << endl;
	  *_flow_out << "ejected_flow(" << _time << ",:) = " << _ejected_flow << ";" << endl;
	  *_flow_out << "received_flow(" << _time << ",:) = " << _received_flow << ";" << endl;
	  *_flow_out << "sent_flow(" << _time << ",:) = " << _sent_flow << ";" << endl;
	}
	_injected_flow.assign(_sources, 0);
	_ejected_flow.assign(_dests, 0);
	_received_flow.assign(_duplicate_networks*_routers, 0);
	_sent_flow.assign(_duplicate_networks*_routers, 0);
	++empty_steps;
	
	if ( empty_steps % 1000 == 0 ) {
	  _DisplayRemaining( ); 
	  cout << ".";
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
      cout << endl;
      cout << "Batch " << total_phases + 1 << " ("<<_batch_size  <<  " flits) received. Time used is " << _time - _drain_time << " cycles. Last packet was " << _last_pid << ", last flit was " << _last_id << "." <<endl;
      _batch_time->AddSample(_time - start_time);
      cout << _sim_state << endl;
      if(_stats_out)
	*_stats_out << "%=================================" << endl;
      double cur_latency = _latency_stats[0]->Average( );
      double min, avg;
      int dmin = _ComputeStats( _accepted_flits[0], &avg, &min );
      
      cout << "Batch duration = " << _time - start_time << endl;
      cout << "Minimum latency = " << _latency_stats[0]->Min( ) << endl;
      cout << "Average latency = " << cur_latency << endl;
      cout << "Maximum latency = " << _latency_stats[0]->Max( ) << endl;
      cout << "Average fragmentation = " << _frag_stats[0]->Average( ) << endl;
      cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
      if(_stats_out) {
	*_stats_out << "batch_time(" << total_phases + 1 << ") = " << _time << ";" << endl
		    << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl
		    << "lat_hist(" << total_phases + 1 << ",:) = "
		    << *_latency_stats[0] << ";" << endl
		    << "frag_hist(" << total_phases + 1 << ",:) = "
		    << *_frag_stats[0] << ";" << endl
		    << "pair_sent(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _sources; ++i) {
	  for(int j = 0; j < _dests; ++j) {
	    *_stats_out << _pair_latency[0][i*_dests+j]->NumSamples( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "pair_lat(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _sources; ++i) {
	  for(int j = 0; j < _dests; ++j) {
	    *_stats_out << _pair_latency[0][i*_dests+j]->Average( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "pair_tlat(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _sources; ++i) {
	  for(int j = 0; j < _dests; ++j) {
	    *_stats_out << _pair_tlat[0][i*_dests+j]->Average( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "sent(" << total_phases + 1 << ",:) = [ ";
	for ( int d = 0; d < _dests; ++d ) {
	  *_stats_out << _sent_flits[0][d]->Average( ) << " ";
	}
	*_stats_out << "];" << endl
		    << "accepted(" << total_phases + 1 << ",:) = [ ";
	for ( int d = 0; d < _dests; ++d ) {
	  *_stats_out << _accepted_flits[0][d]->Average( ) << " ";
	}
	*_stats_out << "];" << endl;
      }
      ++total_phases;
    }
    converged = 1;
  } else { 
    //once warmed up, we require 3 converging runs
    //to end the simulation 
    vector<double> prev_latency(_classes, 0.0);
    vector<double> prev_accepted(_classes, 0.0);
    while( ( total_phases < _max_samples ) && 
	   ( ( _sim_state != running ) || 
	     ( converged < 3 ) ) ) {

      if ( clear_last || (( ( _sim_state == warming_up ) && ( ( total_phases % 2 ) == 0 ) )) ) {
	clear_last = false;
	_ClearStats( );
      }
      
      
      for ( int iter = 0; iter < _sample_period; ++iter ) {
	_Step( );
	if(_flow_out) {
	  *_flow_out << "injected_flow(" << _time << ",:) = " << _injected_flow << ";" << endl;
	  *_flow_out << "ejected_flow(" << _time << ",:) = " << _ejected_flow << ";" << endl;
	  *_flow_out << "received_flow(" << _time << ",:) = " << _received_flow << ";" << endl;
	  *_flow_out << "sent_flow(" << _time << ",:) = " << _sent_flow << ";" << endl;
	}
	_injected_flow.assign(_sources, 0);
	_ejected_flow.assign(_dests, 0);
	_received_flow.assign(_duplicate_networks*_routers, 0);
	_sent_flow.assign(_duplicate_networks*_routers, 0);
      } 
      
      cout << _sim_state << endl;
      if(_stats_out)
	*_stats_out << "%=================================" << endl;

      double max_latency_change = 0.0;
      double max_accepted_change = 0.0;

      for(int c = 0; c < _classes; ++c) {

	double cur_latency = _latency_stats[c]->Average( );
	int dmin;
	double min, avg;
	dmin = _ComputeStats( _accepted_flits[c], &avg, &min );
	double cur_accepted = avg;

	cout << "Class " << c << ":" << endl;

	cout << "Minimum latency = " << _latency_stats[c]->Min( ) << endl;
	cout << "Average latency = " << cur_latency << endl;
	cout << "Maximum latency = " << _latency_stats[c]->Max( ) << endl;
	cout << "Average fragmentation = " << _frag_stats[c]->Average( ) << endl;
	cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
	cout << "Total in-flight flits = " << _total_in_flight_flits[c].size() << " (" << _measured_in_flight_flits[c].size() << " measured)" << endl;
	if(_stats_out) {
	  *_stats_out << "lat(" << c << ") = " << cur_latency << ";" << endl
		    << "lat_hist(" << c << ",:) = " << *_latency_stats[c] << ";" << endl
		    << "frag_hist(" << c << ",:) = " << *_frag_stats[c] << ";" << endl
		    << "pair_sent(" << c << ",:) = [ ";
	  for(int i = 0; i < _sources; ++i) {
	    for(int j = 0; j < _dests; ++j) {
	      *_stats_out << _pair_latency[c][i*_dests+j]->NumSamples( ) << " ";
	    }
	  }
	  *_stats_out << "];" << endl
		      << "pair_lat(" << c << ",:) = [ ";
	  for(int i = 0; i < _sources; ++i) {
	    for(int j = 0; j < _dests; ++j) {
	      *_stats_out << _pair_latency[c][i*_dests+j]->Average( ) << " ";
	    }
	  }
	  *_stats_out << "];" << endl
		      << "pair_lat(" << c << ",:) = [ ";
	  for(int i = 0; i < _sources; ++i) {
	    for(int j = 0; j < _dests; ++j) {
	      *_stats_out << _pair_tlat[c][i*_dests+j]->Average( ) << " ";
	    }
	  }
	  *_stats_out << "];" << endl
		      << "sent(" << c << ",:) = [ ";
	  for ( int d = 0; d < _dests; ++d ) {
	    *_stats_out << _sent_flits[c][d]->Average( ) << " ";
	  }
	  *_stats_out << "];" << endl
		      << "accepted(" << c << ",:) = [ ";
	  for ( int d = 0; d < _dests; ++d ) {
	    *_stats_out << _accepted_flits[c][d]->Average( ) << " ";
	  }
	  *_stats_out << "];" << endl;
	  *_stats_out << "inflight(" << c << ") = " << _total_in_flight_flits[c].size() << ";" << endl;
	}
	
	double latency_change = fabs( ( cur_latency - prev_latency[c] ) / cur_latency );
	prev_latency[c] = cur_latency;
	cout << "latency change    = " << latency_change << endl;
	if(latency_change > max_latency_change) {
	  max_latency_change = latency_change;
	}
	double accepted_change = fabs( ( cur_accepted - prev_accepted[c] ) / cur_accepted );
	prev_accepted[c] = cur_accepted;
	cout << "throughput change = " << accepted_change << endl;
	if(accepted_change > max_accepted_change) {
	  max_accepted_change = accepted_change;
	}
	
      }

      // Fail safe for latency mode, throughput will ust continue
      if ( _sim_mode == latency ) {

	double acc_latency = 0.0;
	int acc_count = 0;
	for(int c = 0; c < _classes; c++) {
	  
	  acc_latency += _latency_stats[c]->Sum();
	  acc_count += _latency_stats[c]->NumSamples();
	  
	  map<int, Flit *>::const_iterator iter;
	  for(iter = _total_in_flight_flits[c].begin(); 
	      iter != _total_in_flight_flits[c].end(); 
	      iter++) {
	    acc_latency += _time - iter->second->time;
	    acc_count++;
	  }
	  
	}
	
	double avg_latency = (double)acc_latency / (double)acc_count;
	if(avg_latency > _latency_thres) {
	  cout << "Average latency " << avg_latency << " exceeded " << _latency_thres << " cycles. Aborting simulation." << endl;
	  converged = 0; 
	  _sim_state = warming_up;
	  break;
	}
      }

      if ( _sim_state == warming_up ) {
	if ( _warmup_periods == 0 ) {
	  if ( _sim_mode == latency ) {
	    if ( ( max_latency_change < _warmup_threshold ) &&
		 ( max_accepted_change < _warmup_threshold ) ) {
	      cout << "Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
	      clear_last = true;
	      _sim_state = running;
	    }
	  } else {
	    if ( max_accepted_change < _warmup_threshold ) {
	      cout << "Warmed up ..." << "Time used is " << _time << " cycles" << endl;
	      clear_last = true;
	      _sim_state = running;
	    }
	  }
	} else {
	  if ( total_phases + 1 >= _warmup_periods ) {
	    cout << "Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
	    clear_last = true;
	    _sim_state = running;
	  }
	}
      } else if ( _sim_state == running ) {
	if ( _sim_mode == latency ) {
	  if ( ( max_latency_change < _stopping_threshold ) &&
	       ( max_accepted_change < _acc_stopping_threshold ) ) {
	    ++converged;
	  } else {
	    converged = 0;
	  }
	} else {
	  if ( max_accepted_change < _acc_stopping_threshold ) {
	    ++converged;
	  } else {
	    converged = 0;
	  }
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
	  if(_flow_out) {
	    *_flow_out << "injected_flow(" << _time << ",:) = " << _injected_flow << ";" << endl;
	    *_flow_out << "ejected_flow(" << _time << ",:) = " << _ejected_flow << ";" << endl;
	    *_flow_out << "received_flow(" << _time << ",:) = " << _received_flow << ";" << endl;
	    *_flow_out << "sent_flow(" << _time << ",:) = " << _sent_flow << ";" << endl;
	  }
	  _injected_flow.assign(_sources, 0);
	  _ejected_flow.assign(_dests, 0);
	  _received_flow.assign(_duplicate_networks*_routers, 0);
	  _sent_flow.assign(_duplicate_networks*_routers, 0);
	  ++empty_steps;
	
	  if ( empty_steps % 1000 == 0 ) {
	    
	    double acc_latency = 0.0;
	    int acc_count = 0;
	    for(int c = 0; c < _classes; c++) {

	      acc_latency += _latency_stats[c]->Sum();
	      acc_count += _latency_stats[c]->NumSamples();

	      map<int, Flit *>::const_iterator iter;
	      for(iter = _measured_in_flight_flits[c].begin(); 
		  iter != _measured_in_flight_flits[c].end(); 
		  iter++) {
		acc_latency += _time - iter->second->time;
		acc_count++;
	      }

	    }
	    
	    double avg_latency = (double)acc_latency / (double)acc_count;
	    if(avg_latency > _latency_thres) {
	      cout << "Average latency " << avg_latency << " exceeded " << _latency_thres << " cycles. Aborting simulation." << endl;
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

      if(_flow_out) {
	*_flow_out << "injected_flow(" << _time << ",:) = " << _injected_flow << ";" << endl;
	*_flow_out << "ejected_flow(" << _time << ",:) = " << _ejected_flow << ";" << endl;
	*_flow_out << "received_flow(" << _time << ",:) = " << _received_flow << ";" << endl;
	*_flow_out << "sent_flow(" << _time << ",:) = " << _sent_flow << ";" << endl;
      }
      _injected_flow.assign(_sources, 0);
      _ejected_flow.assign(_dests, 0);
      _received_flow.assign(_duplicate_networks*_routers, 0);
      _sent_flow.assign(_duplicate_networks*_routers, 0);
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
    _empty_network = false;
  }

  return ( converged > 0 );
}

bool TrafficManager::Run( )
{
  for ( int sim = 0; sim < _total_sims; ++sim ) {
    if ( !_SingleSim( ) ) {
      cout << "Simulation unstable, ending ..." << endl;
      return false;
    }
    //for the love of god don't ever say "Time taken" anywhere else
    //the power script depend on it
    cout << "Time taken is " << _time << " cycles" <<endl; 
    for ( int c = 0; c < _classes; ++c ) {
      _overall_min_latency[c]->AddSample( _latency_stats[c]->Min( ) );
      _overall_avg_latency[c]->AddSample( _latency_stats[c]->Average( ) );
      _overall_max_latency[c]->AddSample( _latency_stats[c]->Max( ) );
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

      if(_sim_mode == batch)
	_overall_batch_time->AddSample(_batch_time->Sum( ));
    }
  }
  
  DisplayStats();
  if(_print_vc_stats) {
    if(_print_csv_results) {
      cout << "vc_stats:"
	   << _traffic
	   << "," << _packet_size
	   << "," << _load
	   << ",";
    }
    VC::DisplayStats(_print_csv_results);
  }
  return true;
}

void TrafficManager::DisplayStats() {
  for ( int c = 0; c < _classes; ++c ) {

    if(_print_csv_results) {
      cout << "results:"
	   << c
	   << "," << _traffic
	   << "," << _use_read_write
	   << "," << _packet_size
	   << "," << _load
	   << "," << _overall_min_latency[c]->Average( )
	   << "," << _overall_avg_latency[c]->Average( )
	   << "," << _overall_max_latency[c]->Average( )
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

    cout << "====== Traffic class " << c << " ======" << endl;
    
    cout << "Overall minimum latency = " << _overall_min_latency[c]->Average( )
	 << " (" << _overall_min_latency[c]->NumSamples( ) << " samples)" << endl;
    cout << "Overall average latency = " << _overall_avg_latency[c]->Average( )
	 << " (" << _overall_avg_latency[c]->NumSamples( ) << " samples)" << endl;
    cout << "Overall maximum latency = " << _overall_max_latency[c]->Average( )
	 << " (" << _overall_max_latency[c]->NumSamples( ) << " samples)" << endl;
    cout << "Overall minimum transaction latency = " << _overall_min_tlat[c]->Average( )
	 << " (" << _overall_min_tlat[c]->NumSamples( ) << " samples)" << endl;
    cout << "Overall average transaction latency = " << _overall_avg_tlat[c]->Average( )
	 << " (" << _overall_avg_tlat[c]->NumSamples( ) << " samples)" << endl;
    cout << "Overall maximum transaction latency = " << _overall_max_tlat[c]->Average( )
	 << " (" << _overall_max_tlat[c]->NumSamples( ) << " samples)" << endl;
    
    cout << "Overall minimum fragmentation = " << _overall_min_frag[c]->Average( )
	 << " (" << _overall_min_frag[c]->NumSamples( ) << " samples)" << endl;
    cout << "Overall average fragmentation = " << _overall_avg_frag[c]->Average( )
	 << " (" << _overall_avg_frag[c]->NumSamples( ) << " samples)" << endl;
    cout << "Overall maximum fragmentation = " << _overall_max_frag[c]->Average( )
	 << " (" << _overall_max_frag[c]->NumSamples( ) << " samples)" << endl;

    cout << "Overall average accepted rate = " << _overall_accepted[c]->Average( )
	 << " (" << _overall_accepted[c]->NumSamples( ) << " samples)" << endl;
    cout << "Overall min accepted rate = " << _overall_accepted_min[c]->Average( )
	 << " (" << _overall_accepted_min[c]->NumSamples( ) << " samples)" << endl;
    
    cout << "Average hops = " << _hop_stats[c]->Average( )
	 << " (" << _hop_stats[c]->NumSamples( ) << " samples)" << endl;

    cout << "Slowest flit = " << _slowest_flit[c] << endl;
  
  }
  
  if(_sim_mode == batch)
    cout << "Overall batch duration = " << _overall_batch_time->Average( )
	 << " (" << _overall_batch_time->NumSamples( ) << " samples)" << endl;
  
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
	} else {
	  _flits_to_watch.insert(atoi(line.c_str()));
	}
      }
    }
    
  } else {
    //cout<<"Unable to open flit watch file, continuing with simulation\n";
  }
}
