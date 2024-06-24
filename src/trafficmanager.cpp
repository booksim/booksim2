// $Id$

/*
  Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
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
#include <ctime>

#include "booksim.hpp"
#include "booksim_config.hpp"
#include "trafficmanager.hpp"
#include "batchtrafficmanager.hpp"
#include "random_utils.hpp"
#include "vc.hpp"
#include "packet_reply_info.hpp"
#include "endpoint.hpp"

TrafficManager * TrafficManager::New(Configuration const & config,
                                     vector<Network *> const & net)
{
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
    : Module( 0, "traffic_manager" ), _net(net), _empty_network(false), _deadlock_timer(0), _reset_time(0), _drain_time(-1), _generation_stopped_time(0), _cur_id(0), _cur_pid(0), _time(0)
{

    if ( config.GetInt("trace_debug") != 0){
        int ret = remove("trace_debug.txt");
        if (ret){
            // Nothing really bad
        }
        debug_trace_file.open("trace_debug.txt", std::ios_base::app);
    }

    _nodes = _net[0]->NumNodes( );
    _routers = _net[0]->NumRouters( );

    _vcs = config.GetInt("num_vcs");
    _subnets = config.GetInt("subnets");

    _subnet.resize(Flit::NUM_FLIT_TYPES);
    _subnet[Flit::READ_REQUEST] = config.GetInt("read_request_subnet");
    _subnet[Flit::READ_REPLY] = config.GetInt("read_reply_subnet");
    _subnet[Flit::WRITE_REQUEST] = config.GetInt("write_request_subnet");
    _subnet[Flit::WRITE_REPLY] = config.GetInt("write_reply_subnet");

    _max_no_accepted_flit_period = config.GetInt("max_no_accepted_flit_period");

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

    // ============ Routing ============

    string rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
    map<string, tRoutingFunction>::const_iterator rf_iter = gRoutingFunctionMap.find(rf);
    if(rf_iter == gRoutingFunctionMap.end()) {
        Error("Invalid routing function: " + rf);
    }
    _rf = rf_iter->second;

    _lookahead_routing = !config.GetInt("routing_delay");
    _noq = config.GetInt("noq");
    if(_noq) {
        if(!_lookahead_routing) {
            Error("NOQ requires lookahead routing to be enabled.");
        }
    }

    // ============ Traffic ============

    _classes = config.GetInt("classes");

    _use_read_write = config.GetIntArray("use_read_write");
    if(_use_read_write.empty()) {
        _use_read_write.push_back(config.GetInt("use_read_write"));
    }
    _use_read_write.resize(_classes, _use_read_write.back());

    _write_fraction = config.GetFloatArray("write_fraction");
    if(_write_fraction.empty()) {
        _write_fraction.push_back(config.GetFloat("write_fraction"));
    }
    _write_fraction.resize(_classes, _write_fraction.back());

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

    string packet_size_str = config.GetStr("packet_size");
    if(packet_size_str.empty()) {
        _packet_size.push_back(vector<int>(1, config.GetInt("packet_size")));
    } else {
        vector<string> packet_size_strings = tokenize_str(packet_size_str);
        for(size_t i = 0; i < packet_size_strings.size(); ++i) {
            _packet_size.push_back(tokenize_int(packet_size_strings[i]));
        }
    }
    _packet_size.resize(_classes, _packet_size.back());

    string packet_size_rate_str = config.GetStr("packet_size_rate");
    if(packet_size_rate_str.empty()) {
        int rate = config.GetInt("packet_size_rate");
        assert(rate >= 0);
        for(int c = 0; c < _classes; ++c) {
            int size = _packet_size[c].size();
            _packet_size_rate.push_back(vector<int>(size, rate));
            _packet_size_max_val.push_back(size * rate - 1);
        }
    } else {
        vector<string> packet_size_rate_strings = tokenize_str(packet_size_rate_str);
        packet_size_rate_strings.resize(_classes, packet_size_rate_strings.back());
        for(int c = 0; c < _classes; ++c) {
            vector<int> rates = tokenize_int(packet_size_rate_strings[c]);
            rates.resize(_packet_size[c].size(), rates.back());
            _packet_size_rate.push_back(rates);
            int size = rates.size();
            int max_val = -1;
            for(int i = 0; i < size; ++i) {
                int rate = rates[i];
                assert(rate >= 0);
                max_val += rate;
            }
            _packet_size_max_val.push_back(max_val);
        }
    }

    for(int c = 0; c < _classes; ++c) {
        if(_use_read_write[c]) {
            _packet_size[c] =
                vector<int>(1, (_read_request_size[c] + _read_reply_size[c] +
                                _write_request_size[c] + _write_reply_size[c]) / 2);
            _packet_size_rate[c] = vector<int>(1, 1);
            _packet_size_max_val[c] = 0;
        }
    }


    // Change _load from a 1D vector by class, to a 2D vector by class, then
    // by node.  This allows us to specify different injection rates per node
    // using the override below.
    vector<double> tload;
    tload = config.GetFloatArray("injection_rate");
    if (tload.empty()) {
        tload.push_back(config.GetFloat("injection_rate"));
    }
    tload.resize(_classes, tload.back());
    _load.resize(_classes);
    _intended_load.resize(_classes);
    for (int c = 0; c < _classes; ++c){
      _load[c] = vector<double>(_nodes, tload[c]);
      _intended_load[c] = vector<double>(_nodes, tload[c]);
    }

    if (config.GetInt("injection_rate_uses_flits")) {
      for (int c = 0; c < _classes; ++c) {
        for (int n = 0; n < _nodes; ++n) {
          _load[c][n] /= _GetAveragePacketSize(c);
        }
      }
    }

    // Per node injection overrides
    vector<string> inj_overrides = tokenize_str(config.GetStr( "injection_override" ));
    size_t ovr_class_size = inj_overrides.size();
    if (ovr_class_size){
      vector<int> ovr_node_vec;
      vector<double> ovr_rates_vec;
      size_t nodes_overridden;

      for (size_t i = 0; i < ovr_class_size; ++i) {
        vector<string> class_overrides = tokenize_str(inj_overrides[i]);
        size_t p_size = class_overrides.size();
        assert(p_size >= 1);
        // The first group contains the node IDs
        ovr_node_vec = tokenize_int(class_overrides[0]);
        nodes_overridden = ovr_node_vec.size();
        if (p_size > 1) {
          // The second group contains the new injection ratek
          ovr_rates_vec = tokenize_float(class_overrides[1]);
        }
        ovr_rates_vec.resize(nodes_overridden,0);

        for (size_t j = 0; j < nodes_overridden; ++j) {
          int node = ovr_node_vec[j];
          assert((node >= 0) && (node < _nodes));
          _load[i][node] = ovr_rates_vec[j];
          _intended_load[i][node] = ovr_rates_vec[j];
        }
      }
      for (size_t i = ovr_class_size; i < (size_t)_classes; ++i) {
        for (size_t j = 0; j < nodes_overridden; ++j) {
          int node = ovr_node_vec[j];
          assert((node >= 0) && (node < _nodes));
          _load[i][node] = ovr_rates_vec[j];
          _intended_load[i][node] = ovr_rates_vec[j];
        }
      }
    }



    _traffic = config.GetStrArray("traffic");
    _traffic.resize(_classes, _traffic.back());

    _traffic_pattern.resize(_classes);

    _class_priority = config.GetIntArray("class_priority");
    if(_class_priority.empty()) {
        _class_priority.push_back(config.GetInt("class_priority"));
    }
    _class_priority.resize(_classes, _class_priority.back());

    vector<string> injection_process = config.GetStrArray("injection_process");
    injection_process.resize(_classes, injection_process.back());

    _injection_process.resize(_classes);

    for(int c = 0; c < _classes; ++c) {
        _traffic_pattern[c] = TrafficPattern::New(_traffic[c], _nodes, &config);
        _injection_process[c] = InjectionProcess::New(injection_process[c], _nodes, _load[c], &config);
    }

    // ============ Injection VC states  ============

    _last_vc.resize(_nodes);
    _last_class.resize(_nodes);

    for ( int source = 0; source < _nodes; ++source ) {
        _last_class[source].resize(_subnets, 0);
        _last_vc[source].resize(_subnets);
        for ( int subnet = 0; subnet < _subnets; ++subnet ) {
            _last_vc[source][subnet].resize(_classes, -1);
        }
    }

#ifdef TRACK_FLOWS
    _outstanding_credits.resize(_classes);
    for(int c = 0; c < _classes; ++c) {
        _outstanding_credits[c].resize(_subnets, vector<int>(_nodes, 0));
    }
    _outstanding_classes.resize(_nodes);
    for(int n = 0; n < _nodes; ++n) {
        _outstanding_classes[n].resize(_subnets, vector<queue<int> >(_vcs));
    }
#endif



    // ============ Endpoints ===================
    _endpoints.resize(_nodes);
    for(int nodeid = 0; nodeid < _nodes; ++nodeid) {
      ostringstream tmp_name;
      tmp_name << "endpoint_" << nodeid;
      _endpoints[nodeid] = new EndPoint(config, this, tmp_name.str(), nodeid);
    }
    _flit_retransmissions = 0;
    _packet_retransmissions = 0;
    _standalone_acks_transmitted = 0;
    _flits_dropped_for_rget_conversion = 0;

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

    _packet_seq_no.resize(_nodes);
    _repliesPending.resize(_nodes);
    _requestsOutstanding.resize(_nodes);

    _hold_switch_for_packet = config.GetInt("hold_switch_for_packet");

    // ============ Simulation parameters ============

    _total_sims = config.GetInt( "sim_count" );

    _router.resize(_subnets);
    for (int i=0; i < _subnets; ++i) {
        _router[i] = _net[i]->GetRouters();
    }

    //seed the network
    int seed;
    if(config.GetStr("seed") == "time") {
      unsigned long long tmp_time = static_cast<unsigned long long>(time(NULL));
      seed = static_cast<int>(tmp_time);
      cout << "SEED: seed=" << seed << endl;
    } else {
      seed = config.GetInt("seed");
    }
    RandomSeed(seed);

    _measure_latency = (config.GetStr("sim_type") == "latency");

    _sample_period = config.GetInt( "sample_period" );
    _max_samples    = config.GetInt( "max_samples" );
    _min_samples    = config.GetInt( "min_samples" );
    _warmup_periods = config.GetInt( "warmup_periods" );

    _measure_stats = config.GetIntArray( "measure_stats" );
    if(_measure_stats.empty()) {
        _measure_stats.push_back(config.GetInt("measure_stats"));
    }
    _measure_stats.resize(_classes, _measure_stats.back());
    _pair_stats = (config.GetInt("pair_stats")==1);

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

    string watch_file = config.GetStr( "watch_file" );
    if((watch_file != "") && (watch_file != "-")) {
        _LoadWatchList(watch_file);
    }

    vector<int> watch_flits = config.GetIntArray("watch_flits");
    for(size_t i = 0; i < watch_flits.size(); ++i) {
        _flits_to_watch.insert(watch_flits[i]);
    }

    vector<int> watch_packets = config.GetIntArray("watch_packets");
    for(size_t i = 0; i < watch_packets.size(); ++i) {
        _packets_to_watch.insert(watch_packets[i]);
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

#ifdef TRACK_FLOWS
    _injected_flits.resize(_classes, vector<int>(_nodes, 0));
    _ejected_flits.resize(_classes, vector<int>(_nodes, 0));
    string injected_flits_out_file = config.GetStr( "injected_flits_out" );
    if(injected_flits_out_file == "") {
        _injected_flits_out = NULL;
    } else {
        _injected_flits_out = new ofstream(injected_flits_out_file.c_str());
    }
    string received_flits_out_file = config.GetStr( "received_flits_out" );
    if(received_flits_out_file == "") {
        _received_flits_out = NULL;
    } else {
        _received_flits_out = new ofstream(received_flits_out_file.c_str());
    }
    string stored_flits_out_file = config.GetStr( "stored_flits_out" );
    if(stored_flits_out_file == "") {
        _stored_flits_out = NULL;
    } else {
        _stored_flits_out = new ofstream(stored_flits_out_file.c_str());
    }
    string sent_flits_out_file = config.GetStr( "sent_flits_out" );
    if(sent_flits_out_file == "") {
        _sent_flits_out = NULL;
    } else {
        _sent_flits_out = new ofstream(sent_flits_out_file.c_str());
    }
    string outstanding_credits_out_file = config.GetStr( "outstanding_credits_out" );
    if(outstanding_credits_out_file == "") {
        _outstanding_credits_out = NULL;
    } else {
        _outstanding_credits_out = new ofstream(outstanding_credits_out_file.c_str());
    }
    string ejected_flits_out_file = config.GetStr( "ejected_flits_out" );
    if(ejected_flits_out_file == "") {
        _ejected_flits_out = NULL;
    } else {
        _ejected_flits_out = new ofstream(ejected_flits_out_file.c_str());
    }
    string active_packets_out_file = config.GetStr( "active_packets_out" );
    if(active_packets_out_file == "") {
        _active_packets_out = NULL;
    } else {
        _active_packets_out = new ofstream(active_packets_out_file.c_str());
    }
#endif

#ifdef TRACK_CREDITS
    string used_credits_out_file = config.GetStr( "used_credits_out" );
    if(used_credits_out_file == "") {
        _used_credits_out = NULL;
    } else {
        _used_credits_out = new ofstream(used_credits_out_file.c_str());
    }
    string free_credits_out_file = config.GetStr( "free_credits_out" );
    if(free_credits_out_file == "") {
        _free_credits_out = NULL;
    } else {
        _free_credits_out = new ofstream(free_credits_out_file.c_str());
    }
    string max_credits_out_file = config.GetStr( "max_credits_out" );
    if(max_credits_out_file == "") {
        _max_credits_out = NULL;
    } else {
        _max_credits_out = new ofstream(max_credits_out_file.c_str());
    }
#endif

    // ============ Statistics ============

    _plat_stats.resize(_classes);
    _overall_min_plat.resize(_classes, 0.0);
    _overall_avg_plat.resize(_classes, 0.0);
    _overall_max_plat.resize(_classes, 0.0);

    _nlat_stats.resize(_classes);
    _overall_min_nlat.resize(_classes, 0.0);
    _overall_avg_nlat.resize(_classes, 0.0);
    _overall_max_nlat.resize(_classes, 0.0);

    _first_nlat_stats.resize(_classes);
    _overall_min_first_nlat.resize(_classes, 0.0);
    _overall_avg_first_nlat.resize(_classes, 0.0);
    _overall_max_first_nlat.resize(_classes, 0.0);

    _flat_stats.resize(_classes);
    _overall_min_flat.resize(_classes, 0.0);
    _overall_avg_flat.resize(_classes, 0.0);
    _overall_max_flat.resize(_classes, 0.0);

    _frag_stats.resize(_classes);
    _overall_min_frag.resize(_classes, 0.0);
    _overall_avg_frag.resize(_classes, 0.0);
    _overall_max_frag.resize(_classes, 0.0);

    if(_pair_stats){
        _pair_plat.resize(_classes);
        _pair_nlat.resize(_classes);
        _pair_flat.resize(_classes);
    }

    _hop_stats.resize(_classes);
    _overall_hop_stats.resize(_classes, 0.0);

    _sent_packets.resize(_classes);
    _overall_min_sent_packets.resize(_classes, 0.0);
    _overall_avg_sent_packets.resize(_classes, 0.0);
    _overall_max_sent_packets.resize(_classes, 0.0);
    _accepted_packets.resize(_classes);
    _overall_min_accepted_packets.resize(_classes, 0.0);
    _overall_avg_accepted_packets.resize(_classes, 0.0);
    _overall_max_accepted_packets.resize(_classes, 0.0);

    _sent_flits.resize(_classes);
    _overall_min_sent.resize(_classes, 0.0);
    _overall_avg_sent.resize(_classes, 0.0);
    _overall_max_sent.resize(_classes, 0.0);
    _accepted_flits.resize(_classes);
    _overall_min_accepted.resize(_classes, 0.0);
    _overall_avg_accepted.resize(_classes, 0.0);
    _overall_max_accepted.resize(_classes, 0.0);

#ifdef TRACK_STALLS
    _buffer_busy_stalls.resize(_classes);
    _buffer_conflict_stalls.resize(_classes);
    _buffer_full_stalls.resize(_classes);
    _buffer_reserved_stalls.resize(_classes);
    _crossbar_conflict_stalls.resize(_classes);
    _overall_buffer_busy_stalls.resize(_classes, 0);
    _overall_buffer_conflict_stalls.resize(_classes, 0);
    _overall_buffer_full_stalls.resize(_classes, 0);
    _overall_buffer_reserved_stalls.resize(_classes, 0);
    _overall_crossbar_conflict_stalls.resize(_classes, 0);
#endif

    for ( int c = 0; c < _classes; ++c ) {
        ostringstream tmp_name;

        tmp_name << "plat_stat_" << c;
        _plat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
        _stats[tmp_name.str()] = _plat_stats[c];
        tmp_name.str("");

        tmp_name << "nlat_stat_" << c;
        _nlat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
        _stats[tmp_name.str()] = _nlat_stats[c];
        tmp_name.str("");

        tmp_name << "first_nlat_stat_" << c;
        _first_nlat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
        _stats[tmp_name.str()] = _first_nlat_stats[c];
        tmp_name.str("");

        tmp_name << "flat_stat_" << c;
        _flat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
        _stats[tmp_name.str()] = _flat_stats[c];
        tmp_name.str("");

        tmp_name << "frag_stat_" << c;
        _frag_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 100 );
        _stats[tmp_name.str()] = _frag_stats[c];
        tmp_name.str("");

        tmp_name << "hop_stat_" << c;
        _hop_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 20 );
        _stats[tmp_name.str()] = _hop_stats[c];
        tmp_name.str("");

        if(_pair_stats){
            _pair_plat[c].resize(_nodes*_nodes);
            _pair_nlat[c].resize(_nodes*_nodes);
            _pair_flat[c].resize(_nodes*_nodes);
        }

        _sent_packets[c].resize(_nodes, 0);
        _accepted_packets[c].resize(_nodes, 0);
        _sent_flits[c].resize(_nodes, 0);
        _accepted_flits[c].resize(_nodes, 0);

#ifdef TRACK_STALLS
        _buffer_busy_stalls[c].resize(_subnets*_routers, 0);
        _buffer_conflict_stalls[c].resize(_subnets*_routers, 0);
        _buffer_full_stalls[c].resize(_subnets*_routers, 0);
        _buffer_reserved_stalls[c].resize(_subnets*_routers, 0);
        _crossbar_conflict_stalls[c].resize(_subnets*_routers, 0);
#endif
        if(_pair_stats){
            for ( int i = 0; i < _nodes; ++i ) {
                for ( int j = 0; j < _nodes; ++j ) {
                    tmp_name << "pair_plat_stat_" << c << "_" << i << "_" << j;
                    _pair_plat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
                    _stats[tmp_name.str()] = _pair_plat[c][i*_nodes+j];
                    tmp_name.str("");

                    tmp_name << "pair_nlat_stat_" << c << "_" << i << "_" << j;
                    _pair_nlat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
                    _stats[tmp_name.str()] = _pair_nlat[c][i*_nodes+j];
                    tmp_name.str("");

                    tmp_name << "pair_flat_stat_" << c << "_" << i << "_" << j;
                    _pair_flat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
                    _stats[tmp_name.str()] = _pair_flat[c][i*_nodes+j];
                    tmp_name.str("");
                }
            }
        }
    }

    _slowest_flit.resize(_classes, -1);
    _slowest_packet.resize(_classes, -1);
    _slowest_first_inj_to_ret_packet.resize(_classes, -1);



}

TrafficManager::~TrafficManager( )
{
    for ( int c = 0; c < _classes; ++c ) {
        delete _plat_stats[c];
        delete _nlat_stats[c];
        delete _first_nlat_stats[c];
        delete _flat_stats[c];
        delete _frag_stats[c];
        delete _hop_stats[c];

        delete _traffic_pattern[c];
        delete _injection_process[c];
        if(_pair_stats){
            for ( int i = 0; i < _nodes; ++i ) {
                for ( int j = 0; j < _nodes; ++j ) {
                    delete _pair_plat[c][i*_nodes+j];
                    delete _pair_nlat[c][i*_nodes+j];
                    delete _pair_flat[c][i*_nodes+j];
                }
            }
        }
    }

    if(gWatchOut && (gWatchOut != &cout)) delete gWatchOut;
    if(_stats_out && (_stats_out != &cout)) delete _stats_out;

#ifdef TRACK_FLOWS
    if(_injected_flits_out) delete _injected_flits_out;
    if(_received_flits_out) delete _received_flits_out;
    if(_stored_flits_out) delete _stored_flits_out;
    if(_sent_flits_out) delete _sent_flits_out;
    if(_outstanding_credits_out) delete _outstanding_credits_out;
    if(_ejected_flits_out) delete _ejected_flits_out;
    if(_active_packets_out) delete _active_packets_out;
#endif

#ifdef TRACK_CREDITS
    if(_used_credits_out) delete _used_credits_out;
    if(_free_credits_out) delete _free_credits_out;
    if(_max_credits_out) delete _max_credits_out;
#endif

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
                   << ", flat = " << f->atime - f->itime
                   << ")." << endl;
    }

// This check is not relevant when the initiator of the transaction is the one
// retiring the flit.  (This check expects the destination to retire it.)
/*
    if ( f->head && ( f->dest != dest ) ) {
        ostringstream err;
        err << "Flit " << f->id << " arrived at incorrect output " << dest;
        Error( err.str( ) );
    }
*/
    if((_slowest_flit[f->cl] < 0) ||
       (_flat_stats[f->cl]->Max() < (f->atime - f->itime)))
        _slowest_flit[f->cl] = f->id;
    _flat_stats[f->cl]->AddSample( f->atime - f->itime);
    if(_pair_stats){
        _pair_flat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->itime );
    }

    if ( f->tail ) {
        Flit * head;
        if(f->head) {
            head = f;
        } else {
            map<int64_t, Flit *>::iterator iter = _retired_packets[f->cl].find(f->pid);
            assert(iter != _retired_packets[f->cl].end());
            head = iter->second;
            _retired_packets[f->cl].erase(iter);
            assert(head->head);
            assert(f->pid == head->pid);
        }
        //if (f->data) {
            //_injection_process[f->cl]->eject(f->data);
        //}
        if ( f->watch ) {
            *gWatchOut << GetSimTime() << " | "
                       << "node" << dest << " | "
                       << "Retiring packet " << f->pid
                       << " (plat = " << f->atime - head->ctime
                       << ", nlat = " << f->atime - head->itime
                       << ", frag = " << (f->atime - head->atime) - (f->id - head->id) // NB: In the spirit of solving problems using ugly hacks, we compute the packet length by taking advantage of the fact that the IDs of flits within a packet are contiguous.
                       << ", src = " << head->src
                       << ", dest = " << head->dest
                       << ")." << endl;
        }

        //code the source of request, look carefully, its tricky ;)
/* Moved to Endpoint::_ProcessReceivedFlits
        if (f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST) {
            PacketReplyInfo* rinfo = PacketReplyInfo::New();
            rinfo->source = f->src;
            rinfo->time = f->atime;
            rinfo->record = f->record;
            rinfo->type = f->type;
            _repliesPending[dest].push_back(rinfo);
        } else {
            if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
                _requestsOutstanding[dest]--;
            } else if(f->type == Flit::ANY_TYPE) {
                _requestsOutstanding[f->src]--;
            }

        }
*/
        // FIXME!!!
        // If the message was not a request, decrement the requestsoutstanding from the sender...
        //   But why?  Why would the sender consider this to be an outstanding request???
        if (!(f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST)) {
          if (f->type == Flit::ANY_TYPE) {
                _requestsOutstanding[f->src]--;
          }
        }



        // Only record statistics once per packet (at tail)
        // and based on the simulation state
        if ( ( _sim_state == warming_up ) || f->record ) {

            _hop_stats[f->cl]->AddSample( f->hops );

            if((_slowest_packet[f->cl] < 0) ||
               (_plat_stats[f->cl]->Max() < (f->atime - head->itime)))
                _slowest_packet[f->cl] = f->pid;
            if((_slowest_first_inj_to_ret_packet[f->cl] < 0) ||
               (_plat_stats[f->cl]->Max() < (f->atime - head->first_itime)))
                _slowest_first_inj_to_ret_packet[f->cl] = f->pid;

            _plat_stats[f->cl]->AddSample( f->atime - head->ctime);
            _nlat_stats[f->cl]->AddSample( f->atime - head->itime);
            _first_nlat_stats[f->cl]->AddSample( f->atime - head->first_itime );
            _frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );

            if(_pair_stats){
                _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - head->ctime );
                _pair_nlat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - head->itime );
            }
        }

        if(f != head) {
// Endpoint frees the flits when ack is received (after the call to this function).
//            head->Free();
        }

    }

    if(f->head && !f->tail) {
        _retired_packets[f->cl].insert(make_pair(f->pid, f));
    } else {
// Endpoint frees the flits when ack is received (after the call to this function).
//        f->Free();
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


    // Capture any flits ejecting from the network into the temporary "flits" vector.
    vector<map<int, Flit *> > flits(_subnets);

    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
        for ( int n = 0; n < _nodes; ++n ) {
            _endpoints[n]->_UpdateTime(_time);
            Flit * const f = _net[subnet]->ReadFlit( n );
            if ( f ) {
/*
                if(f->watch) {
                    *gWatchOut << GetSimTime() << " | "
                               << "node" << n << " | "
                               << "Ejecting flit " << f->id
                               << " (packet " << f->pid << ")"
                               << " from VC " << f->vc
                               << "." << endl;
                }
                flits[subnet].insert(make_pair(n, f));
*/

                _endpoints[n]->_ReceiveFlit(subnet, f);

                // It seems strange that we also count warmup, then in the final stats output,
                // we only include the running state, not the warming_up state.  That is
                // because, when we transition into the running state, we clear all stats.
                if (((_sim_state == warming_up) || (_sim_state == running)) &&
                    (f->type != Flit::CTRL_TYPE)) {
                  ++_accepted_flits[f->cl][n];
                  if(f->tail) {
                      ++_accepted_packets[f->cl][n];
                  }
                }
            }

            Credit * const c = _net[subnet]->ReadCredit( n );
            if ( c ) {
#ifdef TRACK_FLOWS
                for(set<int>::const_iterator iter = c->vc.begin(); iter != c->vc.end(); ++iter) {
                    int const vc = *iter;
                    assert(!_outstanding_classes[n][subnet][vc].empty());
                    int cl = _outstanding_classes[n][subnet][vc].front();
                    _outstanding_classes[n][subnet][vc].pop();
                    assert(_outstanding_credits[cl][subnet][n] > 0);
                    --_outstanding_credits[cl][subnet][n];
                }
#endif
//                _buf_states[n][subnet]->ProcessCredit(c);
//                c->Free();

                // Return credit from network to endpoint
                _endpoints[n]->_ReceiveCredit(subnet, c);

            }
        }
        _net[subnet]->ReadInputs( );
    }


    if ( !_empty_network ) {
        // Call each endpoint to determine if they will generate a packet this cycle.
        // Previously, this was handled in the _Inject function.  If they decide to
        // generate a packet, they will be queued up in their internal _partial_packets
        // structure, and will be injected when _Step is called below.
        for ( int input = 0; input < _nodes; ++input ) {
            _endpoints[input]->_EvaluateNewPacketInjection();
        }
    }

    for(int subnet = 0; subnet < _subnets; ++subnet) {
        for(int n = 0; n < _nodes; ++n) {
            // If an endpoint has a flit ready to inject into the network, do it now.
            Flit * f = _endpoints[n]->_Step(subnet);
            if (f) {
                _net[subnet]->WriteFlit(f, n);
                // It seems strange that we also count warmup, then in the final stats output,
                // we only include the running state, not the warming_up state.  That is
                // because, when we transition into the running state, we clear all stats.
                if (((_sim_state == warming_up) || (_sim_state == running)) &&
                    (f->type != Flit::CTRL_TYPE)) {
                  ++_sent_flits[f->cl][n];
                  if (f->head) {
                      ++_sent_packets[f->cl][n];
                  }
                }
            }
        }
    }


    for(int subnet = 0; subnet < _subnets; ++subnet) {
        for(int n = 0; n < _nodes; ++n) {


/*
            map<int, Flit *>::const_iterator iter = flits[subnet].find(n);
            if(iter != flits[subnet].end()) {
                Flit * const f = iter->second;

                f->atime = _time;
                if(f->watch) {
                    *gWatchOut << GetSimTime() << " | "
                               << "node" << n << " | "
                               << "Injecting credit for VC " << f->vc
                               << " into subnet " << subnet
                               << "." << endl;
                }
                Credit * const c = Credit::New();
                c->vc.insert(f->vc);

                _net[subnet]->WriteCredit(c, n);
*/

            Flit * retired_flit = NULL;
            Credit * c = _endpoints[n]->_ProcessReceivedFlits(subnet, retired_flit);
            if (c) {
              _net[subnet]->WriteCredit(c, n);
            }
            if (retired_flit) {
#ifdef TRACK_FLOWS
              ++_ejected_flits[retired_flit->cl][n];
#endif
// Endpoint retires the flits when ack is received and it is removed from the OPB.
//              _RetireFlit(retired_flit, n);
            }

        }
//        flits[subnet].clear();
        _net[subnet]->Evaluate( );
        _net[subnet]->WriteOutputs( );
    }

    ++_time;
    assert(_time);
    if(gTrace){
        cout<<"TIME "<<_time<<endl;
    }

}

bool TrafficManager::_InjectionsOutstanding( ) const
{
    for ( int c = 0; c < _classes; ++c ) {
        if ( _measure_stats[c] ) {
            if ( _measured_in_flight_flits[c].empty() ) {

                for ( int s = 0; s < _nodes; ++s ) {
//                    if ( !_qdrained[s][c] ) {
                    if ( !_endpoints[s]->_InjectionQueueDrained(c) ) {
#ifdef DEBUG_DRAIN
                        cout << "waiting on node " << s << " class " << c;
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

bool TrafficManager::_EndPointProcessingOutstanding( ) const
{

    for ( int s = 0; s < _nodes; ++s ) {
        if ( !_endpoints[s]->_EndPointProcessingFinished() ) {
            return true;
        }
    }
    return false;
}

void TrafficManager::_ClearStats( )
{
    _slowest_flit.assign(_classes, -1);
    _slowest_packet.assign(_classes, -1);
    _slowest_first_inj_to_ret_packet.assign(_classes, -1);

    for ( int c = 0; c < _classes; ++c ) {

        _plat_stats[c]->Clear( );
        _nlat_stats[c]->Clear( );
        _first_nlat_stats[c]->Clear( );
        _flat_stats[c]->Clear( );

        _frag_stats[c]->Clear( );

        _sent_packets[c].assign(_nodes, 0);
        _accepted_packets[c].assign(_nodes, 0);
        _sent_flits[c].assign(_nodes, 0);
        _accepted_flits[c].assign(_nodes, 0);

#ifdef TRACK_STALLS
        _buffer_busy_stalls[c].assign(_subnets*_routers, 0);
        _buffer_conflict_stalls[c].assign(_subnets*_routers, 0);
        _buffer_full_stalls[c].assign(_subnets*_routers, 0);
        _buffer_reserved_stalls[c].assign(_subnets*_routers, 0);
        _crossbar_conflict_stalls[c].assign(_subnets*_routers, 0);
#endif
        if(_pair_stats){
            for ( int i = 0; i < _nodes; ++i ) {
                for ( int j = 0; j < _nodes; ++j ) {
                    _pair_plat[c][i*_nodes+j]->Clear( );
                    _pair_nlat[c][i*_nodes+j]->Clear( );
                    _pair_flat[c][i*_nodes+j]->Clear( );
                }
            }
        }
        _hop_stats[c]->Clear();

    }

    _reset_time = _time;
}

void TrafficManager::_ComputeStats( const vector<int64_t> & stats, int64_t *sum, int64_t *min,
 int64_t *max, int64_t *min_pos, int64_t *max_pos ) const
{
    int const count = stats.size();
    assert(count > 0);

    if(min_pos) {
        *min_pos = 0;
    }
    if(max_pos) {
        *max_pos = 0;
    }

    if(min) {
        *min = stats[0];
    }
    if(max) {
        *max = stats[0];
    }

    *sum = stats[0];

    for ( int i = 1; i < count; ++i ) {
        int curr = stats[i];
        if ( min  && ( curr < *min ) ) {
            *min = curr;
            if ( min_pos ) {
                *min_pos = i;
            }
        }
        if ( max && ( curr > *max ) ) {
            *max = curr;
            if ( max_pos ) {
                *max_pos = i;
            }
        }
        *sum += curr;
    }
}

void TrafficManager::_DisplayRemaining( ostream & os ) const
{
    for(int c = 0; c < _classes; ++c) {

        map<int64_t, Flit *>::const_iterator iter;
        int i;

        os << "Time: " << _time << endl;

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
    vector<int64_t> total_accepted_count(_classes, 0);
    vector<int64_t> prev_accepted_count(_classes, 0);


    bool clear_last = false;
    int total_phases = 0;
    int num_no_accepted_flit_period = 0;

    if(_warmup_periods == 0)
       _sim_state = running;

    while( ( total_phases < _max_samples ) &&
           ( ( _sim_state != running ) ||
             ( ( converged < 3 ) ||
               ( total_phases < _min_samples ) ) ) ) {

        if ( clear_last || (( ( _sim_state == warming_up ) && ( ( total_phases % 2 ) == 0 ) )) ) {
            clear_last = false;
            _ClearStats( );
        }


        // If there's no sample within period, keep going
        int total_samples;

        if (gSwm) {
            if(gSwmAppRunMode) {
                while(gSimEnabled) {
                   _Step( );
                }
            } else {
                for (int iter = 0; gSimEnabled && iter < _sample_period; ++iter) {
                   _Step( );
                }
            }
        } else {
            do {
                for ( int iter = 0; iter < _sample_period; ++iter )
                    _Step( );
                total_samples = 0;
                for(int c = 0; c < _classes; ++c) {
                    total_samples += _plat_stats[c]->NumSamples( );
                }
                if (total_samples == 0){
                    cout << "Zero packet recorded within sampling period. Extending" << endl;
                }
            } while (total_samples == 0);
        }

        UpdateStats();
        DisplayStats();

        int lat_exc_class = -1;
        int lat_chg_exc_class = -1;
        int acc_chg_exc_class = -1;

        int accepted_flit_count_accum = 0;

        for(int c = 0; c < _classes; ++c) {
            if(_measure_stats[c] == 0) {
                continue;
            }

            _ComputeStats( _accepted_flits[c], &total_accepted_count[c] );
            int64_t accepted_count_change = total_accepted_count[c] - prev_accepted_count[c];
            prev_accepted_count[c] = total_accepted_count[c];
            accepted_flit_count_accum += accepted_count_change;
        }

        if (accepted_flit_count_accum == 0) {
            cout << "Warning: no flit accepted this period. Not updating stats. " <<
                num_no_accepted_flit_period << endl;

            num_no_accepted_flit_period ++;
            if (num_no_accepted_flit_period >= _max_no_accepted_flit_period){
               cout << "no flit accepted for " << _max_no_accepted_flit_period << " periods. Ending" << endl;
                break;
            }
            continue;
        } else {
            num_no_accepted_flit_period = 0;
            cout << "New flit accepted this period : " << accepted_flit_count_accum << endl;
        }


        for(int c = 0; c < _classes; ++c) {

            if(_measure_stats[c] == 0) {
                continue;
            }

            double cur_latency = _plat_stats[c]->Average( );

            double total_accepted_rate = (double)total_accepted_count[c] / (double)(_time - _reset_time);
            double cur_accepted = total_accepted_rate / (double)_nodes;

            double latency_change = fabs((cur_latency - prev_latency[c]) / cur_latency);
            prev_latency[c] = cur_latency;

            double accepted_change = fabs((cur_accepted - prev_accepted[c]) / cur_accepted);
            prev_accepted[c] = cur_accepted;

            double latency = (double)_plat_stats[c]->Sum();
            double count = (double)_plat_stats[c]->NumSamples();

            map<int64_t, Flit *>::const_iterator iter;
            for(iter = _total_in_flight_flits[c].begin();
                iter != _total_in_flight_flits[c].end();
                iter++) {
                latency += (double)(_time - iter->second->ctime);
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
        if ( _measure_latency && ( lat_exc_class >= 0 ) ) {

            cout << "Average latency for class " << lat_exc_class << " exceeded " << _latency_thres[lat_exc_class] << " cycles. Aborting simulation." << endl;
            converged = 0;
            _sim_state = draining;
            _drain_time = _time;
            if(_stats_out) {
                WriteStats(*_stats_out);
            }
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

        cout << endl;
    }


    if ( _sim_state == running ) {
        ++converged;

        _sim_state  = draining;
        _drain_time = _time;

        if ( _measure_latency ) {
            cout << GetSimTime() << ": Draining all recorded packets from injection queues."
                 << "  (Still allowing slow endpoints to inject new packets to catch up.) ..." << endl;
            int empty_steps = 0;
            while( _InjectionsOutstanding( ) ) {
                _Step( );

                ++empty_steps;

                if ( empty_steps % 10000 == 0 ) {

                    int lat_exc_class = -1;

                    for(int c = 0; c < _classes; c++) {

                        double threshold = _latency_thres[c];

                        if(threshold < 0.0) {
                            continue;
                        }

                        double acc_latency = _plat_stats[c]->Sum();
                        double acc_count = (double)_plat_stats[c]->NumSamples();

                        map<int64_t, Flit *>::const_iterator iter;
                        for(iter = _total_in_flight_flits[c].begin();
                            iter != _total_in_flight_flits[c].end();
                            iter++) {
                            acc_latency += (double)(_time - iter->second->ctime);
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
                        if(_stats_out) {
                            WriteStats(*_stats_out);
                        }
                        break;
                    }
                    _DisplayRemaining( );

                }
            }
        } else {
            cout << "Still draining for correctness sake" << endl << flush;
            while( !gShouldSkipDrain  && _InjectionsOutstanding( ) ) {
                _Step( );
            }
        }
    } else {
        cout << "Too many sample periods needed to converge" << endl;
    }

    return ( converged > 0 );
}

bool TrafficManager::Run( )
{
    bool sim_passed = true;
    for ( int sim = 0; sim < _total_sims; ++sim ) {

        _time = 0;

        //remove any pending request from the previous simulations
        _requestsOutstanding.assign(_nodes, 0);
        for (int i=0;i<_nodes;i++) {
            while(!_repliesPending[i].empty()) {
                _repliesPending[i].front()->Free();
                _repliesPending[i].pop_front();
            }
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

        for(int c = 0; c < _classes; ++c) {
            _traffic_pattern[c]->reset();
            _injection_process[c]->reset();
        }

        if ( !_SingleSim( ) ) {
            cout << "Simulation unstable, ending ..." << endl;
            for(int node = 0; node < _nodes; ++node) {
              _endpoints[node]->_EndSimulation();
            }
            _UpdateOverallStats();
            DisplayOverallStats();
            return false;
        }

        if (!gShouldSkipDrain){
            // Empty any remaining packets
            cout << GetSimTime() << ": Draining remaining packets.  (All endpoints caught up.  Stopping generation of new packets.) ..." << endl;
            _empty_network = true;
            _generation_stopped_time = _time;
            int empty_steps = 0;

            bool packets_left = false;
            vector<long unsigned int> flit_left;
            flit_left.resize(_classes,0);

            for(int c = 0; c < _classes; ++c) {
                packets_left |= !_total_in_flight_flits[c].empty();
                flit_left[c] = _total_in_flight_flits[c].size();
            }

            //if(gSwm) {
                //_sim_state = running;
                //cout << _time << "sim state set to R" << _sim_state << endl;
            //}

            // for swm
            int unchange = 0;
            while( packets_left ) {
                _Step( );

                ++empty_steps;

                if ( empty_steps % 10000 == 0 ) {
                    _DisplayRemaining( );
                }

                packets_left = false;
                for(int c = 0; c < _classes; ++c) {
                    packets_left |= !_total_in_flight_flits[c].empty();
                    if (flit_left[c] != _total_in_flight_flits[c].size()) {
                        flit_left[c] = _total_in_flight_flits[c].size();
                        unchange = 0;
                    } else {
                        unchange++;
                    }
                }
                // breakout while loop when remaining flit can't be drained, eg flits
                // don't change for over 10k times, so overall stats can be displayed.
                if (unchange > 10000) {
                    cout << "break out draining... total_flit unchange= " << unchange << endl;
                    break;
                }
            }

            while( _EndPointProcessingOutstanding( ) ) {
                _Step( );
                ++empty_steps;

                if ( empty_steps % 10000 == 0 ) {
                    //cout << GetSimTime() << "Draining Processing" << endl;
                }
            }

            // If there are no flits in-flight, then the OPB should be empty.
            // Check that here.
            for (int node = 0; node < _nodes; node++) {
            if ( !_endpoints[node]->_OPBDrained() ) {
                cout << "ERROR: Endpoint " << node << " still has outstanding packets in its OPB "
                    << "though measured_in_flight_flits is empty!!!" << endl;
                _endpoints[node]->DumpOPB();
                return false;
            }
            }

            //wait until all the credits are drained as well
            while(Credit::OutStanding()!=0){
                _Step();
            }

            // Also wait until all ACKs have been sent.
            // It could be the case that all OPBs are empty, but an endpoint
            // believes that it still has to send an ACK.  This can happen if this
            // endpoint has previously sent an ACK, but then received older
            // (retransmitted) packets due to delays in the network.  (The initiator
            // started the retransmissions before it had received the last ACK.)
            // But the receiver cannot determine whether the initiator just hadn't
            // received the ACK yet, or if the ACK was dropped.
            // So it must resend this ACK.
            bool acks_drained = false;
            while (!acks_drained) {
            _Step();
            acks_drained = true;
            for (int node = 0; node < _nodes; node++) {
                if ( _endpoints[node]->_AcksToReturn() ) {
                acks_drained = false;
                }
            }
            }

            _empty_network = false;

            //for the love of god don't ever say "Time taken" anywhere else
            //the power script depend on it
            cout << "Time taken is " << _time << " cycles" <<endl;
        }


        // Simulation is complete, inform the endpoints so they can do any final
        // checks and closing operations.
        for (int node = 0; node < _nodes; node++) {
          if (!_endpoints[node]->_EndSimulation()) {
            sim_passed = false;
          }
          _flits_dropped_for_rget_conversion += _endpoints[node]->_GetFlitsDroppedForRgetConversion();
        }


        if(_stats_out) {
            WriteStats(*_stats_out);
        }
        if (gSwm){
             UpdateStats();
             DisplayStats();
        }
        _UpdateOverallStats();
    }

    // Display SWM stats
   for(int c = 0; c < _classes; ++c) {
      _injection_process[c]->print_stats();
   }
    DisplayOverallStats();
    if(_print_csv_results) {
        DisplayOverallStatsCSV();
    }

    return sim_passed;
}

void TrafficManager::_UpdateOverallStats() {
    for ( int c = 0; c < _classes; ++c ) {

        if(_measure_stats[c] == 0) {
            continue;
        }

        _overall_min_plat[c] += _plat_stats[c]->Min();
        _overall_avg_plat[c] += _plat_stats[c]->Average();
        _overall_max_plat[c] += _plat_stats[c]->Max();
        _overall_min_nlat[c] += _nlat_stats[c]->Min();
        _overall_avg_nlat[c] += _nlat_stats[c]->Average();
        _overall_max_nlat[c] += _nlat_stats[c]->Max();
        _overall_min_first_nlat[c] += _first_nlat_stats[c]->Min();
        _overall_avg_first_nlat[c] += _first_nlat_stats[c]->Average();
        _overall_max_first_nlat[c] += _first_nlat_stats[c]->Max();
        _overall_min_flat[c] += _flat_stats[c]->Min();
        _overall_avg_flat[c] += _flat_stats[c]->Average();
        _overall_max_flat[c] += _flat_stats[c]->Max();

        _overall_min_frag[c] += _frag_stats[c]->Min();
        _overall_avg_frag[c] += _frag_stats[c]->Average();
        _overall_max_frag[c] += _frag_stats[c]->Max();

        _overall_hop_stats[c] += _hop_stats[c]->Average();

        int64_t count_min, count_sum, count_max;
        double rate_min, rate_sum, rate_max;
        double rate_avg;
        double time_delta = (double)(_drain_time - _reset_time);
        _ComputeStats( _sent_flits[c], &count_sum, &count_min, &count_max );
        rate_min = (double)count_min / time_delta;
        rate_sum = (double)count_sum / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        _overall_min_sent[c] += rate_min;
        _overall_avg_sent[c] += rate_avg;
        _overall_max_sent[c] += rate_max;
        _ComputeStats( _sent_packets[c], &count_sum, &count_min, &count_max );
        rate_min = (double)count_min / time_delta;
        rate_sum = (double)count_sum / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        _overall_min_sent_packets[c] += rate_min;
        _overall_avg_sent_packets[c] += rate_avg;
        _overall_max_sent_packets[c] += rate_max;
        _ComputeStats( _accepted_flits[c], &count_sum, &count_min, &count_max );
        rate_min = (double)count_min / time_delta;
        rate_sum = (double)count_sum / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        _overall_min_accepted[c] += rate_min;
        _overall_avg_accepted[c] += rate_avg;
        _overall_max_accepted[c] += rate_max;
        _ComputeStats( _accepted_packets[c], &count_sum, &count_min, &count_max );
        rate_min = (double)count_min / time_delta;
        rate_sum = (double)count_sum / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        _overall_min_accepted_packets[c] += rate_min;
        _overall_avg_accepted_packets[c] += rate_avg;
        _overall_max_accepted_packets[c] += rate_max;

#ifdef TRACK_STALLS
        _ComputeStats(_buffer_busy_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_buffer_busy_stalls[c] += rate_avg;
        _ComputeStats(_buffer_conflict_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_buffer_conflict_stalls[c] += rate_avg;
        _ComputeStats(_buffer_full_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_buffer_full_stalls[c] += rate_avg;
        _ComputeStats(_buffer_reserved_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_buffer_reserved_stalls[c] += rate_avg;
        _ComputeStats(_crossbar_conflict_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_crossbar_conflict_stalls[c] += rate_avg;
#endif

    }
}

void TrafficManager::EndSimulation() {
  for(int node = 0; node < _nodes; ++node) {
    _endpoints[node]->_EndSimulation();
  }
  _UpdateOverallStats();
  DisplayOverallStats();
}

void TrafficManager::WriteStats(ostream & os) const {

    os << "%=================================" << endl;

    for(int c = 0; c < _classes; ++c) {

        if(_measure_stats[c] == 0) {
            continue;
        }

        //c+1 due to matlab array starting at 1
        os << "plat(" << c+1 << ") = " << _plat_stats[c]->Average() << ";" << endl
           << "plat_hist(" << c+1 << ",:) = " << *_plat_stats[c] << ";" << endl
           << "nlat(" << c+1 << ") = " << _nlat_stats[c]->Average() << ";" << endl
           << "nlat_hist(" << c+1 << ",:) = " << *_nlat_stats[c] << ";" << endl
           << "first_nlat(" << c+1 << ") = " << _first_nlat_stats[c]->Average() << ";" << endl
           << "first_nlat_hist(" << c+1 << ",:) = " << *_first_nlat_stats[c] << ";" << endl
           << "flat(" << c+1 << ") = " << _flat_stats[c]->Average() << ";" << endl
           << "flat_hist(" << c+1 << ",:) = " << *_flat_stats[c] << ";" << endl
           << "frag_hist(" << c+1 << ",:) = " << *_frag_stats[c] << ";" << endl
           << "hops(" << c+1 << ",:) = " << *_hop_stats[c] << ";" << endl;
        if(_pair_stats){
            os<< "pair_sent(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_plat[c][i*_nodes+j]->NumSamples() << " ";
                }
            }
            os << "];" << endl
               << "pair_plat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_plat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
            os << "];" << endl
               << "pair_nlat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_nlat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
            os << "];" << endl
               << "pair_flat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_flat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
        }

        double time_delta = (double)(_drain_time - _reset_time);

        os << "];" << endl
           << "sent_packets(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_packets[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "accepted_packets(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_packets[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "sent_flits(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_flits[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "accepted_flits(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_flits[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "sent_packet_size(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_flits[c][d] / (double)_sent_packets[c][d] << " ";
        }
        os << "];" << endl
           << "accepted_packet_size(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_flits[c][d] / (double)_accepted_packets[c][d] << " ";
        }
        os << "];" << endl;
#ifdef TRACK_STALLS
        os << "buffer_busy_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_busy_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_conflict_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_conflict_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_full_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_full_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_reserved_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_reserved_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "crossbar_conflict_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_crossbar_conflict_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl;
#endif
    }
}

void TrafficManager::UpdateStats() {
#if defined(TRACK_FLOWS) || defined(TRACK_STALLS)
    for(int c = 0; c < _classes; ++c) {
#ifdef TRACK_FLOWS
        {
            char trail_char = (c == _classes - 1) ? '\n' : ',';
            if(_injected_flits_out) *_injected_flits_out << _injected_flits[c] << trail_char;
            _injected_flits[c].assign(_nodes, 0);
            if(_ejected_flits_out) *_ejected_flits_out << _ejected_flits[c] << trail_char;
            _ejected_flits[c].assign(_nodes, 0);
        }
#endif
        for(int subnet = 0; subnet < _subnets; ++subnet) {
#ifdef TRACK_FLOWS
            if(_outstanding_credits_out) *_outstanding_credits_out << _outstanding_credits[c][subnet] << ',';
            if(_stored_flits_out) *_stored_flits_out << vector<int>(_nodes, 0) << ',';
#endif
            for(int router = 0; router < _routers; ++router) {
                Router * const r = _router[subnet][router];
#ifdef TRACK_FLOWS
                char trail_char =
                    ((router == _routers - 1) && (subnet == _subnets - 1) && (c == _classes - 1)) ? '\n' : ',';
                if(_received_flits_out) *_received_flits_out << r->GetReceivedFlits(c) << trail_char;
                if(_stored_flits_out) *_stored_flits_out << r->GetStoredFlits(c) << trail_char;
                if(_sent_flits_out) *_sent_flits_out << r->GetSentFlits(c) << trail_char;
                if(_outstanding_credits_out) *_outstanding_credits_out << r->GetOutstandingCredits(c) << trail_char;
                if(_active_packets_out) *_active_packets_out << r->GetActivePackets(c) << trail_char;
                r->ResetFlowStats(c);
#endif
#ifdef TRACK_STALLS
                _buffer_busy_stalls[c][subnet*_routers+router] += r->GetBufferBusyStalls(c);
                _buffer_conflict_stalls[c][subnet*_routers+router] += r->GetBufferConflictStalls(c);
                _buffer_full_stalls[c][subnet*_routers+router] += r->GetBufferFullStalls(c);
                _buffer_reserved_stalls[c][subnet*_routers+router] += r->GetBufferReservedStalls(c);
                _crossbar_conflict_stalls[c][subnet*_routers+router] += r->GetCrossbarConflictStalls(c);
                r->ResetStallStats(c);
#endif
            }
        }
    }
#ifdef TRACK_FLOWS
    if(_injected_flits_out) *_injected_flits_out << flush;
    if(_received_flits_out) *_received_flits_out << flush;
    if(_stored_flits_out) *_stored_flits_out << flush;
    if(_sent_flits_out) *_sent_flits_out << flush;
    if(_outstanding_credits_out) *_outstanding_credits_out << flush;
    if(_ejected_flits_out) *_ejected_flits_out << flush;
    if(_active_packets_out) *_active_packets_out << flush;
#endif
#endif

#ifdef TRACK_CREDITS
    for(int s = 0; s < _subnets; ++s) {
        for(int r = 0; r < _routers; ++r) {
            Router const * const rtr = _router[s][r];
            char trail_char =
                ((r == _routers - 1) && (s == _subnets - 1)) ? '\n' : ',';
            if(_used_credits_out) *_used_credits_out << rtr->UsedCredits() << trail_char;
            if(_free_credits_out) *_free_credits_out << rtr->FreeCredits() << trail_char;
            if(_max_credits_out) *_max_credits_out << rtr->MaxCredits() << trail_char;
        }
    }
    if(_used_credits_out) *_used_credits_out << flush;
    if(_free_credits_out) *_free_credits_out << flush;
    if(_max_credits_out) *_max_credits_out << flush;
#endif

}

void TrafficManager::DisplayStats(ostream & os) const {

    cout << "Cycle: " << GetSimTime() << endl;
    for(int c = 0; c < _classes; ++c) {

        if(_measure_stats[c] == 0) {
            continue;
        }

        cout << "Class " << c << ":" << endl;

        cout
            << "Packet latency average = " << _plat_stats[c]->Average() << endl
            << "\tminimum = " << _plat_stats[c]->Min() << endl
            << "\tmaximum = " << _plat_stats[c]->Max() << endl
            << "Network latency average = " << _nlat_stats[c]->Average() << endl
            << "\tminimum = " << _nlat_stats[c]->Min() << endl
            << "\tmaximum = " << _nlat_stats[c]->Max() << endl
            << "Slowest packet = " << _slowest_packet[c] << endl

            << "First inject to retire latency average = " << _first_nlat_stats[c]->Average() << endl
            << "\tminimum = " << _first_nlat_stats[c]->Min() << endl
            << "\tmaximum = " << _first_nlat_stats[c]->Max() << endl
            << "Slowest packet = " << _slowest_first_inj_to_ret_packet[c] << endl

            << "Flit latency average = " << _flat_stats[c]->Average() << endl
            << "\tminimum = " << _flat_stats[c]->Min() << endl
            << "\tmaximum = " << _flat_stats[c]->Max() << endl
            << "Slowest flit = " << _slowest_flit[c] << endl
            << "Fragmentation average = " << _frag_stats[c]->Average() << endl
            << "\tminimum = " << _frag_stats[c]->Min() << endl
            << "\tmaximum = " << _frag_stats[c]->Max() << endl;

        int64_t count_sum, count_min, count_max;
        double rate_sum, rate_min, rate_max;
        double rate_avg;
        int sent_packets, sent_flits, accepted_packets, accepted_flits;
        int64_t min_pos, max_pos;
        double time_delta = (double)(_time - _reset_time);
        _ComputeStats(_sent_packets[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
        rate_sum = (double)count_sum / time_delta;
        rate_min = (double)count_min / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        sent_packets = count_sum;
        cout << "Injected packet rate average = " << rate_avg << endl
             << "\tminimum = " << rate_min
             << " (at node " << min_pos << ")" << endl
             << "\tmaximum = " << rate_max
             << " (at node " << max_pos << ")" << endl;
        _ComputeStats(_accepted_packets[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
        rate_sum = (double)count_sum / time_delta;
        rate_min = (double)count_min / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        accepted_packets = count_sum;
        cout << "Accepted packet rate average = " << rate_avg << endl
             << "\tminimum = " << rate_min
             << " (at node " << min_pos << ")" << endl
             << "\tmaximum = " << rate_max
             << " (at node " << max_pos << ")" << endl;
        _ComputeStats(_sent_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
        rate_sum = (double)count_sum / time_delta;
        rate_min = (double)count_min / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        sent_flits = count_sum;
        cout << "Injected flit rate average = " << rate_avg << endl
             << "\tminimum = " << rate_min
             << " (at node " << min_pos << ")" << endl
             << "\tmaximum = " << rate_max
             << " (at node " << max_pos << ")" << endl;
        _ComputeStats(_accepted_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
        rate_sum = (double)count_sum / time_delta;
        rate_min = (double)count_min / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        accepted_flits = count_sum;
        cout << "Accepted flit rate average= " << rate_avg << endl
             << "\tminimum = " << rate_min
             << " (at node " << min_pos << ")" << endl
             << "\tmaximum = " << rate_max
             << " (at node " << max_pos << ")" << endl;

        cout << "Injected packet length average = " << (double)sent_flits / (double)sent_packets << endl
             << "Accepted packet length average = " << (double)accepted_flits / (double)accepted_packets << endl;

        cout << "Total in-flight flits = " << _total_in_flight_flits[c].size()
             << " (" << _measured_in_flight_flits[c].size() << " measured)"
             << endl;
        cout << "Total packets generated = " << _cur_pid << endl;
        cout << "Total flits generated = " << _cur_id << endl;


#ifdef TRACK_STALLS
        _ComputeStats(_buffer_busy_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Buffer busy stall rate = " << rate_avg << endl;
        _ComputeStats(_buffer_conflict_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Buffer conflict stall rate = " << rate_avg << endl;
        _ComputeStats(_buffer_full_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Buffer full stall rate = " << rate_avg << endl;
        _ComputeStats(_buffer_reserved_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Buffer reserved stall rate = " << rate_avg << endl;
        _ComputeStats(_crossbar_conflict_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Crossbar conflict stall rate = " << rate_avg << endl;
#endif

    }
}

void TrafficManager::DisplayOverallStats( ostream & os ) const {

    os << endl << "====== Overall Traffic Statistics ======" << endl;
    for ( int c = 0; c < _classes; ++c ) {

        if(_measure_stats[c] == 0) {
            continue;
        }

        os << "====== Traffic class " << c << " ======" << endl;

        os << "Packet latency average (including time in inject buf) = " << _overall_avg_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Network latency average = " << _overall_avg_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "First inject to retire latency average = " << _overall_avg_first_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_first_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_first_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples. packet: " << _slowest_first_inj_to_ret_packet[c] << ")" << endl;

        os << "Flit latency average = " << _overall_avg_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Fragmentation average = " << _overall_avg_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        int64_t count_min, count_sum, count_max;
        _ComputeStats( _sent_packets[c], &count_sum, &count_min, &count_max );
        os << "Injected packet rate average overall (including retrans, excluding standalone acks) = " << _overall_avg_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << "  (" << count_sum << " from: " << _reset_time << " to " << _drain_time << ")" << endl;
        os << "\tminimum = " << _overall_min_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        _ComputeStats( _accepted_packets[c], &count_sum, &count_min, &count_max );
        os << "Accepted packet rate average (including retrans, excluding standalone acks) = " << _overall_avg_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << "  (" << count_sum << " from: " << _reset_time << " to " << _drain_time << ")" << endl;
        os << "\tminimum = " << _overall_min_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        _ComputeStats( _sent_flits[c], &count_sum, &count_min, &count_max );
        os << "Injected flit rate average (including retrans, excluding standalone acks) = " << _overall_avg_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << "  (" << count_sum << " from: " << _reset_time << " to " << _drain_time << ")" << endl;
        os << "\tminimum = " << _overall_min_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        _ComputeStats( _accepted_flits[c], &count_sum, &count_min, &count_max );
        os << "Accepted flit rate average (including retrans, excluding standalone acks) = " << _overall_avg_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << "  (" << count_sum << " from: " << _reset_time << " to " << _drain_time << ")" << endl;
        os << "\tminimum = " << _overall_min_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Injected packet size average (including retrans, excluding standalone acks) = " << _overall_avg_sent[c] / _overall_avg_sent_packets[c]
           << " (" << _total_sims << " samples)" << endl;

        os << "Accepted packet size average (including retrans, excluding standalone acks) = " << _overall_avg_accepted[c] / _overall_avg_accepted_packets[c]
           << " (" << _total_sims << " samples)" << endl;

        os << "Hops average = " << _overall_hop_stats[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

#ifdef TRACK_STALLS
        os << "Buffer busy stall rate = " << (double)_overall_buffer_busy_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer conflict stall rate = " << (double)_overall_buffer_conflict_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer full stall rate = " << (double)_overall_buffer_full_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer reserved stall rate = " << (double)_overall_buffer_reserved_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Crossbar conflict stall rate = " << (double)_overall_crossbar_conflict_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
#endif

        os << endl;

        unsigned int created_flits = (_cur_id - 1) - _flits_dropped_for_rget_conversion;

        // These do not include standalone ACKs
        os << "All Time Stats (from cycle 0 to " << _time << ")" << endl;
        os << "=============================================" << endl;;
        os << "Created packets (excluding retrans and standalone acks) = " << _cur_pid-1 << endl;
        os << "Created flits   (excluding retrans, standalone acks, and discarded flits) = " << created_flits << endl;
        os << "Flits discarded for rget conversion: " << _flits_dropped_for_rget_conversion << endl;
        os << "Average created packet size (excluding retrans and standalone acks) = " << (double)(created_flits)/(double)(_cur_pid-1) << endl;
        os << "Created Packets/cycle (excluding retrans and standalone acks) = " << (double)(_cur_pid-1)/(double)_time << endl;
        os << "Created Flits/cycle   (excluding retrans and standalone acks) = " << (double)(created_flits)/(double)_time << endl;

        os << "Packet retransmissions (full sim) = " << _packet_retransmissions << endl;
        os << "Flit retransmissions   (full sim) = " << _flit_retransmissions << endl;
        os << "Average retransmit size = " << (double)_flit_retransmissions/(double)_packet_retransmissions << endl;
        os << "Retransmissions/Packet  = " << (double)_packet_retransmissions/(double)(_cur_pid-1) << endl;
        os << "Retransmissions/Flit    = " << (double)_flit_retransmissions/(double)(created_flits) << endl;

        os << "Standalone ACKs transmitted = " << _standalone_acks_transmitted << endl << endl;

        os << "Warmup ended = " << _reset_time << endl;
        os << "Draining started = " << _drain_time << endl;
        os << "Packet generation stopped = " << _generation_stopped_time << endl;
        os << "Total cycles = " << _time << endl;
        os << "Total packets/cycle = " << ((float)_cur_pid-1)/(float)_time << endl;
        os << "Total flits/cycle (goodput) = " << ((float)created_flits)/(float)_time << endl;
        os << "Total drain time = " << _time - _drain_time << endl;


#ifdef TRACK_FLOWS
        unsigned long total_ejected_flits = 0;
        for ( int n = 0; n < _nodes; ++n ) {
          total_ejected_flits += _ejected_flits[c][n];
        }
        os << "Ejected flits = " << total_ejected_flits << endl;
#endif
    }
}

string TrafficManager::_OverallStatsCSV(int c) const
{
    ostringstream os;
    os << _traffic[c]
       << ',' << _use_read_write[c]
       << ',' << _load[c]
       << ',' << _overall_min_plat[c] / (double)_total_sims
       << ',' << _overall_avg_plat[c] / (double)_total_sims
       << ',' << _overall_max_plat[c] / (double)_total_sims
       << ',' << _overall_min_nlat[c] / (double)_total_sims
       << ',' << _overall_avg_nlat[c] / (double)_total_sims
       << ',' << _overall_max_nlat[c] / (double)_total_sims
       << ',' << _overall_min_first_nlat[c] / (double)_total_sims
       << ',' << _overall_avg_first_nlat[c] / (double)_total_sims
       << ',' << _overall_max_first_nlat[c] / (double)_total_sims
       << ',' << _overall_min_flat[c] / (double)_total_sims
       << ',' << _overall_avg_flat[c] / (double)_total_sims
       << ',' << _overall_max_flat[c] / (double)_total_sims
       << ',' << _overall_min_frag[c] / (double)_total_sims
       << ',' << _overall_avg_frag[c] / (double)_total_sims
       << ',' << _overall_max_frag[c] / (double)_total_sims
       << ',' << _overall_min_sent_packets[c] / (double)_total_sims
       << ',' << _overall_avg_sent_packets[c] / (double)_total_sims
       << ',' << _overall_max_sent_packets[c] / (double)_total_sims
       << ',' << _overall_min_accepted_packets[c] / (double)_total_sims
       << ',' << _overall_avg_accepted_packets[c] / (double)_total_sims
       << ',' << _overall_max_accepted_packets[c] / (double)_total_sims
       << ',' << _overall_min_sent[c] / (double)_total_sims
       << ',' << _overall_avg_sent[c] / (double)_total_sims
       << ',' << _overall_max_sent[c] / (double)_total_sims
       << ',' << _overall_min_accepted[c] / (double)_total_sims
       << ',' << _overall_avg_accepted[c] / (double)_total_sims
       << ',' << _overall_max_accepted[c] / (double)_total_sims
       << ',' << _overall_avg_sent[c] / _overall_avg_sent_packets[c]
       << ',' << _overall_avg_accepted[c] / _overall_avg_accepted_packets[c]
       << ',' << _overall_hop_stats[c] / (double)_total_sims;

#ifdef TRACK_STALLS
    os << ',' << (double)_overall_buffer_busy_stalls[c] / (double)_total_sims
       << ',' << (double)_overall_buffer_conflict_stalls[c] / (double)_total_sims
       << ',' << (double)_overall_buffer_full_stalls[c] / (double)_total_sims
       << ',' << (double)_overall_buffer_reserved_stalls[c] / (double)_total_sims
       << ',' << (double)_overall_crossbar_conflict_stalls[c] / (double)_total_sims;
#endif

    return os.str();
}

void TrafficManager::DisplayOverallStatsCSV(ostream & os) const {
    for(int c = 0; c < _classes; ++c) {
        os << "results:" << c << ',' << _OverallStatsCSV() << endl;
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
                } else {
                    _flits_to_watch.insert(atoi(line.c_str()));
                }
            }
        }

    } else {
        Error("Unable to open flit watch file: " + filename);
    }
}

int TrafficManager::_GetNextPacketSize(int cl) const
{
    assert(cl >= 0 && cl < _classes);

    vector<int> const & psize = _packet_size[cl];
    int sizes = psize.size();

    if(sizes == 1) {
        return psize[0];
    }

    vector<int> const & prate = _packet_size_rate[cl];
    int max_val = _packet_size_max_val[cl];

    int pct = RandomInt(max_val);

    for(int i = 0; i < (sizes - 1); ++i) {
        int const limit = prate[i];
        if(limit > pct) {
            return psize[i];
        } else {
            pct -= limit;
        }
    }
    assert(prate.back() > pct);
    return psize.back();
}

double TrafficManager::_GetAveragePacketSize(int cl) const
{
    vector<int> const & psize = _packet_size[cl];
    int sizes = psize.size();
    if(sizes == 1) {
        return (double)psize[0];
    }
    vector<int> const & prate = _packet_size_rate[cl];
    int sum = 0;
    for(int i = 0; i < sizes; ++i) {
        sum += psize[i] * prate[i];
    }
    return (double)sum / (double)(_packet_size_max_val[cl] + 1);
}
