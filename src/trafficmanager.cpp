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

#include "booksim.hpp"
#include <sstream>
#include <math.h>
#include <fstream>
#include "trafficmanager.hpp"
#include "random_utils.hpp" 
#include "vc.hpp"

TrafficManager::TrafficManager( const Configuration &config, const vector<Network *> & net )
  : Module( 0, "traffic_manager" ), _net(net), _cur_id(0), _cur_pid(0),
   _time(0), _warmup_time(-1), _drain_time(-1), _empty_network(false),
   _deadlock_counter(1), _sub_network(0), _timed_mode(false), _last_id(-1), 
   _last_pid(-1)
{

  _sources = _net[0]->NumSources( );
  _dests   = _net[0]->NumDests( );
  _routers = _net[0]->NumRouters( );

  Flit* ftemp = new Flit[  _sources ];
  for(int i = 0; i<  _sources ; i++){
    _flit_pool.push_back(&ftemp[i]);
  }

  //nodes higher than limit do not produce or receive packets
  //for default limit = sources

  _limit = config.GetInt( "limit" );
  if(_limit == 0){
    _limit = _sources;
  }
  assert(_limit<=_sources);
 
  _duplicate_networks = config.GetInt("physical_subnetworks");
 
  // ============ Message priorities ============ 

  string priority;
  config.GetStr( "priority", priority );

  _classes = 1;

  if ( priority == "class" ) {
    _classes  = 2;
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
    cerr << "Unkown priority value: " << priority << "!" << endl;
    Error( "" );
  }

  _replies_inherit_priority = config.GetInt("replies_inherit_priority");

  ostringstream tmp_name;
  
  // ============ Injection VC states  ============ 

  _buf_states.resize(_sources);

  for ( int s = 0; s < _sources; ++s ) {
    tmp_name << "terminal_buf_state_" << s;
    _buf_states[s].resize(_duplicate_networks);
    for (int a = 0; a < _duplicate_networks; ++a) {
      _buf_states[s][a] = new BufferState( config, this, tmp_name.str( ) );
    }
    tmp_name.str("");
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

  _voqing = config.GetInt( "voq" );
  if ( _voqing ) {
    _use_lagging = false;
  } else {
    _use_lagging = true;
  }

  _use_read_write = (config.GetInt("use_read_write")==1);
  _read_request_size = config.GetInt("read_request_size");
  _read_reply_size = config.GetInt("read_reply_size");
  _write_request_size = config.GetInt("write_request_size");
  _write_reply_size = config.GetInt("write_reply_size");
  if(_use_read_write) {
    _packet_size = (_read_request_size + _read_reply_size +
		    _write_request_size + _write_reply_size) / 2;
  }
  else
    _packet_size = config.GetInt( "const_flits_per_packet" );

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

  for ( int c = 0; c < _classes; ++c ) {
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
  }

  _hop_stats    = new Stats( this, "hop_stats", 1.0, 20 );
  _stats["hop_stats"] = _hop_stats;

  _overall_accepted     = new Stats( this, "overall_acceptance" );
  _stats["overall_acceptance"] = _overall_accepted;
  
  _overall_accepted_min = new Stats( this, "overall_min_acceptance" );
  _stats["overall_min_acceptance"] = _overall_accepted_min;
  
  _batch_time = new Stats( this, "batch_time" );
  _overall_batch_time = new Stats( this, "overall_batch_time" );

  _pair_latency.resize(_sources*_dests);
  _pair_tlat.resize(_sources*_dests);
  _sent_flits.resize(_sources);
  _accepted_flits.resize(_dests);
  
  _injected_flow.resize(_sources, 0);

  for ( int i = 0; i < _sources; ++i ) {
    tmp_name << "sent_stat_" << i;
    _sent_flits[i] = new Stats( this, tmp_name.str( ) );
    _stats[tmp_name.str()] = _sent_flits[i];
    tmp_name.str("");    

    for ( int j = 0; j < _dests; ++j ) {
      tmp_name << "pair_latency_stat_" << i << "_" << j;
      _pair_latency[i*_dests+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
      _stats[tmp_name.str()] = _pair_latency[i*_dests+j];
      tmp_name.str("");

      tmp_name << "pair_tlat_stat_" << i << "_" << j;
      _pair_tlat[i*_dests+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
      _stats[tmp_name.str()] = _pair_tlat[i*_dests+j];
      tmp_name.str("");
    }
  }

  _ejected_flow.resize(_dests, 0);

  for ( int i = 0; i < _dests; ++i ) {
    tmp_name << "accepted_stat_" << i;
    _accepted_flits[i] = new Stats( this, tmp_name.str( ) );
    _stats[tmp_name.str()] = _accepted_flits[i];
    tmp_name.str("");    
  }
  
  _received_flow.resize(_duplicate_networks*_routers, 0);
  _sent_flow.resize(_duplicate_networks*_routers, 0);

  _slowest_flit.resize(_classes);

  // ============ Simulation parameters ============ 

  if(config.GetInt( "injection_rate_uses_flits" )) {
    _flit_rate = config.GetFloat( "injection_rate" ); 
    _load = _flit_rate / _packet_size;
  } else {
    _load = config.GetFloat( "injection_rate" ); 
    _flit_rate = _load * _packet_size;
  }

  _total_sims = config.GetInt( "sim_count" );

  _internal_speedup = config.GetFloat( "internal_speedup" );
  _partial_internal_cycles.resize(_duplicate_networks, 0.0);
  _router_map.resize(_duplicate_networks);
  _class_array.resize(_duplicate_networks);
  for (int i=0; i < _duplicate_networks; ++i) {
    _router_map[i] = _net[i]->GetRouters();
    _class_array[i].resize(_classes, 0);
  }

  _traffic_function  = GetTrafficFunction( config );
  _routing_function  = GetRoutingFunction( config );
  _injection_process = GetInjectionProcess( config );

  string sim_type;
  config.GetStr( "sim_type", sim_type );

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
    cerr << "Unknown sim_type value : " << sim_type << "!" << endl;
    Error( "" );
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
  config.GetStr( "traffic", _traffic ) ;
  _drain_measured_only = config.GetInt( "drain_measured_only" );

  string watch_file;
  config.GetStr( "watch_file", watch_file );
  _LoadWatchList(watch_file);

  string stats_out_file;
  config.GetStr( "stats_out", stats_out_file );
  if(stats_out_file == "") {
    _stats_out = NULL;
  } else if(stats_out_file == "-") {
    _stats_out = &cout;
  } else {
    _stats_out = new ofstream(stats_out_file.c_str());
  }
  
  string flow_out_file;
  config.GetStr( "flow_out", flow_out_file );
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
  _flit_pool.clear();

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
  }

  delete _hop_stats;
  delete _overall_accepted;
  delete _overall_accepted_min;

  delete _batch_time;
  delete _overall_batch_time;

  for ( int i = 0; i < _sources; ++i ) {
    delete _sent_flits[i];

    for ( int j = 0; j < _dests; ++j ) {
      delete _pair_latency[i*_dests+j];
      delete _pair_tlat[i*_dests+j];
    }
  }

  for ( int i = 0; i < _dests; ++i ) {
    delete _accepted_flits[i];
  }

  if(gWatchOut && (gWatchOut != &cout)) delete gWatchOut;
  if(_stats_out && (_stats_out != &cout)) delete _stats_out;
  if(_flow_out && (_flow_out != &cout)) delete _flow_out;
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

Flit *TrafficManager::_NewFlit( )
{
  if(_flit_pool.empty()){
    Flit* ftemp = new Flit[  _sources ];
    for(int i = 0; i<  _sources ; i++){
      _flit_pool.push_back(&ftemp[i]);
    }
  }
  //the constructor should initialize everything
  Flit * f = _flit_pool.back();
  _flit_pool.pop_back();
  f->id    = _cur_id;
  _total_in_flight_flits[_cur_id] = f;
  f->watch = gWatchOut && (_flits_to_watch.count(_cur_id) > 0);
  ++_cur_id;
  return f;
}

void TrafficManager::_RetireFlit( Flit *f, int dest )
{
  _deadlock_counter = 1;

  assert(_total_in_flight_flits.count(f->id) > 0);
  _total_in_flight_flits.erase(f->id);
  
  if(f->record) {
    assert(_measured_in_flight_flits.count(f->id) > 0);
    _measured_in_flight_flits.erase(f->id);
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
    cerr << "Flit " << f->id << " arrived at incorrect output " << dest << endl
	 << *f;
    Error( "" );
  }

  if ( f->tail ) {
    assert(_total_in_flight_packets.count(f->pid) > 0);
    Flit * head = _total_in_flight_packets.lower_bound(f->pid)->second;
    assert(head->head);
    assert(f->pid == head->pid);
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
      Packet_Reply* temp = new Packet_Reply;
      temp->source = f->src;
      temp->time = f->atime;
      temp->ttime = f->ttime;
      temp->record = f->record;
      temp->type = f->type;
      _repliesDetails[f->id] = temp;
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
      
      _hop_stats->AddSample( f->hops );

      switch( _pri_type ) {
      case class_based:
	if((_slowest_flit[f->pri] < 0) ||
	   (_latency_stats[f->pri]->Max() < (f->atime - f->time)))
	  _slowest_flit[f->pri] = f->id;
	_latency_stats[f->pri]->AddSample( f->atime - f->time );
	_frag_stats[f->pri]->AddSample( (f->atime - head->atime) - (f->id - head->id) );
	if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY || f->type == Flit::ANY_TYPE)
	  _tlat_stats[f->pri]->AddSample( f->atime - f->ttime );
	break;
      default: // fall through
	if((_slowest_flit[0] < 0) ||
	   (_latency_stats[0]->Max() < (f->atime - f->time)))
	   _slowest_flit[0] = f->id;
	_latency_stats[0]->AddSample( f->atime - f->time);
	_frag_stats[0]->AddSample( (f->atime - head->atime) - (f->id - head->id) );
	if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY || f->type == Flit::ANY_TYPE)
	  _tlat_stats[0]->AddSample( f->atime - f->ttime );
      }
   
      _pair_latency[f->src*_dests+dest]->AddSample( f->atime - f->time );
      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY)
	_pair_tlat[dest*_dests+f->src]->AddSample( f->atime - f->ttime );
      else if(f->type == Flit::ANY_TYPE)
	_pair_tlat[f->src*_dests+dest]->AddSample( f->atime - f->ttime );
      
      if ( f->record ) {
	assert(_measured_in_flight_packets.count(f->pid) > 0);
	_measured_in_flight_packets.erase(f->pid);
      }
    }

    // free all flits associated with the current packet
    int fpid = f->pid;
    pair<multimap<int, Flit *>::iterator, multimap<int, Flit *>::iterator> res = _total_in_flight_packets.equal_range(fpid);
    for(multimap<int, Flit *>::iterator iter = res.first; iter != res.second; ++iter) {
      assert(iter->second->pid == fpid);
      //reset the flit and back to the poool
      iter->second->Reset();
      _flit_pool.push_back(iter->second);
    }
    _total_in_flight_packets.erase(fpid);

  }
}

int TrafficManager::_IssuePacket( int source, int cl )
{
  int result;
  if(_sim_mode == batch){ //batch mode
    if(_use_read_write){ //read write packets
      //check queue for waiting replies.
      //check to make sure it is on time yet
      int pending_time = INT_MAX; //reset to maxtime+1
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
	result = -1;
	
	if (RandomFloat() < 0.5) {
	  result = -2;
	}
	
	_packets_sent[source]++;
	_requestsOutstanding[source]++;
      } 
    } else { //normal
      if ((_packets_sent[source] >= _batch_size && !_timed_mode) || 
		 (_requestsOutstanding[source] >= _maxOutstanding)) {
	result = 0;
      } else {
	result = gConstPacketSize;
	_packets_sent[source]++;
	//here is means, how many flits can be waiting in the queue
	_requestsOutstanding[source]++;
      } 
    } 
  } else { //injection rate mode
    if(_use_read_write){ //use read and write
      //check queue for waiting replies.
      //check to make sure it is on time yet
      int pending_time = INT_MAX; //reset to maxtime+1
      if (!_repliesPending[source].empty()) {
	result = _repliesPending[source].front();
	pending_time = _repliesDetails.find(result)->second->time;
      }
      if (pending_time<=_qtime[source][cl]) {
	result = _repliesPending[source].front();
	_repliesPending[source].pop_front();
      } else {
	result = _injection_process( source, _load );
	//produce a packet
	if(result){
	  //coin toss to determine request type.
	  result = -1;
	
	  if (RandomFloat() < 0.5) {
	    result = -2;
	  }
	}
      } 
    } else { //normal mode
      return _injection_process( source, _load );
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
  int size = gConstPacketSize; //input size 
  int ttime = time;
  int packet_destination;
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
	cerr << "Invalid packet type: " << packet_type << "!" << endl;
	Error( "" );
      }
      packet_destination = _traffic_function( source, _limit );
    } else  {
      map<int, Packet_Reply*>::iterator iter = _repliesDetails.find(stype);
      Packet_Reply* temp = iter->second;
      
      if (temp->type == Flit::READ_REQUEST) {//read reply
	size = _read_reply_size;
	packet_type = Flit::READ_REPLY;
      } else if(temp->type == Flit::WRITE_REQUEST) {  //write reply
	size = _write_reply_size;
	packet_type = Flit::WRITE_REPLY;
      } else {
	cerr << "Invalid packet type: " << temp->type << "!" << endl;
	Error( "" );
      }
      packet_destination = temp->source;
      time = temp->time;
      ttime = temp->ttime;
      record = temp->record;
      _repliesDetails.erase(iter);
      delete temp;
    }
  } else {
    //use uniform packet size
    packet_destination = _traffic_function( source, _limit );
  }



  if ((packet_destination <0) || (packet_destination >= _dests)) {
    cerr << "Incorrect packet destination " << packet_destination
	 << " for stype " << packet_type
	 << "!" << endl;
    Error( "" );
  }

  if ( ( _sim_state == running ) ||
       ( ( _sim_state == draining ) && ( time < _drain_time ) ) ) {
    record = true;
  }

  _sub_network = DivisionAlgorithm(packet_type);
  
  bool watch  = gWatchOut && (_packets_to_watch.count(_cur_pid) > 0);
  
  if ( watch ) { 
    *gWatchOut << GetSimTime() << " | "
		<< "node" << source << " | "
		<< "Enqueuing packet " << _cur_pid
		<< " at time " << time
		<< "." << endl;
  }
  
  for ( int i = 0; i < size; ++i ) {
    Flit * f = _NewFlit( );
    f->pid = _cur_pid;
    f->watch |= watch;
    f->subnetwork = _sub_network;
    f->src    = source;
    f->time   = time;
    f->ttime  = ttime;
    f->record = record;
    
    if(record) {
      _measured_in_flight_flits[f->id] = f;
    }
    
    if(gTrace){
      cout<<"New Flit "<<f->src<<endl;
    }
    f->type = packet_type;

    if ( i == 0 ) { // Head flit
      f->head = true;
      //packets are only generated to nodes smaller or equal to limit
      f->dest = packet_destination;
      _total_in_flight_packets.insert(pair<int, Flit *>(f->pid, f));
      if(record) {
	_measured_in_flight_packets.insert(pair<int, Flit *>(f->pid, f));
      }
    } else {
      f->head = false;
      f->dest = -1;
    }
    switch( _pri_type ) {
    case class_based:
      f->pri = cl;
      break;
    case age_based:
      f->pri = _replies_inherit_priority ? -ttime : -time;
      break;
    case sequence_based:
      f->pri = -_packets_sent[source];
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

    _partial_packets[source][cl][_sub_network].push_back( f );
  }
  ++_cur_pid;
}





void TrafficManager::_FirstStep( )
{  
  
  // Ensure that all outputs are defined before starting simulation
  for (int i = 0; i < _duplicate_networks; ++i) { 
    _net[i]->WriteOutputs( );
  
    for ( int output = 0; output < _net[i]->NumDests( ); ++output ) {
      _net[i]->WriteCredit( 0, output );
    }
  }
  
}

void TrafficManager::_BatchInject(){
  
  // Receive credits and inject new traffic
  for ( int input = 0; input < _sources; ++input ) {
    for (int i = 0; i < _duplicate_networks; ++i) {
      Credit * cred = _net[i]->ReadCredit( input );
      if ( cred ) {
        _buf_states[input][i]->ProcessCredit( cred );
        delete cred;
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
	    int psize = _IssuePacket( input, c );

	    if ( psize ) {
	      _GeneratePacket( input, psize, c, 
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
	if ( generated ) {
	  //highest_class = c;
	  _class_array[_sub_network][c]++; // One more packet for this class.
	}
      }
    }

    // Now, check partially issued packets to
    // see if they can be issued
    for (int i = 0; i < _duplicate_networks; ++i) {
      int highest_class = 0;
      // Now just find which is the highest_class.
      for (short c = _classes - 1; c >= 0; --c) {
        if (_class_array[i][c]) {
          highest_class = c;
          break;
        }
      } 
      bool write_flit = false;
      Flit * f;
      if ( !_partial_packets[input][highest_class][i].empty( ) ) {
        f = _partial_packets[input][highest_class][i].front( );
        if ( f->head && f->vc == -1) { // Find first available VC

	  f->vc = _buf_states[input][i]->FindAvailable( f->type );
	  if ( f->vc != -1 ) {
	    _buf_states[input][i]->TakeBuffer( f->vc );
	  }
        }

        if ( ( f->vc != -1 ) &&
	     ( !_buf_states[input][i]->IsFullFor( f->vc ) ) ) {

	  _partial_packets[input][highest_class][i].pop_front( );
	  _buf_states[input][i]->SendingFlit( f );
	  write_flit = true;

	  if(_pri_type == network_age_based) {
	    f->pri = -_time;
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
	  if ( !_partial_packets[input][highest_class][i].empty( ) && !f->tail ) {
	    Flit * nf = _partial_packets[input][highest_class][i].front( );
	    nf->vc = f->vc;
	  }

	  ++_injected_flow[input];
        }
      }
      _net[i]->WriteFlit( write_flit ? f : 0, input );
      if( _sim_state == running )
	_sent_flits[input]->AddSample(write_flit);
      if (write_flit && f->tail) // If a tail flit, reduce the number of packets of this class.
	_class_array[i][highest_class]--;
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
        delete cred;
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
	    int psize = _IssuePacket( input, c );

	    if ( psize ) { //generate a packet
	      _GeneratePacket( input, psize, c, 
			       _include_queuing==1 ? 
			       _qtime[input][c] : _time );
	      generated = true;
	    }
	    //this is not a request packet
	    //don't advance time
	    if(_use_read_write && psize>0){
	      
	    } else {
	      ++_qtime[input][c];
	    }
	  }
	  
	  if ( ( _sim_state == draining ) && 
	       ( _qtime[input][c] > _drain_time ) ) {
	    _qdrained[input][c] = true;
	  }
	}
	if ( generated ) {
	  //highest_class = c;
	  _class_array[_sub_network][c]++; // One more packet for this class.
	}
      } //else {
	//highest_class = c;
      //} This is not necessary with _class_array because it stays.
    }

    // Now, check partially issued packets to
    // see if they can be issued
    for (int i = 0; i < _duplicate_networks; ++i) {
      int highest_class = 0;
      // Now just find which is the highest_class.
      for (short a = _classes - 1; a >= 0; --a) {
	if (_class_array[i][a]) {
	  highest_class = a;
	  break;
	}
      }
      bool write_flit = false;
      Flit * f;
      if ( !_partial_packets[input][highest_class][i].empty( ) ) {
        f = _partial_packets[input][highest_class][i].front( );
        if ( f->head && f->vc == -1) { // Find first available VC

	  if ( _voqing ) {
	    if ( _buf_states[input][i]->IsAvailableFor( f->dest ) ) {
	      f->vc = f->dest;
  	    }
	  } else {
	    f->vc = _buf_states[input][i]->FindAvailable( f->type );
	  }
	  
	  if ( f->vc != -1 ) {
	    _buf_states[input][i]->TakeBuffer( f->vc );
	  }
        }

        if ( ( f->vc != -1 ) &&
	     ( !_buf_states[input][i]->IsFullFor( f->vc ) ) ) {

	  _partial_packets[input][highest_class][i].pop_front( );
	  _buf_states[input][i]->SendingFlit( f );
	  write_flit = true;

	  if(_pri_type == network_age_based) {
	    f->pri = -_time;
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
	  if ( !_partial_packets[input][highest_class][i].empty( ) && !f->tail ) {
	    Flit * nf = _partial_packets[input][highest_class][i].front( );
	    nf->vc = f->vc;
	  }

	  ++_injected_flow[input];
        }
      }
      _net[i]->WriteFlit( write_flit ? f : 0, input );
      if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	_sent_flits[input]->AddSample(write_flit);
      if (write_flit && f->tail) // If a tail flit, reduce the number of packets of this class.
	_class_array[i][highest_class]--;
    }
  }
}

void TrafficManager::_Step( )
{
  if(_deadlock_counter++ == 0){
    cout << "WARNING: Possible network deadlock.\n";
  }

  if(_sim_mode == batch){
    _BatchInject();
  } else {
    _NormalInject();
  }

  //advance networks
  for (int i = 0; i < _duplicate_networks; ++i) {
    _net[i]->ReadInputs( );
    _partial_internal_cycles[i] += _internal_speedup;
    while( _partial_internal_cycles[i] >= 1.0 ) {
      _net[i]->InternalStep( );
      _partial_internal_cycles[i] -= 1.0;
    }
  }

  for (int a = 0; a < _duplicate_networks; ++a) {
    _net[a]->WriteOutputs( );
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
      
        Credit * cred = new Credit( 1 );
        cred->vc[0] = f->vc;
        cred->vc_cnt = 1;
	cred->dest_router = f->from_router;
        _net[i]->WriteCredit( cred, output );
        _RetireFlit( f, output );
      
        if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	  _accepted_flits[output]->AddSample( 1 );
      } else {
        _net[i]->WriteCredit( 0, output );
        if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	  _accepted_flits[output]->AddSample( 0 );
      }
    }

    for(int j = 0; j < _routers; ++j) {
      _received_flow[i*_routers+j] += _router_map[i][j]->GetReceivedFlits();
      _sent_flow[i*_routers+j] += _router_map[i][j]->GetSentFlits();
      _router_map[i][j]->ResetFlitStats();
    }
  }
  ++_time;
  if(gTrace){
    cout<<"TIME "<<_time<<endl;
  }

}
  
bool TrafficManager::_PacketsOutstanding( ) const
{
  bool outstanding;

  if ( _measured_in_flight_packets.empty() ) {
    outstanding = false;

    for ( int c = 0; c < _classes; ++c ) {
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
      if ( outstanding ) { break; }
    }
  } else {
#ifdef DEBUG_DRAIN
    cout << "in flight = " << _measured_in_flight_packets.size() << endl;
#endif
    outstanding = true;
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
  }
  
  for ( int i = 0; i < _sources; ++i ) {
    _sent_flits[i]->Clear( );

    for ( int j = 0; j < _dests; ++j ) {
      _pair_latency[i*_dests+j]->Clear( );
      _pair_tlat[i*_dests+j]->Clear( );
    }
  }

  for ( int i = 0; i < _dests; ++i ) {
    _accepted_flits[i]->Clear( );
  }
  
  _hop_stats->Clear();

}

int TrafficManager::_ComputeStats( const vector<Stats *> & stats, double *avg, double *min ) const 
{
  int dmin;

  *min = 1.0;
  *avg = 0.0;

  for ( int d = 0; d < _dests; ++d ) {
    if ( stats[d]->Average( ) < *min ) {
      *min = stats[d]->Average( );
      dmin = d;
    }
    *avg += stats[d]->Average( );
  }

  *avg /= (double)_dests;

  return dmin;
}

void TrafficManager::_DisplayRemaining( ) const 
{
  map<int, Flit *>::const_iterator iter;
  int i;

  cout << "Remaining flits: ";
  for ( iter = _total_in_flight_flits.begin( ), i = 0;
	( iter != _total_in_flight_flits.end( ) ) && ( i < 10 );
	iter++, i++ ) {
    cout << iter->first << " ";
  }
  if(_total_in_flight_flits.size() > 10)
    cout << "[...] ";
  
  cout << "(" << _total_in_flight_flits.size() << " flits"
       << ", " << _total_in_flight_packets.size() << " packets"
       << ")" << endl;
  
  cout << "Measured flits: ";
  for ( iter = _measured_in_flight_flits.begin( ), i = 0;
	( iter != _measured_in_flight_flits.end( ) ) && ( i < 10 );
	iter++, i++ ) {
    cout << iter->first << " ";
  }
  if(_measured_in_flight_flits.size() > 10)
    cout << "[...] ";
  
  cout << "(" << _measured_in_flight_flits.size() << " flits"
       << ", " << _measured_in_flight_packets.size() << " packets"
       << ")" << endl;
  
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
	double cur_latency = _latency_stats[0]->Average( );
	double min, avg;
	int dmin = _ComputeStats( _accepted_flits, &avg, &min );
	
	cout << "Minimum latency = " << _latency_stats[0]->Min( ) << endl;
	cout << "Average latency = " << cur_latency << endl;
	cout << "Maximum latency = " << _latency_stats[0]->Max( ) << endl;
	cout << "Average fragmentation = " << _frag_stats[0]->Average( ) << endl;
	cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
	if(_stats_out)
	  *_stats_out << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl
		      << "lat_hist(" << total_phases + 1 << ",:) = "
		      << *_latency_stats[0] << ";" << endl
		      << "frag_hist(" << total_phases + 1 << ",:) = "
		      << *_frag_stats[0] << ";" << endl;
      } 
    }
    cout << "Total inflight " << _total_in_flight_packets.size() << endl;
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
      while( (_drain_measured_only ? _measured_in_flight_packets.size() : _total_in_flight_packets.size()) > 0 ) { 
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
      }
      cout << endl;
      cout << "Batch " << total_phases + 1 << " ("<<_batch_size  <<  " flits) received. Time used is " << _time - _drain_time << " cycles. Last packet was " << _last_pid << ", last flit was " << _last_id << "." <<endl;
      _batch_time->AddSample(_time - start_time);
      cout << _sim_state << endl;
      if(_stats_out)
	*_stats_out << "%=================================" << endl;
      double cur_latency = _latency_stats[0]->Average( );
      double min, avg;
      int dmin = _ComputeStats( _accepted_flits, &avg, &min );
      
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
	    *_stats_out << _pair_latency[i*_dests+j]->NumSamples( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "pair_lat(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _sources; ++i) {
	  for(int j = 0; j < _dests; ++j) {
	    *_stats_out << _pair_latency[i*_dests+j]->Average( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "pair_tlat(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _sources; ++i) {
	  for(int j = 0; j < _dests; ++j) {
	    *_stats_out << _pair_tlat[i*_dests+j]->Average( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "sent(" << total_phases + 1 << ",:) = [ ";
	for ( int d = 0; d < _dests; ++d ) {
	  *_stats_out << _sent_flits[d]->Average( ) << " ";
	}
	*_stats_out << "];" << endl
		    << "accepted(" << total_phases + 1 << ",:) = [ ";
	for ( int d = 0; d < _dests; ++d ) {
	  *_stats_out << _accepted_flits[d]->Average( ) << " ";
	}
	*_stats_out << "];" << endl;
      }
      ++total_phases;
    }
    converged = 1;
  } else { 
    //once warmed up, we require 3 converging runs
    //to end the simulation 
    double prev_latency = 0.0;
    double prev_accepted = 0.0;
    while( ( total_phases < _max_samples ) && 
	   ( ( _sim_state != running ) || 
	     ( converged < 3 ) ) ) {

      if ( clear_last || (( ( _sim_state == warming_up ) && ( total_phases & 0x1 == 0 ) )) ) {
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
      double cur_latency = _latency_stats[0]->Average( );
      int dmin;
      double min, avg;
      dmin = _ComputeStats( _accepted_flits, &avg, &min );
      double cur_accepted = avg;
      cout << "Minimum latency = " << _latency_stats[0]->Min( ) << endl;
      cout << "Average latency = " << cur_latency << endl;
      cout << "Maximum latency = " << _latency_stats[0]->Max( ) << endl;
      cout << "Average fragmentation = " << _frag_stats[0]->Average( ) << endl;
      cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
      if(_stats_out) {
	*_stats_out << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl
		    << "lat_hist(" << total_phases + 1 << ",:) = "
		    << *_latency_stats[0] << ";" << endl
		    << "frag_hist(" << total_phases + 1 << ",:) = "
		    << *_frag_stats[0] << ";" << endl
		    << "pair_sent(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _sources; ++i) {
	  for(int j = 0; j < _dests; ++j) {
	    *_stats_out << _pair_latency[i*_dests+j]->NumSamples( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "pair_lat(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _sources; ++i) {
	  for(int j = 0; j < _dests; ++j) {
	    *_stats_out << _pair_latency[i*_dests+j]->Average( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "pair_lat(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _sources; ++i) {
	  for(int j = 0; j < _dests; ++j) {
	    *_stats_out << _pair_tlat[i*_dests+j]->Average( ) << " ";
	  }
	}
	*_stats_out << "];" << endl
		    << "sent(" << total_phases + 1 << ",:) = [ ";
	for ( int d = 0; d < _dests; ++d ) {
	  *_stats_out << _sent_flits[d]->Average( ) << " ";
	}
	*_stats_out << "];" << endl
		    << "accepted(" << total_phases + 1 << ",:) = [ ";
	for ( int d = 0; d < _dests; ++d ) {
	  *_stats_out << _accepted_flits[d]->Average( ) << " ";
	}
	*_stats_out << "];" << endl;
      }

      // Fail safe for latency mode, throughput will ust continue
      if ( ( _sim_mode == latency ) && ( cur_latency >_latency_thres ) ) {
	cout << "Average latency exceeded " << _latency_thres << " cycles. Aborting simulation." << endl;
	converged = 0; 
	_sim_state = warming_up;
	break;
      }

      cout << "latency change    = " << fabs( ( cur_latency - prev_latency ) / cur_latency ) << endl;
      cout << "throughput change = " << fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) << endl;

      if ( _sim_state == warming_up ) {
	if ( _warmup_periods == 0 ) {
	  if ( _sim_mode == latency ) {
	    if ( ( fabs( ( cur_latency - prev_latency ) / cur_latency ) < _warmup_threshold ) &&
		 ( fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) < _warmup_threshold ) ) {
	      cout << "Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
	      clear_last = true;
	      _sim_state = running;
	    }
	  } else {
	    if ( fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) < _warmup_threshold ) {
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
	  if ( ( fabs( ( cur_latency - prev_latency ) / cur_latency ) < _stopping_threshold ) &&
	       ( fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) < _acc_stopping_threshold ) ) {
	    ++converged;
	  } else {
	    converged = 0;
	  }
	} else {
	  if ( fabs( ( cur_accepted - prev_accepted ) / cur_accepted ) < _acc_stopping_threshold ) {
	    ++converged;
	  } else {
	    converged = 0;
	  }
	} 
      }
      prev_latency  = cur_latency;
      prev_accepted = cur_accepted;
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
	      acc_latency += _latency_stats[c]->Average() * _latency_stats[c]->NumSamples();
	      acc_count += _latency_stats[c]->NumSamples();
	    }
	    
	    int res_latency = 0;
	    int res_count = 0;
	    map<int, Flit *>::const_iterator iter;
	    for(iter = _measured_in_flight_flits.begin(); 
		iter != _measured_in_flight_flits.end(); 
		iter++) {
	      res_latency += _time - iter->second->time;
	      res_count++;
	    }

	    if((acc_latency + res_latency) / (acc_count + res_count) > _latency_thres) {
	      cout << "Average latency exceeded " << _latency_thres << " cycles. Aborting simulation." << endl;
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
    while( (_drain_measured_only ? _measured_in_flight_packets.size() : _total_in_flight_packets.size()) > 0 ) { 
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
    }
    _empty_network = false;
  }

  return ( converged > 0 );
}

bool TrafficManager::Run( )
{
  //      _FirstStep( );
  
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
    }
    
    double min, avg;
    _ComputeStats( _accepted_flits, &avg, &min );
    _overall_accepted->AddSample( avg );
    _overall_accepted_min->AddSample( min );

    if(_sim_mode == batch)
      _overall_batch_time->AddSample(_batch_time->Sum( ));
  }
  
  DisplayStats();
  if(_print_vc_stats) {
    if(_print_csv_results) {
      cout << "vc_stats:"
	   << _traffic
	   << "," << _packet_size
	   << "," << _load
	   << "," << _flit_rate << ",";
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
	   << "," << _flit_rate
	   << "," << _overall_min_latency[c]->Average( )
	   << "," << _overall_avg_latency[c]->Average( )
	   << "," << _overall_max_latency[c]->Average( )
	   << "," << _overall_min_tlat[c]->Average( )
	   << "," << _overall_avg_tlat[c]->Average( )
	   << "," << _overall_max_tlat[c]->Average( )
	   << "," << _overall_min_frag[c]->Average( )
	   << "," << _overall_avg_frag[c]->Average( )
	   << "," << _overall_max_frag[c]->Average( )
	   << "," << _overall_accepted->Average( )
	   << "," << _overall_accepted_min->Average( )
	   << "," << _hop_stats->Average( )
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
    cout << "Overall average accepted rate = " << _overall_accepted->Average( )
	 << " (" << _overall_accepted->NumSamples( ) << " samples)" << endl;
    
    cout << "Overall min accepted rate = " << _overall_accepted_min->Average( )
	 << " (" << _overall_accepted_min->NumSamples( ) << " samples)" << endl;
    
    if(_sim_mode == batch)
      cout << "Overall batch duration = " << _overall_batch_time->Average( )
	   << " (" << _overall_batch_time->NumSamples( ) << " samples)" << endl;

    cout << "Slowest flit = " << _slowest_flit[c] << endl;
  }

  cout << "Average hops = " << _hop_stats->Average( )
       << " (" << _hop_stats->NumSamples( ) << " samples)" << endl;

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
