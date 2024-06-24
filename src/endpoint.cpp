#include "endpoint.hpp"

#include <sstream>
#include <limits>
#include <math.h>

#include "random_utils.hpp"
#include "outputset.hpp"

EndPoint::EndPoint(Configuration const & config, TrafficManager * parent,
                   const string & name, int nodeid):
  TimedModule( parent, name ), _nodeid( nodeid ), _parent( parent ) {

  vector<string> debug_endpoint_vec = config.GetStrArray("debug_endpoint");
  if (debug_endpoint_vec.empty()) {
    string debug_endpoint = config.GetStr("debug_endpoint");
    int debug_int;
    std::istringstream(debug_endpoint) >> debug_int;
    if (!debug_endpoint.empty() && (debug_int == _nodeid)) {
      _debug_enabled = true;
    }
  } else {
    // Iterate through all names in the list to see if this one matches
    for (vector<string>::const_iterator iter = debug_endpoint_vec.begin();
         iter != debug_endpoint_vec.end();
         ++iter) {
      int debug_int;
      std::istringstream(*iter) >> debug_int;
      if (debug_int == _nodeid) {
        _debug_enabled = true;
      }
    }
  }

  vector<string> host_congestion_str_vec = config.GetStrArray("host_congestion_active");
  { // Check if this host is in congestion mode
    std::string s = config.GetStr("host_congestion_active");
    if (s.length()){
      _mypolicy_endpoint.host_congestion_enabled = false;
      for (string token : host_congestion_str_vec) {
          int this_host = stoi(token);
          if (this_host == _nodeid){
            _mypolicy_endpoint.host_congestion_enabled = true;
            break;
          }
      }
    } else {
      // By default al endpoints experinece host congestion
    }
  }

  _trace_debug = config.GetInt("trace_debug") != 0;

  flit_type_strings.resize(Flit::NUM_TYPES);
  flit_type_strings[Flit::READ_REQUEST]     = "READ_REQUEST";
  flit_type_strings[Flit::READ_REPLY]       = "READ_REPLY";
  flit_type_strings[Flit::WRITE_REQUEST]    = "WRITE_REQUEST";
  flit_type_strings[Flit::WRITE_REPLY]      = "WRITE_REPLY";
  flit_type_strings[Flit::ANY_TYPE]         = "ANY_TYPE";
  flit_type_strings[Flit::CTRL_TYPE]        = "CTRL_TYPE";
  flit_type_strings[Flit::RGET_REQUEST]     = "RGET_REQUEST";
  flit_type_strings[Flit::RGET_GET_REQUEST] = "RGET_GET_REQUEST";
  flit_type_strings[Flit::RGET_GET_REPLY]   = "RGET_GET_REPLY";
  flit_type_strings[Flit::WRITE_REQUEST_NOOP]    = "WRITE_REQUEST_NOOP";

  retry_mode_strings = {"IDLE", "NACKED", "SACKED", "TIMEOUT"};


  _cur_time = 0;
  _endpoints   = _parent->_nodes;
  _subnets = _parent->_subnets;
  _classes = _parent->_classes;
  _buf_states.resize(_subnets);
  _num_injection_queues = _endpoints;
  _inj_buf_rr_idx = 0;
  _num_response_queues = _endpoints;
  _rsp_buf_rr_idx = 0;
  _num_rget_get_req_queues = _endpoints;
  _rget_get_req_buf_rr_idx = 0;
  _num_tx_queues = _num_injection_queues + _num_response_queues + _num_rget_get_req_queues;
  _tx_queue_type_rr_selector = NEW_CMD_Q;
  _tx_arb_mode = ROUND_ROBIN;
  const string tx_arb_str = config.GetStr("endpoint_tx_arb_type");
  if (tx_arb_str == "weighted") {
    _tx_arb_mode = WEIGHTED;
  }
  _weighted_sched_req_init_tokens = config.GetInt("weighted_sched_req_tokens");
  _weighted_sched_rsp_init_tokens = config.GetInt("weighted_sched_rsp_tokens");
  _weighted_sched_incr_tokens = config.GetInt("weighted_sched_incr_tokens");
  _weighted_sched_rsp_slots_per_req_slot = config.GetInt("weighted_sched_rsp_incr_mult");
  _debug_ws = false;

  // Init tokens for queues
  _weighted_sched_queue_tokens.resize(NUM_QUEUE_TYPES);
  _weighted_sched_queue_tokens[NEW_CMD_Q].resize(_num_injection_queues, _weighted_sched_req_init_tokens);
  _weighted_sched_queue_tokens[READ_REPLY_Q].resize(_num_response_queues, _weighted_sched_rsp_init_tokens);
  _weighted_sched_queue_tokens[RGET_GET_REQ_Q].resize(_num_rget_get_req_queues, _weighted_sched_req_init_tokens);


  _injection_buffer.resize(_classes);
  _full_packets_in_inj_buf.resize(_classes);
  for (int traffic_class = 0; traffic_class < _classes; traffic_class++) {
    _injection_buffer[traffic_class].resize(_num_injection_queues);
    _full_packets_in_inj_buf[traffic_class].resize(_num_injection_queues, 0);
  }
  _generated_packets.resize(_classes, 0);
  _generated_flits.resize(_classes, 0);
  _injected_flits.resize(_classes, 0);
  _sent_flits.resize(_classes, 0);
  _sent_packets.resize(_classes, 0);
  _new_sent_flits.resize(_classes, 0);
  _new_sent_packets.resize(_classes, 0);
  _received_flits.resize(_classes, 0);
  _received_packets.resize(_classes, 0);
  _qtime.resize(_classes, 0);
  _qdrained.resize(_classes, false);
  _last_class.resize(_subnets,0);
  _last_vc.resize(_subnets);
  _incoming_flit_queue.resize(_subnets);
  for (int subnet = 0; subnet < _subnets; ++subnet) {
    // FIXME: Add buf_states creation
    ostringstream tmp_name;
    tmp_name << "terminal_buf_state_" << _nodeid << "_" << subnet;
    BufferState * bs = new BufferState( config, this, tmp_name.str( ) );
    int vc_alloc_delay = config.GetInt("vc_alloc_delay");
    int sw_alloc_delay = config.GetInt("sw_alloc_delay");
    int router_latency = config.GetInt("routing_delay") + (config.GetInt("speculative") ? max(vc_alloc_delay, sw_alloc_delay) : (vc_alloc_delay + sw_alloc_delay));
    int min_latency = 1 + _parent->_net[subnet]->GetInject(_nodeid)->GetLatency() + router_latency + _parent->_net[subnet]->GetInjectCred(_nodeid)->GetLatency();
    bs->SetMinLatency(min_latency);
    _buf_states[subnet] = bs;

    _last_vc[subnet].resize(_classes, -1);

  }

  _incoming_packet_src = -1;
  _incoming_packet_pid = -1;
  _incoming_packet_seq = -1;
  _incoming_packet_flit_countdown = -1;

  _cycles_before_standalone_ack = config.GetInt("cycles_before_standalone_ack");
  _cycles_before_standalone_ack_has_priority = _cycles_before_standalone_ack  * 2;
  _packets_before_standalone_ack = config.GetInt("packets_before_standalone_ack");
  _sack_enabled = config.GetInt("enable_sack");
  _sack_vec_length = config.GetInt("sack_vec_length");
  _sack_vec_mask = 0xffffffffffffffff >> (64 - _sack_vec_length);
  if (_sack_vec_length > 64) {
    cout << _cur_time << ": " << Name() << ": ERROR: sack_vec_length was configured to be "
         << _sack_vec_length << ", but max size is 64." << endl;
    exit(1);
  }
  _debug_sack = config.GetInt("debug_sack");
  _max_receivable_pkts_after_drop = config.GetInt("max_receivable_pkts_after_drop");
  _opb_max_pkt_occupancy = config.GetInt("opb_max_pkt_occupancy");
  _opb_pkt_occupancy = 0;
  _opb_ways = config.GetInt("opb_ways");
  _opb_dest_idx_mask = (1UL << config.GetInt("opb_dest_idx_bits")) - 1;
  _opb_seq_num_bits  = config.GetInt("opb_seq_num_idx_bits");
  _opb_seq_num_idx_mask = (1UL << _opb_seq_num_bits) - 1;
  _inj_buf_depth = config.GetInt("inj_buf_depth");
/*
  // Only use nodeid 0 to check for config errors
  if (_nodeid == 0) {
    // The total size of the OPB should match the indicies * ways.
    unsigned int index_bits = (_opb_seq_num_bits + config.GetInt("opb_dest_idx_bits"));
    if (((1UL << index_bits) * _opb_ways) != _opb_max_pkt_occupancy) {
      cout << "ERROR: Inconsistent OPB configuration!  OPB max occupancy specified as: " << _opb_max_pkt_occupancy
           << ".  Seq bits: " << _opb_seq_num_bits << ", dest bits: " << config.GetInt("opb_dest_idx_bits")
           << ", num ways: " << _opb_ways << "   => " << (1UL << index_bits) * _opb_ways << endl;
      exit(1);
    }
  }
*/

  int flit_size = 32; // in bytes
  _retry_timer_timeout = config.GetInt("retry_timer_timeout");

  if (_mypolicy_constant.policy == HC_HOMA_POLICY)
    _retry_timer_timeout = _estimate_round_trip_cycle * 3;

  _max_retry_attempts = config.GetInt("max_retry_attempts");
  _response_timer_timeout = config.GetInt("response_timer_timeout");
  _rget_req_pull_timeout = config.GetInt("rget_req_pull_timeout");
  _xaction_limit_per_dest = config.GetInt("endpoint_xaction_limit_per_dest");
  _global_get_request_limit = config.GetInt("endpoint_global_get_limit");
  _get_limit_per_dest = config.GetInt("endpoint_get_limit_per_dest");
  _rget_req_limit_per_dest = config.GetInt("endpoint_rget_req_limit_per_dest");
  _xaction_size_limit_per_dest = int(config.GetInt("endpoint_xaction_size_limit_per_dest_in_kb") * 1000 / gFlitSize);
  _get_inbound_size_limit_per_dest = int(config.GetInt("endpoint_get_inbound_size_limit_per_dest_in_kb") * 1000 / gFlitSize);
  _rget_inbound_size_limit_per_dest = int(config.GetInt("endpoint_rget_inbound_size_limit_per_dest_in_kb") * 1000 / gFlitSize);
  //_rget_inbound_size_limit_per_dest = config.GetInt("endpoint_rget_inbound_size_limit_per_dest");

  //_global_get_req_size_limit = config.GetInt("endpoint_global_get_req_size_limit");
  _global_get_req_size_limit = int(config.GetInt("endpoint_global_get_req_size_limit_in_kb") * 1000 / gFlitSize);

  _use_new_rget_metering = config.GetInt("endpoint_use_new_rget_metering");

  _ack_processing_latency = config.GetInt("ack_processing_latency");
  _rsp_processing_latency = config.GetInt("rsp_processing_latency");
  _req_processing_latency = config.GetInt("req_processing_latency");
  _rget_processing_latency = config.GetInt("rget_processing_latency");
  _put_to_rget_conversion_rate = config.GetFloat("put_to_rget_conversion_rate");
  _rget_convert_unacked_perc = config.GetFloat("rget_convert_unacked_perc");
  _rget_revert_acked_perc = config.GetFloat("rget_revert_acked_perc");
  _rget_convert_min_data_before_convert = config.GetInt("rget_convert_min_data_before_convert");
  _rget_min_samples_since_last_transition = config.GetInt("rget_min_samples_since_last_transition");

  _put_to_noop = false; // For now: true = convert all write requests to write noop

  _packet_gen_attempts = config.GetInt("packet_gen_attempts");  // Simulator artifact, not real HW
  _opb_insertion_conflicts = 0;
  _req_inj_blocked_on_xaction_limit = 0;
  _req_inj_blocked_on_size_limit = 0;
  _req_inj_blocked_on_ws_tokens = 0;
  _read_req_inj_blocked_on_xaction_limit = 0;
  _read_req_inj_blocked_on_size_limit = 0;
  _resp_inj_blocked_on_xaction_limit = 0;
  _resp_inj_blocked_on_size_limit = 0;
  _resp_inj_blocked_on_ws_tokens = 0;
  _rget_req_inj_blocked_on_xaction_limit = 0;
  _rget_req_inj_blocked_on_rget_req_limit = 0;
  _rget_req_inj_blocked_on_size_limit = 0;
  _rget_req_inj_blocked_on_inbound_data_limit = 0;
  _rget_get_req_inj_blocked_on_get_limit = 0;
  _rget_get_req_inj_blocked_on_inbound_data_limit = 0;
  _rget_get_req_inj_blocked_on_ws_tokens = 0;
  _rget_get_req_inj_blocked_on_global_request_limit = 0;
  _rget_get_req_inj_blocked_on_global_get_data_limit = 0;
  _get_req_inj_blocked_on_global_request_limit = 0;
  _get_req_inj_blocked_on_global_get_data_limit = 0;

  _packets_retired = 0;
  _packets_retired_full_sim = 0;
  _flits_retired = 0;
  _flits_retired_full_sim = 0;
  _data_flits_retired = 0;
  _data_flits_retired_full_sim = 0;
  _good_packets_received = 0;
  _good_packets_write_received = 0;
  _good_flits_received = 0;
  _good_data_flits_received = 0;
  _good_packets_received_full_sim = 0;
  _good_flits_received_full_sim = 0;
  _good_data_flits_received_full_sim = 0;
  _duplicate_packets_received = 0;
  _duplicate_flits_received = 0;
  _duplicate_packets_received_full_sim = 0;
  _duplicate_flits_received_full_sim = 0;
  _bad_packets_received = 0;
  _bad_flits_received = 0;
  _bad_packets_received_full_sim = 0;
  _bad_flits_received_full_sim = 0;
  _flits_dropped_for_rget_conversion = 0;

  _use_crediting = config.GetInt("use_endpoint_crediting");

  _new_packet_transmission_in_progress = NULL_Q;
  _min_packet_processing_penalty = config.GetInt("packet_processing_penalty");
  _next_packet_injection_blocked_until = 0;
  _num_flits_waiting_to_inject = 0;
  _max_flits_waiting_to_inject = config.GetInt("max_flits_waiting_to_inject");


  _repliesPending.resize(_num_response_queues);
  _rget_get_req_queues.resize(_num_rget_get_req_queues);
  _outstanding_global_get_req_inbound_data = 0;
  _outstanding_global_get_requests = 0;
  _enable_adaptive_rget = config.GetInt("enable_adaptive_rget");
  _rget_convert_sample_period = config.GetInt("rget_convert_sample_period");
  _rget_convert_num_samples = config.GetInt("rget_convert_num_samples");
  if (_rget_convert_num_samples > 2) {
    cout << "endpoint.cpp: Configuration error: rget_convert_num_samples = " << _rget_convert_num_samples
         << ".  The decide_on_put_to_rget_conversion function only supports a value of 2." << endl;
    exit(1);
  }

  _mypolicy_constant.ack_queue_preemptive_insertion_enabled = true;
  _mypolicy_constant.load_balance_queue_enabled = true;
  _mypolicy_constant.speculative_ack_enabled = true;

  _put_buffer_meta.queue_size = config.GetInt("put_wait_buf_size"),
  _put_buffer_meta.load_balance_queue_size = config.GetInt("load_balance_buf_size");

  if (_mypolicy_constant.load_balance_queue_enabled &&
      _mypolicy_constant.policy == HC_MY_POLICY
      ){
    _put_buffer_meta.queue_size -= _put_buffer_meta.load_balance_queue_size;
    assert(_put_buffer_meta.queue_size > 0);
  }
  _put_buffer_meta.remaining = _put_buffer_meta.queue_size;
  _put_buffer_meta.load_balance_queue_size = config.GetInt("load_balance_buf_size"),
  _put_buffer_meta.load_balance_queue_remaining = _put_buffer_meta.load_balance_queue_size;

  _put_buffer_meta.header_latency = config.GetInt("put_latency_header");
  _put_buffer_meta.header_flit_num = config.GetInt("put_header_flit");
  _put_buffer_meta.normal_length = config.GetInt("put_latency_normal_length");
  _put_buffer_meta.slow_length = config.GetInt("put_latency_slow_length");
  _put_buffer_meta.slow = false;
  _put_buffer_meta.latency_length_remaining = _put_buffer_meta.normal_length;
  _put_buffer_meta.left_over_cycle_saving_from_previous_packet = 0.0;

  _put_buffer_meta.next_bursty_time = 0;
  _put_buffer_meta.packet_dropped = 0;
  _put_buffer_meta.packet_dropped_full = 0;

  _mypolicy_constant.policy = config.GetInt("host_control_policy");
  if (_mypolicy_constant.policy == HC_MY_POLICY) {
    _mypolicy_constant.delayed_ack_threshold =
        config.GetInt("mypolicy_delayed_ack_threshold");
    _mypolicy_constant.NACK_reservation_size =
        config.GetInt("mypoicy_NACK_reservation_size");
    _mypolicy_constant.max_packet_send_per_ack =
        config.GetInt("host_control_max_packet_send_per_ack");
    _mypolicy_constant.max_ack_before_send_packet =
      -config.GetInt("host_control_max_ack_before_send_packet");
    _mypolicy_constant.time_before_halt_state_timeout = config.GetInt("host_control_timeout");
    _mypolicy_constant.suppress_duplicate_ack_max =
      config.GetInt("suppress_duplicate_acks_between_n_incremental_acks");
    _mypolicy_constant.fairness_sampling_time =
      config.GetInt("host_control_fairness_sampling_period");
    _mypolicy_constant.fairness_reset_period =
      config.GetInt("host_control_fairness_reset_period");
    _mypolicy_constant.fairness_diff_threshold =
      config.GetFloat("host_control_fairness_diff_threshold");
    _mypolicy_constant.coalescing_timeout =
      config.GetInt("host_control_coalescing_timeout");
    _mypolicy_constant.fairness_packet_count_threshold =
      config.GetInt("host_control_fairness_packet_count_threshold");

    _mypolicy_constant.time_before_shared_ack_timeout = config.GetInt("shared_ack_timeout");
    _mypolicy_constant.speculative_ack_queue_size =
      config.GetInt("speculative_ack_queue_size");

    _mypolicy_endpoint.max_ratio_valid = false;
    _mypolicy_endpoint.suppress_request = false;
    _mypolicy_endpoint.reserved_space = 0;
    _mypolicy_endpoint.total_packet_occupy = 0;
    _mypolicy_endpoint.next_fairness_request_time = _mypolicy_constant.fairness_sampling_time;
    _mypolicy_endpoint.next_fairness_reset_time = _mypolicy_constant.fairness_reset_period;

    assert(_mypolicy_constant.max_ack_before_send_packet < 0);
  } else if (_mypolicy_constant.policy == HC_TCP_LIKE_POLICY || _mypolicy_constant.policy == HC_ECN_POLICY) {
    _mypolicy_constant.tcplikepolicy_MSS =
        config.GetInt("host_control_tcplikepolicy_MSS");

    _mypolicy_constant.ecn_period = config.GetInt("ecn_period");
    _mypolicy_constant.ecn_param_g = config.GetFloat("ecn_param_g");

    _mypolicy_constant.ecn_threshold_percent = config.GetFloat("ecn_threshold_percent");
    _mypolicy_constant.ecn_threshold = (int) (_put_buffer_meta.load_balance_queue_size *
        _mypolicy_constant.ecn_threshold_percent);
  }

  _mypolicy_endpoint.acked_data_in_queue = 0;
  _mypolicy_endpoint.data_dequeued_but_need_acked = 0;
  _mypolicy_endpoint.latency = new Stats( this, "latency", 1.0, 1000 );
  _mypolicy_endpoint.latency->Clear();
  _mypolicy_endpoint.latency->_need_percentile = true;

  // Gbps to flit / Cycles
  //
  _mypolicy_constant.host_bandwidth_high =
      config.GetFloat("host_bandwidth_gbps") * 2.5 / (flit_size * 8);
  _mypolicy_constant.host_bandwidth_low =
      config.GetFloat("host_bandwidth_gbps_low") * 2.5 / (flit_size * 8);
  _mypolicy_endpoint.current_host_bandwith = _mypolicy_constant.host_bandwidth_high;

  double mean = config.GetFloat("inter_host_bandwidth_change_mean");
  double variance = config.GetFloat("inter_host_bandwidth_change_variance");

  _mypolicy_endpoint.generator.seed(123 + _nodeid);
  _mypolicy_endpoint.interarrival_logn = new std::lognormal_distribution<double>(log(mean) - variance / 2, sqrt(variance));

  _mypolicy_endpoint.current_host_bandwith = _mypolicy_constant.host_bandwidth_high;
  _mypolicy_endpoint.next_change_bandwidth_time = (*_mypolicy_endpoint.interarrival_logn)(_mypolicy_endpoint.generator);
  _mypolicy_endpoint.host_bandwidth_is_slow = false;
  _mypolicy_endpoint.ecn_next_check_period = 0;


  for (unsigned int target = 0; target < _endpoints; target++) {
    // Transmitted sequence numbers start with 1.
    _packet_seq_num[target] = 1;
    _recvd_seq_num_vec[target] = 0;

    // For host congestion policy
    _mypolicy_connections[target] = {
            .periodic_buffer_occupancy = 0,
            .periodic_ack_occupancy = 0,
            .highest_bad_seq_num_from_initiator = 0,
            .initiator_retransmitting = false,
            .put_drop_counter = 0,
            .last_index_in_ack_queue = -1,
            .space_after_NACK_reserved = false,
            .speculative_ack_allowance_size = 0,
            .earliest_accum_ack_shared_time = -1,
            .ecn_count = 0,
            .ecn_total = 0,
            .ecn_running_percent = 0.0,
            .halt_active = false,
            .halt_state = -9999,
            .send_allowance_counter_size = 0,
            .must_retry_at_least_one_packet = false,
            .time_last_ack_recvd = 999999999,
            .last_valid_ack_seq_num_recvd = 0,
            .pending_nack_seq_num = -1,
            .tcplikepolicy_cwd = _mypolicy_constant.tcplikepolicy_MSS,
            .tcplikepolicy_ssthresh = _xaction_size_limit_per_dest,
    };

    // For tracking ACKs/NACKs that this endpoint needs to send out as a result
    // of packets received.
    _ack_response_state[target] = ack_response_record{
      // Start with 0, so the first received seq_num of 1 looks good.
                                       .last_valid_seq_num_recvd = 0,
                                       .last_valid_seq_num_recvd_and_ackd = 0,
                                       .last_valid_seq_num_recvd_and_ready_to_ack = 0,
// Change to something like: time_of_receipt_of_oldest_unacked_packet
                                       .time_last_valid_unacked_packet_recvd = 999999999,
                                       .packets_recvd_since_last_ack = 0,
                                       .outstanding_ack_type_to_return = ACK,
                                       .already_nacked_bad_seq_num = false,
                                       .time_last_valid_packet_recvd = 0,
                                       .time_last_ack_sent = 0,
                                       .sack_vec = 0
                                  };

    dest_retry_record d = dest_retry_record();
    d.dest_retry_state = IDLE;
    d.nack_replay_opb_flit_index = -1;
    d.pending_ack = -1;
    d.sack = false;

    _retry_state_tracker[target] = d;

    _timedout_packet_retransmit_in_progress = {-1, -1};
    _outstanding_xactions_per_dest[target] = 0;
    _outstanding_put_data_per_dest[target] = 0;
    _new_write_ack_data_per_dest[target] = 0;
    _periods_since_last_transition[target] = 0;
    _outstanding_gets_per_dest[target] = 0;
    _converting_puts_to_rgets[target] = false;
    _outstanding_rget_reqs_per_dest[target] = 0;
    _outstanding_outbound_data_per_dest[target] = 0;
    _outstanding_inbound_data_per_dest[target] = 0;
    _outstanding_rget_inbound_data_per_dest[target] = 0;

    _outstanding_put_data_samples_per_dest[target].resize(_rget_convert_num_samples, 0);
    _new_write_ack_data_samples_per_dest[target].resize(_rget_convert_num_samples-1, 0);
  }

  // Stats
  _cycles_generation_not_attempted = 0;
  _cycles_gen_attempted_but_blocked = 0;
  _cycles_inj_present_but_blocked = 0;
  _cycles_new_flit_not_injected = 0;
  _cycles_link_avail_no_new_flits = 0;
  _cycles_retransmitting = 0;
  _packets_retransmitted = 0;
  _packets_retransmitted_full_sim = 0;
  _flits_retransmitted_full_sim = 0;
  _max_packet_retries_full_sim = 0;
  _cycles_inj_all_blocked_on_timeout = 0;
  _max_outstanding_xactions_per_dest_stat = 0;
  _outstanding_xactions_all_dests_stat = 0;
  _max_outstanding_xactions_all_dests_stat = 0;
  _max_total_outstanding_data_per_dest_stat = 0;
  _outstanding_outbound_data_all_dests_stat = 0;
  _max_total_outstanding_outbound_data_all_dests_stat = 0;
  _nacks_sent = 0;
  _nacks_received = 0;
  _sacks_received = 0;
  _sacks_sent = 0;
  _retry_timeouts = 0;
  _puts_converted_to_rgets = 0;
}

EndPoint::~EndPoint() {
  for (int subnet = 0; subnet < _subnets; ++subnet) {
    delete _buf_states[subnet];
  }
}


//  NOTE: TrafficManager calls the major endpoint functions in this order every cycle:
//    _ReceiveFlit
//    _ReceiveCredit
//    _EvaluateNewPacketInjection
//    _Step
//    _ProcessReceivedFlits


//void EndPoint::_Inject() {
void EndPoint::_EvaluateNewPacketInjection() {
  // Depending on the state of the endpoint, determine whether we want to
  // attempt to generate/inject a new packet.

  // We have to block on OPB occupancy here to prevent full packets from being
  // pushed into the injection buffer.  If we try to block after this, we get
  // errors where a packet is in the process of being sent, and another will
  // try to use the output VC.
// FIXME: DO WE STILL NEED THIS???
  if (_opb_pkt_occupancy >= _opb_max_pkt_occupancy) {
    return;
  }

  unsigned int gen_attempts_but_blocked = 0;
  unsigned int attempted_generation_count = 0;
  bool final_generated = false;
  for ( int c = 0; c < _classes; ++c ) {
    // Potentially generate packets for any (input,class)
    // that is currently empty
    bool generated = false;

    // _qtime is not advanced when there is something already in the injection
    // buffer that was generated in a prior cycle but was unable to be
    // inserted into the network.  Each time this happens, _qtime falls
    // further behind the current cycle time.  Once the packet is inserted
    // into the network, we try to make up for lost time by calling
    // DecideWhetherToGeneratePacket for every cycle that was missed.  This
    // loop gives the endpoint a chance to generate a packet for every one of
    // those missed cycles, just later in time.  This makes sense under the
    // assumption that all endpoints need to do roughly the same amount of
    // work.  Note that the call may not result in a packet being generated,
    // in which case, _qtime is still advanced.  (The important thing is that
    // the endpoint got the opportunity to generate a new packet for that
    // cycle.  So if a given endpoint was blocked by other traffic, it will
    // "catch up" at the end.)
    // However, since we only increment _qtime by 1, it will never catch up
    // to the current time.  So how does the simulation stop?
    // It all comes down to _InjectionsOutstanding in the traffic manager.
    //   _SingleSim() in the "drain" stage...
    // Once _InjectionsOutstanding returns false, _SingleSim ends and true
    // draining (disabling any new packet generation) happens in the Run
    // function.  (Ie. this function will no longer be called.)
    // So what causes _InjectionsOutstanding to return false?
    // _measured_in_flight_flits is empty, and _qdrained for each node is
    // true.  _qdrained is set to true in this function after the endpoint's
    // _qtime has *caught up to the time when the drain phase began*.  (So
    // this endpoint has had a chance to "catch up" and inject all of its
    // packets that it could otherwise have injected before drain started.)
    //
    // But how does _measured_in_flight_flits ever get cleared?
    // We keep inserting into it every time we generate a new message, or at
    // least when "record" is true.  "record" is set to false during the drain
    // stage.

    // When a packet is generated, it will use _qtime as its creation time to
    // determine the total packet latency (if _include_queueing is enabled).
    while( !generated && ( _qtime[c] <= _parent->_time ) ) {
      // This node is enabled to generate/inject a packet, so now run the
      // (random) injection process to determine whether or not we will
      // actually generate a packet.
      int stype = DecideWhetherToGeneratePacket( c );

      // stype encodings:
      //  -1: only use_read_write, indicates a reply is ready to send, generates READ_REPLY or WRITE_REPLY
      //   0: both, do not generate a packet
      //   1: !use_read_write: generate a packet of type ANY_TYPE
      //      use_read_write: generate a packet of type READ_REQUEST
      //   2: only use_read_write, generate a packet of type WRITE_REQUEST or WRITE_REQUEST_NOOP (flag)

      // The injection process has determined that we will generate a
      // packet, so attempt to do so here.  (It may not succeed depending on
      // the checks performed in GeneratePacket.)

      // The original implementation would not advance the qtime if the
      // injection buffer was not empty (and also wouldn't try to generate
      // a new packet).  The equivalent would be if we wanted to generate a
      // packet here, but GeneratePacket couldn't make it happen.  (Since
      // that is where the check against the per-dest injection queues
      // happens.  So to preserve this behavior, capture the original stype
      // as attempt_generation, and use it in the qtime increment condition
      // below.
      bool attempt_generation = (stype != 0);
      if (attempt_generation) {
        // GeneratePacket will overwrite stype.
        GeneratePacket( stype, c,
                        _parent->_include_queuing==1 ?
                        _qtime[c] : _parent->_time );
        if (stype != 0) {
          generated = true;
          final_generated = true;
        } else {
          gen_attempts_but_blocked++;
        }

        attempted_generation_count++;

      // If the coin flip didn't generate, but the intended rate is 1.0, and all buffers are empty,
      // then generate anyway.
      } else if ((_parent->_intended_load[0][_nodeid] == (double)1.0) &&
          (_parent->_sim_state == TrafficManager::running) &&
                 InjectionBuffersEmpty(0) && PendingRepliesDrained() && !gSwm) {

        //stype = DecideWhetherToGeneratePacket( c, true );  // Force generation
        stype = DecideWhetherToGeneratePacket( c, !gSwm );  // Force generation
        if (!gSwm || stype)
            GeneratePacket( stype, c,
                            _parent->_include_queuing==1 ?
                            _qtime[c] : _parent->_time );
        if (stype == 0 && !gSwm) {
          cout << _cur_time << ": " << Name() << ": Tried to force generation, but still failed." << endl;
        } else {
//          cout << _cur_time << ": " << Name() << ": Forced generation." << endl;
          generated = true;
          final_generated = true;
        }
      }


      // Advance _qtime when:
      // 1. The injection process was given a chance to generate, but declined.
      // 2. A new packet *was* generated, having found an available injection
      //    buffer slot, and the message generated was not a reply.
      // We do not advance qtime when we tried to generate a packet, but could
      // not due to injection buffer queueing.
      //   But if that keeps happening, we can never get out of this loop...
      //   And we want to keep the concept of qtime so we can "catch up" later..
      //   So instead of incrementing _qtime in this case, we'll just set
      //   generated to true to exit out of the loop, even though we didn't
      //   really generate a packet.

      // We took our shot and decided not to generate.
      if (!attempt_generation) {
        ++_qtime[c];
      }

      // We successfully generated a packet, and it was not a response.
      else if (generated && (!_parent->_use_read_write[c] || (stype > 0))) {
        ++_qtime[c];
      }


      // We decided to generate, but couldn't due to full injection buffers.
      // Do not advance qtime in this case, which gives us a chance to generate
      // in a later cycle.  But set generated to true so we can bail out of the
      // loop.

      // The functional difference here is that this will cause us to bail out
      // of the loop, whereas before, we would keep looping.... how would we ever
      // get out of the loop if we couldn't find a packet???
      // We could only get out when GeneratePacket decided not to generate and
      // incremented _qtime beyond the current time.
      // Confirmed: Without this exit condition, with an injection rate of 1 and
      // a packet size 1, then we do encounter cases where it loops forever.
      // This wasn't seen previously because we weren't running cases with a
      // packet size of 1.  It also wasn't a problem at all before we added the
      // COPA-specific generation blocks.  (In the original booksim code, it
      // only entered the loop if there was room in the (single)
      // _injection_buffer.)
      else if (attempt_generation && !generated) {
        generated = true;
      }
    }


    // One the endpoint has injected enough to make up for lost opportunities
    // (when the coin flip determined that we should generate a new packet, but
    // it was blocked from doing so) during the warmup and running phase, we can
    // stop further generation and begin "real" draining.  This happens when
    // _qtime > _drain_time.
    // As an example, in most sims, the warmup+running time is about 60k cycles.
    // If the injection rate is 0.1, we would expect to have generated about 6k
    // flits. But if we were blocked from injecting, we want to allow the
    // endpoint to finish injecting the rest during the drain phase.  This makes
    // sense because we assume each endpoint is running similar workloads that
    // have a fixed amount of transactions to send.
    if ( ( _parent->_sim_state == TrafficManager::draining ) &&
         ( _qtime[c] > _parent->_drain_time ) ) {
      _qdrained[c] = true;
    }
  }


  if ((attempted_generation_count == 0) && (_parent->_sim_state == TrafficManager::running)) {
    _cycles_generation_not_attempted++;
  }

  if ((!final_generated) && (gen_attempts_but_blocked > 0) && (_parent->_sim_state == TrafficManager::running)) {
    _cycles_gen_attempted_but_blocked++;
  }
}

int EndPoint::DecideWhetherToGeneratePacket( int cl, bool force_gen ) {
  int result = 0;
  assert(!force_gen); // only for swm
  if (_parent->_use_read_write[cl]) { //use read and write
    //produce a packet
    if (force_gen || (_parent->_injection_process[cl]->test(_nodeid))) {

      if (gSwm){
        if (WorkloadMessagePtr m = _parent->_injection_process[cl]->get(_nodeid)){
          // If we have a valid non-nil, non dummy message
          bool is_reply = m->IsReply();
          result = is_reply ? -1 : m->IsRead() ? 1 : 2;
          assert(m->Source() == _nodeid);
        }
      } else {
        // Non swm
        //coin toss to determine request type.
        result = (RandomFloat() < _parent->_write_fraction[cl]) ? 2 : 1;
      }
    }
  } else { //normal mode
    result = _parent->_injection_process[cl]->test(_nodeid) ? 1 : force_gen;
  }
  return result;
}


// Generated packets are pushed onto the _injection_buffer list
void EndPoint::GeneratePacket( int & stype, int cl, int64_t time) {
    assert(stype!=0);

    Flit::FlitType packet_type = Flit::ANY_TYPE;
    int packet_dest = -1;
    int size = -1;
    bool record = false;
    int req_seq_num = -1;
/*
if (_debug_enabled) {
  cout << _cur_time << ": dest: " << packet_dest << ": out_xactions: " << _outstanding_xactions_per_dest[packet_dest]
       << ", limit: " << _xaction_limit_per_dest
       << ", out_size: " << _outstanding_outbound_data_per_dest[packet_dest]
       << ", limit: " << _xaction_size_limit_per_dest << endl;
}
*/
    WorkloadMessagePtr pdata = 0;

    unsigned int requested_data_transfer_size = 0;
    // first see if injector supplies the destination and other packet info,
    // but not if it's a reply since replies are handled by legacy code further below

    // If we couldn't find a reply to insert into an inject buffer, try to
    // generate new requests.
    if (stype != -1) {
      packet_type = Flit::ANY_TYPE;
      // This will only generate valid dests (as determined by traffic type, eg. hotspot).
      if (gSwm){
        if (stype > 0 && (pdata = _parent->_injection_process[cl]->get(_nodeid)).get() != NULL) {
            size = pdata->Size();
            packet_dest = pdata->Dest();
            if (_parent->_use_read_write[cl]) {
                packet_type = pdata->IsRead() ? Flit::READ_REQUEST : Flit::WRITE_REQUEST;
            }
        }
        assert(packet_dest >= 0);
        if ((_full_packets_in_inj_buf[cl][packet_dest] >= _inj_buf_depth)){
          // Injection buffer is full halt
          stype = 0;
          return;
        }
        if (_trace_debug && packet_type == Flit::WRITE_REQUEST){
            _parent->debug_trace_file << _cur_time << ",BUFPACKETBUFF," <<
                _nodeid << "," << _full_packets_in_inj_buf[cl][packet_dest] << endl;
        }
        _parent->_injection_process[cl]->next(_nodeid); // Call next to continue packet injection
      } else {
        packet_dest = _parent->_traffic_pattern[cl]->dest(_nodeid);
        size = _parent->_GetNextPacketSize(cl); //input size

        unsigned int dest_gen_attempts = 0;
        // Only generate a packet for dests that have an injection buffer with
        // space for another packet.  Allow packet generation once the resident
        // packet has *begun* to transmit.  That way, with the actual injection
        // rate of (desired inject rate/packet size), we have plenty of
        // opportunities to generate a new packet while the previous one is being
        // transmitted.  This doesn't really have an effect with injection buffer
        // sizes greater than 1.

        // Allowing more packets to queue here (by increasing _inj_buf_depth)
        // causes the "packet latency" to go up, since it is measured starting
        // from creation time (ctime), but should not affect "network latency",
        // which is measured from injection time (itime).
        while ((_full_packets_in_inj_buf[cl][packet_dest] >= _inj_buf_depth) &&
               (dest_gen_attempts < _packet_gen_attempts)) {
          packet_dest = _parent->_traffic_pattern[cl]->dest(_nodeid);
          dest_gen_attempts++;
        }
        if (dest_gen_attempts >= _packet_gen_attempts) {
          stype = 0;
          return;
        }
      }
    }

    if(_parent->_use_read_write[cl]){
      // Requests
      if(stype > 0 && !gSwm) {
          if (stype == 1) {
              packet_type = Flit::READ_REQUEST;
              size = _parent->_read_request_size[cl];
              requested_data_transfer_size = _parent->_read_reply_size[cl];
          } else if (stype == 2) {
              packet_type = _put_to_noop? Flit::WRITE_REQUEST_NOOP: Flit::WRITE_REQUEST;
              size = _parent->_write_request_size[cl];
          } else {
              ostringstream err;
              err << "Invalid packet type: " << packet_type;
              Error( err.str( ) );
          }

      // Note, responses are no longer generated here since we now generate
      // and queue them immediately in UpdateAckAndReadResponseState.
      } else {
      }
    }

    if ((packet_dest < 0) || (packet_dest >= (int)_endpoints)) {
        ostringstream err;
        err << "Incorrect packet destination " << packet_dest
            << " for stype " << packet_type;
        Error( err.str( ) );
    }

    if ( ( _parent->_sim_state == TrafficManager::running ) ||
         ( ( _parent->_sim_state == TrafficManager::draining ) && ( time < _parent->_drain_time ) ) ) {
      record = _parent->_measure_stats[cl];
    }

    GeneratePacketFlits(packet_dest, packet_type, size, time, record, cl, req_seq_num,
                        requested_data_transfer_size,
                        pdata, &(_injection_buffer[cl][packet_dest]), 0);
    _full_packets_in_inj_buf[cl][packet_dest]++;

  return;
}


void EndPoint::GeneratePacketFlits(int dest, Flit::FlitType packet_type, int size, int time,
                                   bool record, int cl, int req_seq_num, int requested_data_transfer_size,
                                   WorkloadMessagePtr data,
                                   list< Flit * > * flit_list, bool watch_rsp) {
    // WARNING: Multi-thread mutex required
    int pid = _parent->_cur_pid++;
    assert(_parent->_cur_pid);

    int subnetwork = 0;

    bool watch = gWatchOut && (_parent->_packets_to_watch.count(pid) > 0);

    if ( watch ) {
        *gWatchOut << _cur_time << " | "
                   << Name() << " | "
                   << "Enqueuing packet " << pid
                   << " at time " << time
                   << " with dest " << dest
                   << ", type: " << packet_type
                   << ", seq_num: " << _packet_seq_num[dest] << "." << endl;
    }
    if ( watch_rsp ) {
        *gWatchOut << _cur_time << " | "
                   << Name() << " | "
                   << "Enqueuing RESPONSE packet " << pid
                   << " at time " << time
                   << " with dest " << dest
                   << ", type: " << packet_type
                   << ", seq_num: " << _packet_seq_num[dest] << "." << endl;
    }

    if (_debug_enabled && (req_seq_num != 0)) {


      cout << _cur_time << ": " << Name() << ": Generating " << flit_type_strings[packet_type]
           << " back to nodeid " << dest << " in response to req_seq_num: " << req_seq_num
           << " with packetid: " << pid << ", flitid: " << _parent->_cur_id << endl;
    }

    if (_trace_debug){
      //_parent->debug_trace_file << _cur_time << ",GENF," << _nodeid << "," << dest << "," << flit_type_strings[packet_type] <<
        //"," << _packet_seq_num[dest]  << "," << req_seq_num << "," << pid << "," << _parent->_cur_id << "," << size << endl;
    }

    for ( int i = 0; i < size; ++i ) {
        // This is the *original* packet creation.
        // Careful, though... for convenience, this is the copy of the flit that
        // is first injected into the network, *NOT* the OPB copy, as one might
        // expect.  The OPB copy is created in find_new_flit_to_inject, using
        // the local variable "opb_flit_copy".
        Flit * flit  = Flit::New();
        // WARNING: Mutex required when multi-threading is added
        flit->id     = _parent->_cur_id++;
        assert(_parent->_cur_id);
        flit->pid    = pid;
        flit->watch  = watch | (gWatchOut && (_parent->_flits_to_watch.count(flit->id) > 0));
        flit->subnetwork = subnetwork;
        flit->src    = _nodeid;
        flit->ctime  = time;
        flit->record = record;
        flit->cl     = cl;

        if (gSwm)
            assert(time == _cur_time);

        flit->response_to_seq_num = req_seq_num;
        if (packet_type == Flit::READ_REQUEST) {
          flit->read_requested_data_size = requested_data_transfer_size;
        } else if (packet_type == Flit::RGET_GET_REQUEST) {
          flit->read_requested_data_size = requested_data_transfer_size;
        } else {
          flit->read_requested_data_size = 0;
        }

        // WARNING: Mutex required when multi-threading is added
        // This structure is used by the TrafficManager to determine how long to
        // keep simulating.
        // It is erased by TM in _RetireFlit (called by _ReceiveFlit) when the
        // ACK is received).
        _parent->_total_in_flight_flits[flit->cl].insert(make_pair(flit->id, flit));
        if(record) {
          // Note: TM will not quiesce until _measured_in_flight_flits is empty.
          // Each of the flits registered here must be retired for the
          // simulation to end.  (This happens in the _ReceiveFlit function,
          // when the ACK is received.)
          _parent->_measured_in_flight_flits[flit->cl].insert(make_pair(flit->id, flit));
        }


        if(gTrace){
            cout << "New Flit " << flit->src <<endl;
        }
        flit->type = packet_type;

        if ( i == 0 ) { // Head flit
            flit->head = true;
            //packets are only generated to nodes smaller or equal to limit
            flit->dest = dest;
            flit->size = size;
            flit->data = data;
        } else {
            flit->head = false;
            // Temporarily set a body flit's dest field so we know where in the
            // OPB to put it.  Once that's done, we will set it to -1 to ensure
            // that we can't "cheat" later.
            flit->dest = dest;
            flit->debug_dest = dest;
            flit->size = size;
        }

        switch( _parent->_pri_type ) {
        case TrafficManager::class_based:
            flit->pri = _parent->_class_priority[cl];
            assert(flit->pri >= 0);
            break;
        case TrafficManager::age_based:
            flit->pri = numeric_limits<int>::max() - time;
            assert(flit->pri >= 0);
            break;
        default:
            flit->pri = 0;
        }
        if ( i == ( size - 1 ) ) { // Tail flit
            flit->tail = true;
            flit->data = data;
        } else {
            flit->tail = false;
        }

        flit->vc  = -1;

        if ( flit->watch ) {
            *gWatchOut << _cur_time << " | "
                       << Name() << " | "
                       << "Enqueuing flit " << flit->id
                       << " (packet " << flit->pid
                       << ", head: " << flit->head
                       << ", type: " << flit->type
                       << ") at time " << time
                       << " with dest: " << dest << "." << endl;
        }

//        _injection_buffer[cl][dest].push_back( flit );
        flit_list->push_back( flit );

    }

    if (_parent->_sim_state == TrafficManager::running) {
      _generated_packets[cl]++;
      _generated_flits[cl] += size;
    }
    ++_generated_packets_full_sim;
    _generated_flits_full_sim += size;
}


unsigned int EndPoint::get_opb_hash(int target, int seq_num) {
  return ((target & _opb_dest_idx_mask) << _opb_seq_num_bits) |
          (seq_num & _opb_seq_num_idx_mask);
}


bool EndPoint::check_for_opb_insertion_conflict(int target) {
  // Find combinations that alias to the same OPB set.
  // OPB index consists of:
  //   target[_opb_dest_idx_mask], seq_num[_opb_seq_num_bits]
  int seq_num = _packet_seq_num[target];

  unsigned int matching_entries =
    _opb_occupancy_map[get_opb_hash(target, seq_num)];

  // We should never find more matches than there are ways.
  if (matching_entries > _opb_ways) {
    cout << _cur_time << ": " << Name() << ": ERROR: On OPB insertion attempt, found more matches than ways: "
         << matching_entries << ", ways: " << _opb_ways << ", target: "
         << target << ", seq_num: " << seq_num << endl;
    exit(1);
  }

  if (matching_entries < _opb_ways) {
    return false;
  } else {
    _opb_insertion_conflicts++;
    return true;
  }
}

// The _Step function chooses a valid flit to inject from messages resident in
// the various buffers and passes it up to the trafficmanager to put the flit
// onto the network wires.
Flit * EndPoint::_Step(int subnet) {


  if(_cur_time == gLastClearStatTime) {
    printf("Displaying and clearing stats...%d\n", _cur_time);
    _parent->UpdateStats();
    _ClearStats( );
    assert (_parent->_sim_state == TrafficManager::running);
  }

  Flit * flit = NULL;

  if (_debug_enabled && ((_cur_time % 20000) == 0)) {
    DumpOPB();
    cout << _cur_time << ": " << Name() << ": Retry state: " << endl;
    for (unsigned int target = 0; target < _endpoints; target++) {
      cout << "    " << target << ": " << _retry_state_tracker[target].dest_retry_state << endl;
    }
    cout << _cur_time << ": " << Name() << ": Ack response state:   "
         << "last_valid_seq_num_recvd, time_last_valid_unacked_packet_recvd, "
         << "packets_recvd_since_last_ack, outstanding_ack_type_to_return, "
         << "already_nacked_bad_seq_num" << endl;
    for (unsigned int target = 0; target < _endpoints; target++) {
      cout << "    " << target << ": " << _ack_response_state[target].last_valid_seq_num_recvd
           << "," << _ack_response_state[target].time_last_valid_unacked_packet_recvd
           << "," << _ack_response_state[target].packets_recvd_since_last_ack
           << "," << _ack_response_state[target].outstanding_ack_type_to_return
           << "," << _ack_response_state[target].already_nacked_bad_seq_num << endl;
    }
  }

  // Updating state for adaptive RGET conversion.
  if ((_cur_time > 0) && ((_cur_time % _rget_convert_sample_period) == 0)) {
    for (unsigned int dest = 0; dest < _endpoints; dest++) {
      _outstanding_put_data_samples_per_dest[dest].push_front(_outstanding_put_data_per_dest[dest]);
      _outstanding_put_data_samples_per_dest[dest].pop_back();

      _new_write_ack_data_samples_per_dest[dest].push_front(_new_write_ack_data_per_dest[dest]);
      _new_write_ack_data_samples_per_dest[dest].pop_back();

      // Always reset the ack count for each new sample period.
      _new_write_ack_data_per_dest[dest] = 0;
      _periods_since_last_transition[dest]++;
    }

    if (_debug_enabled) {
      cout << _cur_time << ": " << Name() << ": Target: 0:  retry_mode: "
           << retry_mode_strings[_retry_state_tracker[0].dest_retry_state]
           << ", converting to RGET: " << _converting_puts_to_rgets[0]
           << ".  outs_data[0]: " << _outstanding_put_data_samples_per_dest[0][0]
           << ", outs_data[1]: "  << _outstanding_put_data_samples_per_dest[0][1]
           << ", acks[0]: "       << _new_write_ack_data_samples_per_dest[0][0] << endl;
    }
  }

  BufferState * const dest_buf = _buf_states[subnet];
  int const last_class = _last_class[subnet];
  int class_limit = _classes;
  bool new_flit = false;

  // If there is a packet in the process of transmitting, we have to let it
  // complete.  It can either be a new packet, a retry from a timeout, or a retry
  // from a NACK.
  // If none of these things are happening, then make sure we've waited until at
  // least _next_packet_injection_blocked_until before we considering injecting
  // another packet.
  if ((_new_packet_transmission_in_progress == NULL_Q) &&
      (_timedout_packet_retransmit_in_progress.seq_num == -1) &&
      (_pending_nack_replays.empty() ||
        (_retry_state_tracker[_pending_nack_replays.front()].dest_retry_state == IDLE)) &&

      ((_next_packet_injection_blocked_until > (unsigned int)_cur_time) ||
       (_num_flits_waiting_to_inject >= _max_flits_waiting_to_inject))) {

    // Nothing in-progress, and can't "inject" another packet into the staging
    // buffer.
    if (_parent->_sim_state == TrafficManager::running) {
      ++_cycles_new_flit_not_injected;
      if (_next_packet_injection_blocked_until > (unsigned int)_cur_time) {
        ++_cycles_new_flit_not_injected_due_to_packet_processing_penalty;
      }
      if (_num_flits_waiting_to_inject >= _max_flits_waiting_to_inject) {
        ++_cycles_new_flit_not_injected_due_to_staging_buffer_full;
      }
    }

  } else  {

    // Check to see if we need to retransmit any packets, but do not interrupt a
    // new packet currently transmitting.
    if (_new_packet_transmission_in_progress == NULL_Q) {
      flit = find_flit_to_retransmit(dest_buf);
      if (_debug_enabled && flit) {
        cout << _cur_time << ": " << Name() << ": Found flit to retransmit: " << flit->id << endl;
      }
      if (flit) {
        if (_parent->_sim_state == TrafficManager::running) {
          ++_cycles_retransmitting;
          ++_cycles_new_flit_not_injected;
          if (flit->head) {
            ++_packets_retransmitted;
          }
        }
        if (flit->head) {
          ++_packets_retransmitted_full_sim;
        }
        ++_flits_retransmitted_full_sim;
      }
    }


    // If we are not retransmitting an old packet, check the injection buffers for
    // a new flit (which may be a continuation of an in-progress packet).
    if (!flit) {
      flit = find_new_flit_to_inject(dest_buf, last_class, class_limit, subnet);
      if (flit) {
        new_flit = true;
      }

      if ((!flit) && (_parent->_sim_state == TrafficManager::running)) {
        _cycles_new_flit_not_injected++;

        if ((!InjectionBuffersEmpty(0)) || (!PendingRepliesDrained())) {
          _cycles_inj_present_but_blocked++;
        }
        if (InjectionBuffersNotEmptyButAllBlockedOnTimeout(0)) {
          _cycles_inj_all_blocked_on_timeout++;
        }
      }

      if (_debug_enabled && flit) {
        cout << _cur_time << ": " << Name() << ": Found new flit to transmit: " << flit->id
             << ", with packet_id: " << flit->pid << ", type: " << flit_type_strings[flit->type]
             << ", head: " << flit->head << ", tail: " << flit->tail << ", seq_num: "
             << flit->packet_seq_num << ", dest node " << flit->dest << endl;
      }
    }

    if ((!flit) && InjectionBuffersEmpty(0) && PendingRepliesDrained() &&
        (_parent->_sim_state == TrafficManager::running)) {
      _cycles_link_avail_no_new_flits++;
    }

    // If we have a flit to send, insert piggybacked acks.  (If we don't have any
    // we'll manufacture a standalone ack just below.)
    if (flit) {
      insert_piggybacked_acks(flit);

      if (flit->head) {
        if ((flit->type == Flit::ANY_TYPE) ||  // ANY_TYPEs are PUTs
            (flit->type == Flit::WRITE_REQUEST) ||
            (flit->type == Flit::WRITE_REQUEST_NOOP) ||
            (flit->type == Flit::READ_REPLY) ||
            (flit->type == Flit::RGET_GET_REPLY)) {
          _next_packet_injection_blocked_until = _cur_time + _min_packet_processing_penalty;
        } else {
          _next_packet_injection_blocked_until = _cur_time + 1;
        }
      }
    }

    // Manufacture standalone ack.
    else {
      flit = manufacture_standalone_ack(subnet, dest_buf);
    }

    if (flit) {
      if (flit->head) {
        _num_flits_waiting_to_inject += flit->size;
      }
      // Actual injection has to go through the staging buffer.
      if ((flit->type == Flit::ANY_TYPE) ||  // ANY_TYPEs are PUTs
          (flit->type == Flit::WRITE_REQUEST) ||
          (flit->type == Flit::WRITE_REQUEST_NOOP) ||
          (flit->type == Flit::READ_REPLY) ||
          (flit->type == Flit::RGET_GET_REPLY)) {

        int mydest = flit->debug_dest;
        _flits_waiting_to_inject.push({flit, _cur_time + (int)_min_packet_processing_penalty, new_flit});
      } else {
        _flits_waiting_to_inject.push({flit, _cur_time + 1, new_flit});
      }
    }
  }
  //
  // Allowance counter should decrement if host contorl's active and we're sending a relevant packet
  if (flit && flit->head){
      if ((flit->type == Flit::WRITE_REQUEST) ||
         (flit->type == Flit::WRITE_REQUEST_NOOP) ||
         (flit->type == Flit::RGET_GET_REPLY) ||
         (flit->type == Flit::READ_REPLY)
         ){
          mypolicy_host_control_connection_record * my_state = &_mypolicy_connections[flit->dest];
          if (_mypolicy_constant.policy == HC_MY_POLICY) {
            assert(flit->size);
            if (my_state->send_allowance_counter_size >= flit->size){
                // This variable is ignored duing replay
                my_state->send_allowance_counter_size -= flit->size;
                my_state->must_retry_at_least_one_packet = false;
                if (_trace_debug){
                    _parent->debug_trace_file << _cur_time << ",HOSTCTRLALLOWANCEDEST," << _nodeid <<
                    "," << my_state->send_allowance_counter_size << "," << flit->dest << endl;
                }
            }
          }
      }

  }

  // This flit is what will actually get driven into the network this cycle.
  flit = inject_flit(dest_buf);

  process_received_ack_queue();
  process_pending_inbound_response_queue();
  process_pending_outbound_response_queue();

  update_host_bandwidth();
  sender_process_ecn();
  process_put_queue();
  if (_mypolicy_constant.policy == HC_MY_POLICY){
    process_delayed_ack_if_needed();
  }

  return flit;
}


Flit * EndPoint::inject_flit(BufferState * const dest_buf) {
  // The flit received here should be a transmit flit, not the copy in the OPB.

  Flit * flit = NULL;
  if ((!_flits_waiting_to_inject.empty()) &&
      _flits_waiting_to_inject.front().time <= _cur_time) {

    flit = _flits_waiting_to_inject.front().flit;
    bool new_flit = _flits_waiting_to_inject.front().new_flit;
    _flits_waiting_to_inject.pop();
    --_num_flits_waiting_to_inject;
    // Now that we have selected a flit, increment the buffer count and pop the
    // flit off of the injection queue (if it is a new flit).
    int const subnet = flit->subnetwork;
    int const c = flit->cl;

    if (flit->head) {
      find_available_output_vc_for_packet(flit);

      // Set the flit's route
      const FlitChannel * inject = _parent->_net[subnet]->GetInject(_nodeid);
      const Router * router = inject->GetSink();
      assert(router);
      int in_channel = inject->GetSinkPort();
      _parent->_rf(router, flit, in_channel, &flit->la_route_set, false);
      if (flit->watch) {
        *gWatchOut << _cur_time << " | "
                   << Name() << " | "
                   << "Generating lookahead routing info for flit " << flit->id
                   << "." << endl;
      }

      if (_use_crediting) {
        if (_debug_enabled) {
          cout << _cur_time << ": " << Name() << ": Taking VC " << flit->vc << " for flit: "
               << flit->id << endl;
        }
        dest_buf->TakeBuffer(flit->vc);
      }
      // Record this flit's VC as the most recent one used.
      _last_vc[subnet][c] = flit->vc;
    } else {
      // For non-head flit, get vc from previous flit.
      flit->vc = _last_vc[subnet][c];
    }


#ifdef TRACK_FLOWS
    // WARNING: Mutex required when multi-threading is added
    ++(_parent->_outstanding_credits[c][subnet][_nodeid]);
    _parent->_outstanding_classes[_nodeid][subnet][flit->vc].push(c);
    ++_injected_flits[c];
#endif


    // Increment the credit counters to account for the flit being sent, and
    // release the output VC if this is a tail flit.
    if (_use_crediting) {
      dest_buf->SendingFlit(flit);
    }
    if (_trace_debug){
        if (flit->type == Flit::WRITE_REQUEST) {
            _parent->debug_trace_file << _cur_time << ",INJWRITEREQ" <<"," <<
                _nodeid << "," << flit->packet_seq_num << endl;
            int mydest = flit->debug_dest;
            _parent->debug_trace_file << _cur_time << ",INJWRITEREQDEST," <<
                _nodeid << "," << flit->packet_seq_num << "," << mydest << endl;

        } else if (flit->type == Flit::RGET_REQUEST){
            _parent->debug_trace_file << _cur_time << ",INJRGET," <<
                _nodeid << "," << flit->packet_seq_num << endl;
        } else if (flit->type == Flit::RGET_GET_REQUEST){
            _parent->debug_trace_file << _cur_time << ",INJGETREQUEST," <<
                _nodeid << "," << flit->packet_seq_num << endl;
        } else if (flit->type == Flit::RGET_GET_REPLY){
            _parent->debug_trace_file << _cur_time << ",INJGETREPLY," <<
                _nodeid << "," << flit->packet_seq_num << endl;
        } else if  (flit->type == Flit::READ_REQUEST) {
            _parent->debug_trace_file << _cur_time << ",INJREADREQUEST," << _nodeid << "," << flit->packet_seq_num << endl;
        } else if  (flit->type == Flit::READ_REPLY) {
            _parent->debug_trace_file << _cur_time << ",INJREADREPLY," << _nodeid << "," <<
              flit->packet_seq_num << endl;
        } else {
            _parent->debug_trace_file << _cur_time << ",INJ," <<
                _nodeid << "," << flit->packet_seq_num << endl;
        }
    }
  if (_debug_enabled) {
    cout << _cur_time << ": " << Name() << ": Injecting flit " << flit->id
         << ", packet: " << flit->pid << " to dest: "
         << flit->dest << ", debug_dest: "
         << flit->debug_dest << ", head: "
         << flit->head << ", tail: "
         << flit->tail << ", pkt_size: " << flit->size << ", seq_num: "
         << flit->packet_seq_num << ", on vc: " << flit->vc << endl;
  }

/*
    if (_parent->_pri_type == TrafficManager::network_age_based) {
      flit->pri = numeric_limits<int>::max() - _parent->_time;
      assert(flit->pri >= 0);
    }
*/

    if (flit->watch) {
      *gWatchOut << _cur_time << " | " << Name() << " | " << "Injecting flit " << flit->id
                 << " into subnet " << subnet << " at time " << _parent->_time
                 << " with priority " << flit->pri << "." << endl;
    }


//    if (((_parent->_sim_state == TrafficManager::warming_up) || (_parent->_sim_state == TrafficManager::running)) &&
    if ((_parent->_sim_state == TrafficManager::running) &&
        (flit->type != Flit::CTRL_TYPE)) {
      ++_sent_flits[flit->cl];
      if (new_flit) {
        ++_new_sent_flits[flit->cl];
      }

      if (flit->head) {
        ++_sent_packets[flit->cl];
        if (new_flit) {
          ++_new_sent_packets[flit->cl];
        }

        // If this is a data-bearing message, then increment _sent_data_flits.
        if ((flit->type == Flit::READ_REPLY) ||
            (flit->type == Flit::WRITE_REQUEST) ||
            (flit->type == Flit::WRITE_REQUEST_NOOP) ||
            (flit->type == Flit::ANY_TYPE) ||  // Early tests use ANY_TYPE as PUTs/WRITEs
            (flit->type == Flit::RGET_GET_REPLY)) {
          // Subtract 2 flits for the header
          _sent_data_flits += flit->size - 2;
          if (new_flit) {
            _new_sent_data_flits += flit->size - 2;
          }
        }
      }
    }
  }

  return flit;
}

bool EndPoint::packet_qualifies_for_retransmission(Flit::FlitType type, int dest, int size, int data_transfer_size) {
  // This is only used for mypolicy
  assert (_mypolicy_constant.policy == HC_MY_POLICY);
  bool blocked_on_metering = false;

  if ((type == Flit::WRITE_REQUEST) ||
     (type == Flit::WRITE_REQUEST_NOOP) ||
     (type == Flit::RGET_GET_REPLY) ||
     (type == Flit::READ_REPLY)) {
  } {

      assert(size);
      if (_mypolicy_connections[dest].halt_active &&
          (!(_mypolicy_connections[dest].send_allowance_counter_size >= size) &&
           !_mypolicy_connections[dest].must_retry_at_least_one_packet)) {
        blocked_on_metering = true;
      } else {
      }
  }
  return !blocked_on_metering;
}

Flit * EndPoint::find_flit_to_retransmit(BufferState * const dest_buf) {
  Flit * flit = NULL;
  // There are 2 reasons to do a retransmit:
  // 1. We received a nack.
  // 2. The retry timer expired.
  //
  // For any particular dest, we can be in 1 of 3 modes:
  // 1. Sending new packets, not in retry.
  // 2. In a NACK-provoked retry where we resend all packets in OPB back-to-back
  //    before sending any new packets.  (They do not have to be acked before we
  //    begin sending new packets.)  The OPB index number that is currently
  //    being retried is held in _retry_state_tracker[dest].nack_replay_opb_flit_index.
  // 3. In a timeout-provoked retry state where we resend packets from the OPB
  //    as they time-out, and do not allow any new packets to transmit until all
  //    packets in the OPB (for that dest) have been acked and retired.

  // If there is a NACK replay ready to start or currently in progress, service
  // it first, unless there is a packet currently being transmitted due to a
  // timeout.
  if ((!_pending_nack_replays.empty()) && (_timedout_packet_retransmit_in_progress.seq_num == -1)) {
    int dest = _pending_nack_replays.front();
    int opb_index = _retry_state_tracker[dest].nack_replay_opb_flit_index;

    // If the retry index is larger than the number of flits in the OPB for this
    // dest, then something has gone wrong.
    if (opb_index >= (int)_outstanding_packet_buffer[dest].size()) {

      // Previously, this could happen when we allowed newly-received ACKs to
      // clear packets out of the OPB while a NACK-based replay was in progress.
      // This case is no longer permitted.  Instead we pend the ACK until the
      // NACK replay is complete.  This is done in clear_opb_of_acked_packets.

      cout << _cur_time << ": " << Name() << ": ERROR: Attempted to retransmit a flit "
           << "using an index that is beyond the largest in the OPB!  dest: " << dest << endl;
      DumpOPB(dest);
      exit(1);

    } else {
      if (_debug_enabled) {

        cout << _cur_time << ": " << Name() << ": Active NACK replay: Attempting to retry opb entry: "
             << opb_index << " to dest node " << dest << ", with seq_num: "
             << _outstanding_packet_buffer[dest][opb_index]->packet_seq_num << ", with OPB["
             << dest << "] contents: " << endl;
        //DumpOPB(dest);
      }

      Flit * cf = _outstanding_packet_buffer[dest][opb_index];

      if (cf->head){
          if (_mypolicy_constant.policy == HC_MY_POLICY) {
            // For processing NACK
            // If we get -1 packet ID, it means some packet transmission gets cut in the middle.
            // so expected and actual flit id is not the same

            if (_mypolicy_connections[dest].halt_active &&
                !(_mypolicy_connections[dest].send_allowance_counter_size >= cf->size) &&
                !_mypolicy_connections[dest].must_retry_at_least_one_packet){
              // If there're multiple pending nacks try next available
              int this_dest = _pending_nack_replays.front();
              _pending_nack_replays.pop_front();
              _pending_nack_replays.push_back(this_dest);

              if (_debug_enabled) {
                cout << _cur_time << ": " << Name() << ": Retransmission blocked by allowance counter"
                  " for dest " << dest << "counter " <<_mypolicy_connections[dest].send_allowance_counter_size << endl;
              }
              return NULL;
            }


            // IF a NACK has been received during a NACK-based retry,
            // we will update the new packet position only if the current flit is a head
            // flit. This is because we can't intercept a packet in progress

            if (_mypolicy_connections[dest].pending_nack_seq_num != -1){

              // Need to update my flit index so that i can restart from there
              int opb_idx = find_opb_idx_of_seq_num_head(dest, _mypolicy_connections[dest].pending_nack_seq_num + 1);

              assert(opb_idx != -1);

              cf = _outstanding_packet_buffer[dest][opb_idx];
              _retry_state_tracker[dest].nack_replay_opb_flit_index = opb_idx;
              _mypolicy_connections[dest].pending_nack_seq_num = -1;

              if (_debug_enabled) {
                cout << _cur_time << ": " << Name() << ": Setting NACK replay index to " << opb_idx << endl;
              }
            }


              if (!packet_qualifies_for_retransmission(
                          cf->type, dest, cf->size, cf->read_requested_data_size)){
                  return NULL;
              }
          }
      }

      if (cf->head && cf->vc == -1) { // Find first available output VC

        if (cf->watch) {
          *gWatchOut << _cur_time << " | " << Name() << " | Retransmitting flit "
                     << cf->id << " due to NACK." << endl;
        }

        find_available_output_vc_for_packet(cf);
        if (_debug_enabled) {
          cout << _cur_time << ": " << Name()
               << ": Calling find_available_output_vc_for_packet from find_flit_to_retransmit NACK" << endl;
        }
        if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",NACKNEWNVC," << _nodeid <<
            "," << cf->pid << "," << cf->id << "," << cf->packet_seq_num <<
            "," << cf->vc << endl;
        }
      } else {
        if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",NACK," << _nodeid <<
           "," << cf->pid << "," << cf->id <<
           "," << cf->src << "," << cf->dest <<
           "," << cf->packet_seq_num <<
           "," << cf->vc << endl;
        }

      }

      // Create a copy of the flit for transmission from the one in the OPB.
      flit = Flit::New();
      assert(flit != NULL); // For coverity static analaysis
      flit->copy(cf);
      flit->vc = cf->vc;
      flit->la_route_set = cf->la_route_set;

      // Reset the retry timeout for this packet... the copy in the OPB, not
      // the transmitted copy.
      cf->itime = _cur_time;
      cf->expire_time = _cur_time + _retry_timer_timeout;
      if (cf->head) {
        _retry_timer_expiration_queue.push_back({_cur_time + _retry_timer_timeout, cf->dest, cf->packet_seq_num});
      }


      // If we have a flit from a NACK-based replay to retransmit:
      if (flit) {
        if (!_retry_state_tracker[dest].sack) {
            // If this is the last flit for this dest in the OPB, pop the
            // _pending_nack_replays queue, and return the state to IDLE.
            if (opb_index == (int)(_outstanding_packet_buffer[dest].size()-1)) {


              // If a NACK arrives when the last packet in the system is sent out,
              // then it should not get back to IDLE at the end of this packet
              //there is a pending NACK

              if (_mypolicy_connections[dest].pending_nack_seq_num != -1){

                int opb_idx = find_opb_idx_of_seq_num_head(dest,
                    _mypolicy_connections[dest].pending_nack_seq_num + 1);
                assert(opb_idx != -1);

                _retry_state_tracker[dest].nack_replay_opb_flit_index = opb_idx;
                _mypolicy_connections[dest].pending_nack_seq_num = -1;
              } else {
                  _pending_nack_replays.pop_front();
                  _retry_state_tracker[dest].dest_retry_state = IDLE;
                  if (_trace_debug){
                      _parent->debug_trace_file << _cur_time << ",RETRYSTATE," << _nodeid << "," << _retry_state_tracker[dest].dest_retry_state << endl;
                  }

                  if (_debug_enabled) {
                    cout << _cur_time << ": " << Name() << ": Completed NACK-based replay for dest: "
                        << dest << ".  Popping NACK from pending queue." << endl;
                  }
                  if (_trace_debug){
                    _parent->debug_trace_file << _cur_time << ",NACKC," << _nodeid << endl;
                  }

                  // Also apply any pending ACKs that arrived while we were performing
                  // the NACK-based replay
                  if (_retry_state_tracker[dest].pending_ack != -1) {
                    clear_opb_of_acked_packets(dest, _retry_state_tracker[dest].pending_ack);
                    _retry_state_tracker[dest].pending_ack = -1;
                  }
              }

            }

            // Otherwise, increment the index to the next flit.
            else {
              _retry_state_tracker[dest].nack_replay_opb_flit_index++;

              // Just like we do with in the injection buffer, pass the VC "back" to the
              // next flit in the packet, if this is not the tail.
              if (!flit->tail) {
                Flit * const next_flit = _outstanding_packet_buffer[dest][opb_index+1];
                next_flit->vc = flit->vc;
              }
            }

        } else {
          if (_debug_sack) {
            cout << GetSimTime() << ": " << Name() << ": Sending SACKed flit from opb_idx " << opb_index
                 << " to node " << dest << ", recvd sack_vec: 0x" << hex << _retry_state_tracker[dest].sack_vec << dec
                 << ", seq_num_in_progress: " << _retry_state_tracker[dest].seq_num_in_progress
                 << ", pid: " << flit->pid << ", id: " << flit->id << endl;
          }

          int next_sack_idx = sack_vec_next_retrans(_retry_state_tracker[dest].sack_vec >> 1);
          // If this is the last flit for this dest in the OPB, pop the
          // _pending_nack_replays queue, and return the state to IDLE.
          if ((opb_index == (int)(_outstanding_packet_buffer[dest].size()-1)) ||
              // Or we just finished the last flit, and there are no more SACKed packets
              (flit->tail && (next_sack_idx == -1))) {

            _pending_nack_replays.pop_front();
            _retry_state_tracker[dest].dest_retry_state = IDLE;
            _retry_state_tracker[dest].sack = false;

            if (_trace_debug){
                _parent->debug_trace_file << _cur_time << ",RETRYSTATE," << _nodeid << "," << _retry_state_tracker[dest].dest_retry_state << endl;
            }

            if (_debug_sack) {
              cout << _cur_time << ": " << Name() << ": Completed SACK-based replay for dest node "
                   << dest << ".  Popping SACK from pending queue.  "
                   << "orig_sack_vec: 0x" << hex << _retry_state_tracker[dest].orig_sack_vec << dec
                   << ", orig_ack_seq_num: " << _retry_state_tracker[dest].orig_ack_seq_num
                   << ", highest seq_num sent: " << (_packet_seq_num[dest] - 1) << endl;

    //if the receiver ran out of space in the sack_vec, then we know the later packets were not stored.
    //So we need to retransmit them, as well, instead of sending entirely new packets.
            }
            if (_trace_debug){
              _parent->debug_trace_file << _cur_time << "SACKC" << "," << _nodeid << "," << dest <<
              endl;
            }

            // Also apply any pending ACKs that arrived while we were performing
            // the NACK-based replay
            if (_retry_state_tracker[dest].pending_ack != -1) {
              clear_opb_of_acked_packets(dest, _retry_state_tracker[dest].pending_ack);
              _retry_state_tracker[dest].pending_ack = -1;
            }
          }
          // Otherwise, increment the index to the next flit.
          else {
            // Just like we do with in the injection buffer, pass the VC "back" to the
            // next flit in the packet, if this is not the tail.
            if (!flit->tail) {
              Flit * const next_flit = _outstanding_packet_buffer[dest][opb_index+1];
              next_flit->vc = flit->vc;
              _retry_state_tracker[dest].nack_replay_opb_flit_index++;

            } else {
              // If this is a sack, and we're at the end of the packet, check the
              // sack_vec and advance to the next packet to retry.
              if (_debug_sack) {
              cout << GetSimTime() << ": " << Name() << ": Completed retrans of SACKed seq_num "
                   << _retry_state_tracker[dest].seq_num_in_progress << " to node " << dest << endl;
              }

              // Because of the next_sack_idx condition above, we know next_sack_idx is != -1.
              // So handle the next packet.
              _retry_state_tracker[dest].sack_vec >>= (1 + next_sack_idx);
              _retry_state_tracker[dest].seq_num_in_progress += (1 + next_sack_idx);
              _retry_state_tracker[dest].nack_replay_opb_flit_index =
                find_opb_idx_of_seq_num_head(dest, _retry_state_tracker[dest].seq_num_in_progress);

              if (_debug_sack) {
              cout << GetSimTime() << ": " << Name() << ": Starting retrans of SACKed seq_num "
                   << _retry_state_tracker[dest].seq_num_in_progress << " to node " << dest
                   << " from opb_idx: " << _retry_state_tracker[dest].nack_replay_opb_flit_index << endl;
              }
            }
          }
        }

        // Increment retransmission counts
        _parent->_flit_retransmissions++;
        if (cf->head) {
          _parent->_packet_retransmissions++;
          if (cf->transmit_attempts > _max_retry_attempts) {
            cout << _cur_time << ": " << Name() << ": ERROR: Packet " << cf->pid
                 << ", seq_num: " << cf->packet_seq_num << ", dest: " << cf->dest
                 << " attempted more than " << _max_retry_attempts << " transmissions: "
                 << cf->transmit_attempts << endl;
            _parent->EndSimulation();
            exit(1);
          }
          // Increment the transmit_attempts field in the OPB copy.
          cf->transmit_attempts++;
          if ((unsigned int)cf->transmit_attempts > _max_packet_retries_full_sim) {
            _max_packet_retries_full_sim = (unsigned int)cf->transmit_attempts;
          }
        }

      // Make sure to invalidate the VC for this flit in the OPB
      // so that it doesn't get picked up next time.
        cf->vc = -1;

      }
    }
  }

  // If we don't have any flits from a NACK-induced retry, see if there are any
  // packets timing-out by looking at the expire_time of each flit.
  // FIXME: Just look at the head flit.  A non-head flit should never timeout
  // on its own.
  if (!flit) {
    flit = find_timed_out_flit_to_retransmit(dest_buf);
  }

  return flit;
}


Flit * EndPoint::find_timed_out_flit_to_retransmit(BufferState * const dest_buf) {
  Flit * flit = NULL;
  int dest = -1;
  int retry_seq_num = -1;

  bool from_retry_timer = false;
  bool from_response_timer = false;
  int cached_time = 0;
  // If a retransmit is already in progress, continue it.
  if (_timedout_packet_retransmit_in_progress.seq_num != -1) {
    dest = _timedout_packet_retransmit_in_progress.dest;
    retry_seq_num = _timedout_packet_retransmit_in_progress.seq_num;
  }
  else {
    // Check the head of the retry timer queue to see if any packets have
    // timed out.
    if ((!_retry_timer_expiration_queue.empty()) && (_retry_timer_expiration_queue.front().time <= _cur_time)) {
      dest = _retry_timer_expiration_queue.front().dest;
      retry_seq_num = _retry_timer_expiration_queue.front().seq_num;
      _retry_timer_expiration_queue.pop_front();

      // For roll back for mypolicy
      from_retry_timer = true;
      cached_time = _retry_timer_expiration_queue.front().time;
    } else if ((!_response_timer_expiration_queue.empty()) && (_response_timer_expiration_queue.front().time <= _cur_time)) {
      dest = _response_timer_expiration_queue.front().dest;
      retry_seq_num = _response_timer_expiration_queue.front().seq_num;
      _response_timer_expiration_queue.pop_front();
      from_response_timer = true;
      cached_time = _response_timer_expiration_queue.front().time;
    }
  }


  if (dest >= 0) {
    if (!_outstanding_packet_buffer[dest].empty()) {
      // Iterate through all flits until we find the expired one.  It could be
      // the head of the packet, or one of the body flits.  All we need to
      // check is the expire_time of the flit.
      // An optimization is to save the OPB index while we are transmitting a
      // packet (like we do for nack-based retries), but we need to make sure
      // that the OPB can't have packets retired while this is in progress.
      // If that happens, we need to adjust the OPB idx like we do for the
      // NACK-based retry (which happens in clear_opb_of_flit_by_index).
      for (deque< Flit * >::iterator flit_iter = _outstanding_packet_buffer[dest].begin();
           flit_iter < _outstanding_packet_buffer[dest].end();
           flit_iter++) {

        // First look for the packet by seq_num.
        // If the head had its expire time bumped (due to receipt of an ACK,
        // head was set to response timeout), then skip the whole packet.
        if (((*flit_iter)->packet_seq_num < retry_seq_num) && (*flit_iter)->head) {
          // Skip the entire packet if it is older than the packet we're
          // looking for.
          flit_iter += (*flit_iter)->size - 1;
        } else if ((*flit_iter)->packet_seq_num == retry_seq_num) {

          // Then iterate to the next flit in the list that hasn't already
          // been retransmitted based on its expire_time.
          if ((*flit_iter)->expire_time <= _cur_time) {
            if ((((*flit_iter)->type == Flit::READ_REQUEST) || ((*flit_iter)->type == Flit::RGET_REQUEST) ||
                 ((*flit_iter)->type == Flit::RGET_GET_REQUEST)) &&
                 (*flit_iter)->ack_received) {
              cout << _cur_time << ": " << Name() << ": ERROR: " << flit_type_strings[(*flit_iter)->type]
                   << " targeting endpoint " << (*flit_iter)->dest << " was acked (at cycle "
                   << (*flit_iter)->ack_received_time << "), but expired waiting for protocol response.  packet_id: "
                   << (*flit_iter)->pid << ", seq_num: " << (*flit_iter)->packet_seq_num
                   << ", ack_received_time: " << (*flit_iter)->ack_received_time
                   << ", response_received: " << (*flit_iter)->response_received
                   << ", ctime: " << (*flit_iter)->ctime << ", expire_time: " << (*flit_iter)->expire_time << endl;
              cout << "This is a fatal condition!  Shutting down the simulation." << endl << endl;
              _parent->EndSimulation();
              exit(1);
            }

            Flit * const cf = *flit_iter;

            // If head, always look for a new VC, because we could have set it in
            // a prior retry, and we need to call find_available_output_vc_for_packet
            // again to ensure there's enough room for the entire packet.
            if (cf->head) { // Find first available output VC

              if (_mypolicy_constant.policy == HC_MY_POLICY) {
                // For processing NACK
                assert(cf->size);
                if (_mypolicy_connections[cf->dest].halt_active &&
                    !(_mypolicy_connections[cf->dest].send_allowance_counter_size > cf->size) &&
                    !_mypolicy_connections[cf->dest].must_retry_at_least_one_packet){
                  if (from_retry_timer){
                    _retry_timer_expiration_queue.push_front({cached_time, dest, retry_seq_num});
                  } else if(from_response_timer){
                    _response_timer_expiration_queue.push_front({cached_time, dest, retry_seq_num});
                  }
                  return NULL;
                }
              }

              if (_retry_state_tracker[cf->dest].dest_retry_state != TIMEOUT_BASED) {
                // Once we start a timeout-based retry, we have to retry all flits
                // in the OPB for this dest and cannot release until they have all
                // been acked.  This setting will prevent newly received NACKs from
                // starting replays in process_received_acks.  In this case, the
                // NACKs will simply be dropped.
                _retry_state_tracker[cf->dest].dest_retry_state = TIMEOUT_BASED;
                if (_trace_debug){
                    _parent->debug_trace_file << _cur_time << ",RETRYSTATE," << _nodeid << "," << _retry_state_tracker[cf->dest].dest_retry_state << endl;
                }
                if (_debug_enabled) {
                  cout << _cur_time << ": " << Name() << ": Entered TIMEOUT_BASED recovery mode for dest: "
                       << cf->dest << endl;
                }
              }
              if (_debug_enabled) {
                cout << _cur_time << ": " << Name()
                     << ": Calling find_available_output_vc_for_packet from find_flit_to_retransmit TIMEOUT" << endl;
              }

              if (cf->watch) {
                *gWatchOut << _cur_time << " | " << Name() << " | Retransmitting flit "
                           << cf->id << " due to retry timeout." << endl;
              }

              // This sets cf->vc (the copy in the OPB)...
              find_available_output_vc_for_packet(cf);
            }

            // This flit will definitely be retransmitted, so do all of the
            // accounting in this block.

            // Create a copy of the flit for transmission from the one in the OPB.
            flit = Flit::New();
            flit->copy(cf);
            flit->vc = cf->vc;
            flit->la_route_set = cf->la_route_set;

            if (_debug_enabled) {
              cout << _cur_time << ": " << Name() << ": seq_num: " << cf->packet_seq_num
                   << " timed out!!  dest: " << dest << ", expire_time: " << cf->expire_time << endl;
            }

            // Reset the retry timeout for this flit... the copy in the OPB, not
            // the transmitted copy.
            cf->itime = _cur_time;
            cf->expire_time = _cur_time + _retry_timer_timeout;

            // Increment retransmission counts
            _parent->_flit_retransmissions++;

            if (cf->head) {
              // Only push packets (head flits) onto the timer expiration queue.
              _retry_timer_expiration_queue.push_back({_cur_time + _retry_timer_timeout, cf->dest, cf->packet_seq_num});
              _parent->_packet_retransmissions++;

              if (cf->transmit_attempts > _max_retry_attempts) {
                cout << _cur_time << ": " << Name() << ": ERROR: Packet " << cf->pid
                     << ", seq_num: " << cf->packet_seq_num << ", dest: " << cf->dest
                     << ", type: " << cf->type << " attempted more than " << _max_retry_attempts
                     << " transmissions: " << cf->transmit_attempts << ". ack_received: "
                     << cf->ack_received << ", ack_received_time: " << cf->ack_received_time
                     << ", response_received: " << cf->response_received << ", ctime: " << cf->ctime
                     << ", expire_time: " << cf->expire_time << endl;
                _parent->EndSimulation();
                exit(1);
              }
              // Increment the transmit_attempts field in the OPB copy.
              cf->transmit_attempts++;
              if ((unsigned int)cf->transmit_attempts > _max_packet_retries_full_sim) {
                _max_packet_retries_full_sim = (unsigned int)cf->transmit_attempts;
              }

              ++_retry_timeouts;
            }

            // If this is not the last flit, then pass the VC "back" to the
            // next flit in the OPB.
            if (!cf->tail) {
              flit_iter++;
              (*flit_iter)->vc = cf->vc;
              _timedout_packet_retransmit_in_progress = {dest, cf->packet_seq_num};
            }
            // If this *is* the last flit of the packet, then clear the retry state.
            else {
              if (_debug_enabled) {
                cout << _cur_time << ": " << Name() << ": Completed timeout-based retry of packet "
                     << (*flit_iter)->pid << ", to dest: " << dest << ", seq_num: " << cf->packet_seq_num
                     << ".  Pending ack: " << _retry_state_tracker[dest].pending_ack << endl;
              }
              // If there are any pending acks for this dest, clear the OPB as
              // appropriate now that the timeout-based retry is complete.
              if (_retry_state_tracker[dest].pending_ack != -1) {
//                if (_debug_enabled) {
                  cout << _cur_time << ": " << Name()
                       << ": Completed timeout-based retry.  Applying pending ack from OPB for dest: "
                       << dest << ", seq_num: " << _retry_state_tracker[dest].pending_ack << endl;
//                }
                clear_opb_of_acked_packets(dest, _retry_state_tracker[dest].pending_ack);
                _retry_state_tracker[dest].pending_ack = -1;
              }

              if (_trace_debug){
                //_parent->debug_trace_file << _cur_time << ",RETTIMEC," << _nodeid << "," << (*flit_iter)-> pid << "," << dest << endl;
              }
              _timedout_packet_retransmit_in_progress = {-1, -1};
            }

            // Make sure to invalidate the VC for this flit in the OPB
            // so that it doesn't get picked up next time.
            cf->vc = -1;

            // Bail out of the loops since we found a flit, making sure to
            // return a pointer to the new copy, not the one in the OPB.
            return flit;
          }

        } else if ((*flit_iter)->packet_seq_num > retry_seq_num) {
          // Stop here... the expired packet no longer exists in the OPB.
          // This should never happen in the middle of a packet retransmit.

          if ((_timedout_packet_retransmit_in_progress.dest != -1) ||
              (_timedout_packet_retransmit_in_progress.seq_num != -1)) {
            cout << _cur_time << ": " << Name() << ": ERROR: Saw timer expiration, but packet "
                 << "vanished from OPB in the middle of retransmission!!!" << endl;
            exit(1);
          }
          return NULL;
        }
      }
    }
  }

  return flit;
}



// Look for a candidate flit to send from the injection buffer, but it must be
// for the correct subnet and be higher priority than any potential flit that
// was already selected, possibly from the switch being held (above).
// A successful search will set the flit pointer.
Flit * EndPoint::find_new_flit_to_inject(BufferState * const dest_buf, int last_class, int class_limit, int subnet) {
  Flit * flit = NULL;
  int traffic_class = 0;

  if (_new_packet_transmission_in_progress != NULL_Q) {
    flit = find_new_flit_to_inject_from_multi_queue(_new_packet_transmission_in_progress, traffic_class, dest_buf, subnet);

  // Round-robin selection
  } else {
    unsigned int queue_types_checked = 0;
    while ((queue_types_checked < NUM_QUEUE_TYPES) && (!flit)) {
      flit = find_new_flit_to_inject_from_multi_queue(_tx_queue_type_rr_selector, traffic_class, dest_buf, subnet);
      _tx_queue_type_rr_selector = (tx_queue_type_e)(((int)_tx_queue_type_rr_selector + 1) % (int)NUM_QUEUE_TYPES);
      queue_types_checked++;
    }
  }

  if (flit) { // Only here do we finally assign a seq_num.
    flit->packet_seq_num = _packet_seq_num[flit->dest];
//    if (flit->head && (flit->type == Flit::RGET_REQUEST)) {
//      cout << _cur_time << ": " << Name() << ": Injecting RGET to target: "
//           << flit->dest << " with seq num: " << flit->packet_seq_num << endl;
//    }
    if (flit->tail) {
      _packet_seq_num[flit->dest]++;
    }
    // Note that this creates a copy that is inserted into the OPB.
    insert_flit_into_opb(flit);
  }

  return flit;
}


Flit * EndPoint::find_new_flit_to_inject_from_multi_queue(tx_queue_type_e queue_type,
  int traffic_class, BufferState * const dest_buf, int subnet) {

  Flit * flit = NULL;


// We *could* find that dest_retry_state == NACK_BASED here, because we set
// it immediately when we receive a NACK (as long as there isn't a
// TIMEOUT-based reply in progress.)  But it is prevented from starting if
// a new packet is being transmitted by the conditions:
//   if (!_new_packet_transmission_in_progress && !_response_transmission_in_progress) {
//     flit = find_flit_to_retransmit
//assert(_retry_state_tracker[front_flit->dest].dest_retry_state != NACK_BASED);


  vector< list< Flit * > > * flit_buf;
  unsigned int * rr_idx_ptr;

  if (queue_type == NEW_CMD_Q) {
    flit_buf = &_injection_buffer[traffic_class];
    rr_idx_ptr = &_inj_buf_rr_idx;
  } else if (queue_type == READ_REPLY_Q) {
    flit_buf = &_repliesPending;
    rr_idx_ptr = &_rsp_buf_rr_idx;
  } else if (queue_type == RGET_GET_REQ_Q) {
    flit_buf = &_rget_get_req_queues;
    rr_idx_ptr = &_rget_get_req_buf_rr_idx;
  } else {
    cout << _cur_time << ": " << Name() << ": ERROR: Unknown queue_type: " << queue_type << endl;
    exit(1);
  }


  unsigned int num_queues = flit_buf->size();
  unsigned int queues_checked = 0;
  // Round robin across injection queues.
  if (_tx_arb_mode == ROUND_ROBIN) {
    while ((queues_checked < num_queues) &&
           (!flit)) {
      flit = check_single_list_for_available_flit((*flit_buf)[*rr_idx_ptr], dest_buf, subnet);
      if (!flit) {
        // Advance to the next queue.
        // If nothing is found, RR will return to where it started.
        *rr_idx_ptr = (*rr_idx_ptr + 1) % num_queues;
      }
      queues_checked++;
    }

  // WEIGHTED scheduler
  } else {
    while ((queues_checked < num_queues) &&
           (!flit)) {
      // If we are in the middle of transmitting a packet, don't do the token
      // check, since we already did it for the head and decremented the tokens.
      if ((_new_packet_transmission_in_progress != NULL_Q) ||
          (_weighted_sched_queue_tokens[queue_type][*rr_idx_ptr] > 0)) {
        flit = check_single_list_for_available_flit((*flit_buf)[*rr_idx_ptr], dest_buf, subnet);
        if (!flit) {
          // Advance to the next queue.
          // If nothing is found, RR will return to where it started.
          *rr_idx_ptr = (*rr_idx_ptr + 1) % num_queues;
        } else {
          // Decrement by the number of 32B chunks (each flit is 32B)
          _weighted_sched_queue_tokens[queue_type][*rr_idx_ptr] -= flit->size;
          if (_debug_ws) {
            cout << _cur_time << ": " << Name() << ": WS decremented queue " << *rr_idx_ptr
                 << " to: " << _weighted_sched_queue_tokens[*rr_idx_ptr] << endl;
            DumpWeightedSchedulerState();
          }
        }

      // If current queue does not have tokens, advance to the next one.
      } else {
        *rr_idx_ptr = (*rr_idx_ptr + 1) % num_queues;
      }
      queues_checked++;
    }
  }


  // If a flit has been found, add it to the OPB and pop it off the injection
  // buffer.
  if (flit) {
    pop_and_lock((*flit_buf)[*rr_idx_ptr], queue_type, rr_idx_ptr, num_queues);

    if (_trace_debug && *rr_idx_ptr == 0 && queue_type == NEW_CMD_Q){
        //_parent->debug_trace_file << _cur_time << ",INJBUFF," << _nodeid << "," <<
          //_injection_buffer[traffic_class][*rr_idx_ptr].size() << endl;
    }


  } else if (_tx_arb_mode == WEIGHTED) {
    // If there were no injectable flits, then increment the token count of
    // all queues up to the max.
    if (!flit) {
      // Stats collection only...
      // Count queues that could have injected but were blocked by tokens...
      inject_blocked_by_tokens(dest_buf, subnet);

      increment_weighted_scheduler_tokens();
    }
  }

  return flit;
}


void EndPoint::pop_and_lock(list<Flit *> & flit_list, tx_queue_type_e queue_type,
                            unsigned int * rr_idx_ptr, unsigned int rr_max) {
  Flit * flit = flit_list.front();
  flit_list.pop_front();

  if (flit->head) {
    _new_packet_transmission_in_progress = queue_type;

    if (queue_type == NEW_CMD_Q) {
      _full_packets_in_inj_buf[flit->cl][flit->dest]--;
    }
  }

  // If this flit is not the last one in the packet, we need to pass the
  // output VC "back" to the next flit in the injection buffer.
  if (!flit_list.empty() && !flit->tail) {
if (_debug_enabled) {
  cout << _cur_time << ": " << Name() << ": Passing vc: " << flit->vc << " back to flit: " << flit_list.front()->id << endl;
}
    flit_list.front()->vc = flit->vc;

  // Only increment the RR when the packet transmission is complete.
  // Not moving the pointer until the tail is sent allows us to keep
  // transmitting flits from the same packet without adding any
  // additional tracking state.
  } else if (flit->tail) {
    *rr_idx_ptr = (*rr_idx_ptr + 1) % rr_max;

    // Clear transmission in progress state.
    _new_packet_transmission_in_progress = NULL_Q;
  }
}


bool EndPoint::decide_on_put_to_rget_conversion(int dest) {
  if (_put_to_rget_conversion_rate > 0.0) {
    if (RandomFloat() > _put_to_rget_conversion_rate) {
      return false;
    } else {
      return true;
    }
  }
  else if (_enable_adaptive_rget) {
    // Note that this function currently only supports a write_ack_data_samples
    // FIFO size of 1 (corresponding to an rget_convert_min_samples setting of
    // 2). To support larger settings, this function would need to sum all
    // entries in the FIFO.  Since we don't think we need any more than 1 sample,
    // this function simply uses _new_write_ack_data_samples_per_dest[dest][0].

    // Check to see if we should start converting PUTs to RGETs.
    if (!_converting_puts_to_rgets[dest]) {
      // If we have a reasonable amount of write data outstanding
      if ((_outstanding_put_data_samples_per_dest[dest].back() > _rget_convert_min_data_before_convert) &&
        // And it's been a while since the last transition
          (_periods_since_last_transition[dest] >= _rget_min_samples_since_last_transition)) {

        // If we haven't received a large percentage of the acks back yet, then convert to RGET.
        if (_new_write_ack_data_samples_per_dest[dest][0] <
            (_outstanding_put_data_samples_per_dest[dest].back() * _rget_convert_unacked_perc)) {
          _converting_puts_to_rgets[dest] = true;
          _periods_since_last_transition[dest] = 0;

          if (_debug_enabled) {
            cout << _cur_time << ": " << Name() << ": Target: " << dest << ": Starting to convert PUTs to RGETs.  "
                 <<   "outs_data[0]: " << _outstanding_put_data_samples_per_dest[dest][0]
                 << ", outs_data[1]: " << _outstanding_put_data_samples_per_dest[dest][1]
                 << ", acks[0]: "      << _new_write_ack_data_samples_per_dest[dest][0] << endl;
          }
        }
      }
    }
    // Check to see if we should start reverting RGETs to PUTs.
    else {
      // If it's been a while since the last transition
      if (_periods_since_last_transition[dest] >= _rget_min_samples_since_last_transition) {
        // If most of the outstanding data has been acked, then revert to PUTs.
        if (_new_write_ack_data_samples_per_dest[dest][0] >=
            (_outstanding_put_data_samples_per_dest[dest].back() * _rget_revert_acked_perc)) {
          _converting_puts_to_rgets[dest] = false;
          _periods_since_last_transition[dest] = 0;

          if (_debug_enabled) {
            cout << _cur_time << ": " << Name() << ": Target: " << dest << ": Starting to revert RGETs to PUTs.  "
                 <<   "outs_data[0]: " << _outstanding_put_data_samples_per_dest[dest][0]
                 << ", outs_data[1]: " << _outstanding_put_data_samples_per_dest[dest][1]
                 << ", acks[0]: "      << _new_write_ack_data_samples_per_dest[dest][0] << endl;
          }
        }
      }
    }


    if (_converting_puts_to_rgets[dest]) {
      return true;
    } else {
      return false;
    }

  } else {
    return false;
  }
}


// This version assumes the head flit is still in the list.
void EndPoint::convert_put_to_rget(list<Flit *> & flit_list) {
  Flit * head_flit = flit_list.front();
  assert(head_flit->type == Flit::WRITE_REQUEST);
  if (!head_flit->head) {
    cout << _cur_time << ": " << Name() << ": convert_put_to_rget(): ERROR: first flit is not a head flit!  flit id: "
         << head_flit->id << ", tail: " << head_flit->tail << ", type: " << head_flit->type << endl;
    exit(1);
  }

  if (!decide_on_put_to_rget_conversion(flit_list.front()->dest)) {
    return;
  }

  // If we're converting this PUT to an RGET, remove all of the extra data flits
  // from the queue.  Just for the purpose of not perturbing test results, only
  // generate a random number if conversion is enabled.
  _puts_converted_to_rgets++;

  int original_size = head_flit->size;
  int new_size = _parent->_read_request_size[0];

  // A read request should be smaller than a PUT.  Find the size difference and drop the appropriate number of flits.
  if (new_size > original_size) {
    cout << _cur_time << ": " << Name() << ": ERROR: Converting PUT to RGET, size of RGET is larger than original PUT!" << endl;
    exit(1);
  }

  // Just drop the intermediate flits.  This leaves the last flit with tail = 1.
  // FIXME: This will make the generation stat incorrect... need to compensate...
  // Alternatively, leave _generated_flits, and instead add _injected_flits...
  unsigned int flits_to_drop = original_size - new_size;

  _flits_dropped_for_rget_conversion += flits_to_drop;

  // Start dropping the first flit *after* the head.
  // Note that we can't use <list>.erase because that destroys the flit, and we
  // first need to retire it from the trafficmanager.
  list< Flit * >::iterator iter;
  for (iter = flit_list.begin();
       (iter != flit_list.end()); // conditionally increment in body, not here
       ) {
      // Putting into the loop so that coverity won't complain
     if ((*iter)->tail){
         // We ended on tail, so fix it here
         (*iter)->type = Flit::RGET_REQUEST;
         (*iter)->size = new_size;
         (*iter)->read_requested_data_size = original_size;
         break;
     }

    if (!((*iter)->head) && (!(*iter)->tail) && (flits_to_drop > 0)) {
      Flit * dead_flit = *iter;
      // WARNING!!!  Modification of parent member requires mutex if running multi-threaded!
      // Do not call _parent->_RetireFlit, because that treats the flit as legitimate and
      // will capture stats about it.  Instead, just erase it here.
      _parent->_total_in_flight_flits[dead_flit->cl].erase(dead_flit->id);
      if (dead_flit->record) {
        _parent->_measured_in_flight_flits[dead_flit->cl].erase(dead_flit->id);
      }

      // erase invalidates the iterator, but returns the next element in the sequence.
      iter = flit_list.erase(iter);
      dead_flit->Free();

      flits_to_drop--;

    } else {
      (*iter)->type = Flit::RGET_REQUEST;
      (*iter)->size = new_size;
      // Set the size of the data transfer.  Here we can reuse the "read_requested_data_size" member.
      // Maybe rename to: requested_data_size
      // Size includes header flit(s).
      (*iter)->read_requested_data_size = original_size;
      iter++;
    }
  }
}


Flit * EndPoint::check_single_list_for_available_flit(list<Flit *> & flit_list, BufferState * const dest_buf, int subnet) {
  Flit * flit = head_packet_avail_and_qualified(flit_list, subnet);

  if (flit) {
    // If we can find an available output VC for this flit (which is a
    // head flit,) then set flit = candidate_flit.
    // (If the flit is not a head flit, then it got its VC "passed back" to it
    // from the preceding flit.)
    if (flit->head && flit->vc == -1) { // Find first available VC
      if (_debug_enabled) {
        cout << _cur_time << ": " << Name()
             << ": Calling find_available_output_vc_for_packet from find_new_flit_to_inject" << endl;
      }

      find_available_output_vc_for_packet(flit);
    }
  }

  return flit;
}


Flit * EndPoint::head_packet_avail_and_qualified(list<Flit *> & packet_buf, int subnet) {
  Flit * flit = NULL;


  if (!packet_buf.empty()) {
    unsigned int dest = packet_buf.front()->dest;

    // All of the necessary checking is performed on the head flit only.
    // So if this is not a head flit, let it go without any additional
    // conditions.

    if (!packet_buf.front()->head) {
      flit = packet_buf.front();
    } else {
      if (has_priority_standalone_ack()){
        // Allow standalone ack to go if urgent enough
        return NULL;
      }
      if ((packet_buf.front()->subnetwork == subnet) &&
           // Dest is not in retry.
           (_retry_state_tracker[dest].dest_retry_state != TIMEOUT_BASED &&
           _retry_state_tracker[dest].dest_retry_state != NACK_BASED
      )) {
        if (packet_buf.front()->head && !_put_to_noop &&
            (packet_buf.front()->type == Flit::WRITE_REQUEST)) {
          // Note that if we decide not to convert, and then can't issue the PUT,
          // we will come back here again in a later cycle and roll the dice on
          // another conversion of the same packet, with the result that we will
          // convert more PUTs than the user intended with the setting of
          // put_to_rget_conversion_rate.  However, this may not matter much
          // because that knob is just a temporary convenience and will be
          // replaced with something else.
          // if put_to_noop is true, then it will never be converted to rget
          convert_put_to_rget(packet_buf);
        }

        if (new_packet_qualifies_for_arb(packet_buf.front()->type, dest,
              packet_buf.front()->size, packet_buf.front()->read_requested_data_size)) {
          flit = packet_buf.front();
        }
      }
    }
  }

  return flit;
}

void EndPoint::update_stat(unsigned int & stat, int change_amount) {
  if (_parent->_sim_state == TrafficManager::running) {
    stat += change_amount;
  }
}

bool EndPoint::new_packet_qualifies_for_arb(Flit::FlitType type, int dest, int size, int data_transfer_size) {
  bool blocked_on_metering = false;

  // Don't send any more packets to a dest if it might overrun its internal
  // sack_vec tracking.
  // The hardware doesn't have an explicit meter for this.  This meter models
  // hardware's initiator command queues which are 64 entries deep per
  // connection and only clears packets in order.  So if we are waiting for
  // seq_num 0 to complete, we could inject up to packet 63 (provided all of the
  // other meters are satisfied), but will block 64.
  // The depth of the queue we are modeling is configurable via the
  // max_receivable_pkts_after_drop knob.
  if (_sack_enabled && (_outstanding_packet_buffer[dest].size() > 0)) {
    if ((_packet_seq_num[dest] - _outstanding_packet_buffer[dest].front()->packet_seq_num) >= (int) _max_receivable_pkts_after_drop) {
//      cout << _cur_time << ": " << Name() << ": Blocking new packet from transmit to node " << dest
//           << " to prevent overrun of receiver's packet buffering.  max_receivable_pkts_after_drop: "
//           << _max_receivable_pkts_after_drop << ", oldest outstanding packet seq_num: "
//           << _outstanding_packet_buffer[dest].front()->packet_seq_num
//           << ", current packet seq_num: " << _packet_seq_num[dest] << endl;
      blocked_on_metering = true;
    }
  }

  // Broke out each transaction type separately since it was very difficult to
  // follow the prior logic which tried to optimize by reusing the same
  // conditions for similar transaction behaviors.  The different stats also
  // made it more difficult to keep the conditions straightforward.  This new
  // organization makes it very clear and easy to change.

  // ANY_TYPE means that reads and writes are not enabled, so everything is
  // intended to look like a WRITE_REQUEST.
  if ((type == Flit::WRITE_REQUEST) || (type == Flit::WRITE_REQUEST_NOOP)|| (type == Flit::ANY_TYPE)) {

    if (_mypolicy_constant.policy == HC_NO_POLICY ||
        _mypolicy_constant.policy == HC_MY_POLICY
       ) {
        // RATE_LIMIT.reliable_pkts
        if (_outstanding_xactions_per_dest[dest] >= _xaction_limit_per_dest) {
          update_stat(_req_inj_blocked_on_xaction_limit, 1);
          blocked_on_metering = true;
        }
        // RATE_LIMIT.reliable_size
        if ((_outstanding_outbound_data_per_dest[dest] + size) > _xaction_size_limit_per_dest) {
          update_stat(_req_inj_blocked_on_size_limit, 1);
          blocked_on_metering = true;
        }
    }

    if (_mypolicy_constant.policy == HC_HOMA_POLICY){
        // Only tesitng for writes for now
        // RATE_LIMIT.reliable_size
        if ((_outstanding_outbound_data_per_dest[dest] + size) > _estimate_round_trip_cycle) {
          update_stat(_req_inj_blocked_on_size_limit, 1);
          blocked_on_metering = true;
        }
    }

     //Host duplicate ack metering
    if (_mypolicy_constant.policy == HC_MY_POLICY) {
      if (_mypolicy_connections[dest].halt_active &&
          !(_mypolicy_connections[dest].send_allowance_counter_size >= size) &&
          !_mypolicy_connections[dest].must_retry_at_least_one_packet ) {
        blocked_on_metering = true;
      }
    } else if (_mypolicy_constant.policy == HC_TCP_LIKE_POLICY || _mypolicy_constant.policy == HC_ECN_POLICY) {
      // Already done after everything
      //if ((_outstanding_outbound_data_per_dest[dest] + size) >
          //_ack_response_state[dest].host_control_tcplikepolicy_cwd) {
        //blocked_on_metering = true;
      //}
    }
  }

  else if (type == Flit::READ_REQUEST) {
    // Specific limit for GET requests.
    // GET_RATE_LIMIT.outstanding_pkts
    if (_outstanding_gets_per_dest[dest] >= _get_limit_per_dest) {
      update_stat(_read_req_inj_blocked_on_xaction_limit, 1);
      blocked_on_metering = true;
    }
    // Limit on amount of data requested by outstanding GETs.
    // Note that currently we use the same limit as that used for the total size
    // of outstanding transactions.
    // GET_RATE_LIMIT.outstanding_data
    if ((_outstanding_inbound_data_per_dest[dest] + data_transfer_size) >
        _get_inbound_size_limit_per_dest) {
      update_stat(_read_req_inj_blocked_on_size_limit, 1);
      blocked_on_metering = true;
    }
    // Global outstanding GET requests
    // GLOBAL_GET_RATE_LIMIT.outstd_pkts
    if (_outstanding_global_get_requests >= _global_get_request_limit) {
      update_stat(_get_req_inj_blocked_on_global_request_limit, 1);
      blocked_on_metering = true;
    }
    // Global outstanding inbound data
    // GLOBAL_GET_RATE_LIMIT.outstd_data
    if ((_outstanding_global_get_req_inbound_data + data_transfer_size) >
        _global_get_req_size_limit) {
      update_stat(_get_req_inj_blocked_on_global_get_data_limit, 1);
      blocked_on_metering = true;
    }
  }

  else if (type == Flit::READ_REPLY) {
    // RATE_LIMIT.reliable_pkts
    if (_outstanding_xactions_per_dest[dest] >= _xaction_limit_per_dest) {
      update_stat(_resp_inj_blocked_on_xaction_limit, 1);
      blocked_on_metering = true;
    }
    // RATE_LIMIT.reliable_size
    if ((_outstanding_outbound_data_per_dest[dest] + size) > _xaction_size_limit_per_dest) {
      update_stat(_resp_inj_blocked_on_size_limit, 1);
      blocked_on_metering = true;
    }

    if (_mypolicy_constant.policy == HC_MY_POLICY) {
      if (_mypolicy_connections[dest].halt_active &&
          !(_mypolicy_connections[dest].send_allowance_counter_size >= size) &&
          !_mypolicy_connections[dest].must_retry_at_least_one_packet
      ) {
        blocked_on_metering = true;
      }
    }
  }

  else if (type == Flit::RGET_REQUEST) {
    // RATE_LIMIT.reliable_pkts
    if (_outstanding_xactions_per_dest[dest] >= _xaction_limit_per_dest) {
      update_stat(_rget_req_inj_blocked_on_xaction_limit, 1);
      blocked_on_metering = true;
    }
    // RGETs use the size of the data transfer requested as opposed to the size
    // of the transaction itself.
    // RATE_LIMIT.reliable_size
    if ((_outstanding_outbound_data_per_dest[dest] + data_transfer_size) > _xaction_size_limit_per_dest) {
      update_stat(_rget_req_inj_blocked_on_size_limit, 1);
      blocked_on_metering = true;
    }
    // Additional threshold for RGETs
    // RGET.pkt_credit
    if (_outstanding_rget_reqs_per_dest[dest] >= _rget_req_limit_per_dest) {
      update_stat(_rget_req_inj_blocked_on_rget_req_limit, 1);
      blocked_on_metering = true;
    }

    // FIXME for deadlock: Add additional metering on RGET_GET_LIMIT::data_size using data_transfer_size
    // RGET.data_credit
    if (_use_new_rget_metering) {
      if ((_outstanding_rget_inbound_data_per_dest[dest] + data_transfer_size) > _rget_inbound_size_limit_per_dest) {
        update_stat(_rget_req_inj_blocked_on_inbound_data_limit, 1);
        blocked_on_metering = true;
      }
    }
  }

  else if (type == Flit::RGET_GET_REQUEST) {
    // Add additional metering on GET_RATE_LIMIT both # and data size, just like
    // a regular GET.
    // GET_RATE_LIMIT.outstanding_pkts
    if (_outstanding_gets_per_dest[dest] >= _get_limit_per_dest) {
      update_stat(_rget_get_req_inj_blocked_on_get_limit, 1);
      blocked_on_metering = true;
    }
    // Limit on amount of data requested by outstanding GETs.
    // Note that currently we use the same limit as that used for the total size
    // of outstanding transactions.
    // GET_RATE_LIMIT.outstanding_data
    if ((_outstanding_inbound_data_per_dest[dest] + data_transfer_size) >
        _get_inbound_size_limit_per_dest) {
      update_stat(_rget_get_req_inj_blocked_on_inbound_data_limit, 1);
      blocked_on_metering = true;
    }

    // Global outstanding GET requests
    // GLOBAL_GET_RATE_LIMIT.outstd_pkts
    if (_outstanding_global_get_requests >= _global_get_request_limit) {
      update_stat(_rget_get_req_inj_blocked_on_global_request_limit, 1);
      blocked_on_metering = true;
    }
    // Global outstanding inbound data
    if ((_outstanding_global_get_req_inbound_data + data_transfer_size) > _global_get_req_size_limit) {
      update_stat(_rget_get_req_inj_blocked_on_global_get_data_limit, 1);
      blocked_on_metering = true;
    }

      if (_trace_debug){
        _parent->debug_trace_file << _cur_time << ",OUTGETPERDEST," << _nodeid << "," << _outstanding_gets_per_dest[dest] << endl;
        _parent->debug_trace_file << _cur_time << ",OUTINBOUND," << _nodeid << "," << _outstanding_inbound_data_per_dest[dest] << endl;
      }
  } else if (type == Flit::RGET_GET_REPLY) {
    if (_mypolicy_constant.policy == HC_MY_POLICY) {
      if (_mypolicy_connections[dest].halt_active &&
          !(_mypolicy_connections[dest].send_allowance_counter_size >= size) &&
          !_mypolicy_connections[dest].must_retry_at_least_one_packet ) {
        blocked_on_metering = true;
      }
    }
  }

  if (_mypolicy_constant.policy == HC_TCP_LIKE_POLICY || _mypolicy_constant.policy == HC_ECN_POLICY) {
    if ((_outstanding_outbound_data_per_dest[dest] + size) >
        _mypolicy_connections[dest].tcplikepolicy_cwd) {
      blocked_on_metering = true;
    }
  }
  // No metering for RGET_GET_REPLY


  return (!blocked_on_metering &&

          // No way conflict in OPB
          !check_for_opb_insertion_conflict(dest));
}




void EndPoint::increment_weighted_scheduler_tokens() {
  bool changed_state = false;
  // CMD queue
  for (unsigned int queue_idx = 0; queue_idx < _num_injection_queues; ++queue_idx) {
    if (_weighted_sched_queue_tokens[NEW_CMD_Q][queue_idx] < _weighted_sched_req_init_tokens) {
      changed_state = true;

      _weighted_sched_queue_tokens[NEW_CMD_Q][queue_idx] += _weighted_sched_incr_tokens;
      if (_weighted_sched_queue_tokens[NEW_CMD_Q][queue_idx] > _weighted_sched_req_init_tokens) {
        _weighted_sched_queue_tokens[NEW_CMD_Q][queue_idx] = _weighted_sched_req_init_tokens;
      }
    }
  }

  // Response queue
  for (unsigned int queue_idx = 0; queue_idx < _num_response_queues; ++queue_idx) {
    if (_weighted_sched_queue_tokens[READ_REPLY_Q][queue_idx] < _weighted_sched_rsp_init_tokens) {
      changed_state = true;

      _weighted_sched_queue_tokens[READ_REPLY_Q][queue_idx] +=
        (_weighted_sched_incr_tokens * _weighted_sched_rsp_slots_per_req_slot);
      if (_weighted_sched_queue_tokens[READ_REPLY_Q][queue_idx] > _weighted_sched_rsp_init_tokens) {
        _weighted_sched_queue_tokens[READ_REPLY_Q][queue_idx] = _weighted_sched_rsp_init_tokens;
      }
    }
  }

  // RGET Get Request queue... for now use # of Req tokens for max and incr.
  for (unsigned int queue_idx = 0; queue_idx < _num_rget_get_req_queues; ++queue_idx) {
    if (_weighted_sched_queue_tokens[RGET_GET_REQ_Q][queue_idx] < _weighted_sched_req_init_tokens) {
      changed_state = true;

      _weighted_sched_queue_tokens[RGET_GET_REQ_Q][queue_idx] += _weighted_sched_incr_tokens;
      if (_weighted_sched_queue_tokens[RGET_GET_REQ_Q][queue_idx] > _weighted_sched_req_init_tokens) {
        _weighted_sched_queue_tokens[RGET_GET_REQ_Q][queue_idx] = _weighted_sched_req_init_tokens;
      }
    }
  }

  if (_debug_ws && changed_state) {
    cout << _cur_time << ": " << Name() << ": WS: No flits available. Incrementing tokens." << endl;
    DumpWeightedSchedulerState();
  }
}


// This function has no effect on "real" simulator state, it only collects
// stats.
void EndPoint::inject_blocked_by_tokens(BufferState * const dest_buf, int subnet) {
  // Only count 1 per cycle, so bail out if we find 1.
  Flit * flit = NULL;
  int traffic_class = 0;

  if (_parent->_sim_state == TrafficManager::running) {
    for (unsigned int queue_idx = 0; queue_idx < _num_injection_queues; ++queue_idx) {
      if (_weighted_sched_queue_tokens[NEW_CMD_Q][queue_idx] <= 0) {
        flit = check_single_list_for_available_flit(_injection_buffer[traffic_class][queue_idx],
                 dest_buf, subnet);
        if (flit) {
          _req_inj_blocked_on_ws_tokens++;
          return;
        }
      }
    }
    for (unsigned int queue_idx = 0; queue_idx < _num_response_queues; ++queue_idx) {
      if (_weighted_sched_queue_tokens[READ_REPLY_Q][queue_idx] <= 0) {
        flit = check_single_list_for_available_flit(_repliesPending[queue_idx],
                 dest_buf, subnet);
        if (flit) {
          _resp_inj_blocked_on_ws_tokens++;
          return;
        }
      }
    }
    for (unsigned int queue_idx = 0; queue_idx < _num_rget_get_req_queues; ++queue_idx) {
      if (_weighted_sched_queue_tokens[RGET_GET_REQ_Q][queue_idx] <= 0) {
        flit = check_single_list_for_available_flit(_rget_get_req_queues[queue_idx],
                                                    dest_buf, subnet);
        if (flit) {
          _rget_get_req_inj_blocked_on_ws_tokens++;
          return;
        }
      }
    }
  }
}


void EndPoint::insert_flit_into_opb(Flit * flit) {
  // Increment all relevant metering/limiter counters.
  if (flit->head) {
    // Increment on the head flit since we want to potentially prevent any
    // further generation of packets to this dest if we've hit the limit.
    Flit::FlitType type = flit->type;

    if ((type == Flit::WRITE_REQUEST) || (type == Flit::WRITE_REQUEST_NOOP) || (type == Flit::ANY_TYPE)) {
      _outstanding_xactions_per_dest[flit->dest]++;
      _outstanding_put_data_per_dest[flit->dest] += flit->size;
      _outstanding_xactions_all_dests_stat++;  // Stat only
      _outstanding_outbound_data_per_dest[flit->dest] += flit->size;
      if (_trace_debug){
        //_parent->debug_trace_file << _cur_time << ",OUTDATAPERDEST," << _nodeid << "," << _outstanding_outbound_data_per_dest[flit->dest] << endl;
        //_parent->debug_trace_file << _cur_time << ",OUTXACTIONPERDEST," << _nodeid << "," << _outstanding_xactions_per_dest[flit->dest] << endl;
      }
      _outstanding_outbound_data_all_dests_stat += flit->size;  // Stat only
    }
    else if (flit->type == Flit::READ_REQUEST) {
      _outstanding_xactions_all_dests_stat++;  // Stat only
      _outstanding_gets_per_dest[flit->dest]++;
      _outstanding_inbound_data_per_dest[flit->dest] += flit->read_requested_data_size;
      _outstanding_outbound_data_all_dests_stat += flit->size;  // Stat only

      _outstanding_global_get_req_inbound_data += flit->read_requested_data_size;
      ++_outstanding_global_get_requests;
    }
    else if (type == Flit::READ_REPLY) {
      _outstanding_xactions_per_dest[flit->dest]++;
      _outstanding_xactions_all_dests_stat++;  // Stat only
      _outstanding_outbound_data_per_dest[flit->dest] += flit->size;
      if (_trace_debug){
        //_parent->debug_trace_file << _cur_time << ",OUTDATAPERDEST," << _nodeid << "," << _outstanding_outbound_data_per_dest[flit->dest] << endl;
        //_parent->debug_trace_file << _cur_time << ",OUTXACTIONPERDEST," << _nodeid << "," << _outstanding_xactions_per_dest[flit->dest] << endl;
      }
      _outstanding_outbound_data_all_dests_stat += flit->size;  // Stat only
    }
    else if (type == Flit::RGET_REQUEST) {
      _outstanding_xactions_per_dest[flit->dest]++;
      _outstanding_put_data_per_dest[flit->dest] += flit->read_requested_data_size;
      _outstanding_xactions_all_dests_stat++;  // Stat only

      // According to the E2E slides, the size of data requested by an RGET is
      // tracked by RATE_LIMIT::data size.  Regular GETs have both limiters: the
      // size of the request, and the size of the data being requested.  RGETs
      // only have the latter.
      _outstanding_outbound_data_per_dest[flit->dest] += flit->read_requested_data_size;
      if (_trace_debug){
        //_parent->debug_trace_file << _cur_time << ",OUTDATAPERDEST," << _nodeid << "," << _outstanding_outbound_data_per_dest[flit->dest] << endl;
        //_parent->debug_trace_file << _cur_time << ",OUTXACTIONPERDEST," << _nodeid << "," << _outstanding_xactions_per_dest[flit->dest] << endl;
      }
      _outstanding_outbound_data_all_dests_stat += flit->read_requested_data_size;  // Stat only

      _outstanding_rget_reqs_per_dest[flit->dest]++;

      // FIXME for deadlock: Add additional metering on RGET_GET_LIMIT::data_size using data_transfer_size
      if (_use_new_rget_metering) {
        _outstanding_rget_inbound_data_per_dest[flit->dest] += flit->read_requested_data_size;
      }
/*
cout << _cur_time << ": " << Name() << ": Sending RGET_REQUEST to " << flit->dest
     << ".  incrementing outstanding_rget_inbound_data_per_dest by " << flit->read_requested_data_size
     << " to " << _outstanding_rget_inbound_data_per_dest[flit->dest] << endl;
*/
    }
    else if (type == Flit::RGET_GET_REQUEST) {
      _outstanding_xactions_all_dests_stat++;  // Stat only
      _outstanding_outbound_data_all_dests_stat += flit->size;  // Stat only

      // Add additional metering on GET_RATE_LIMIT both # and data size, just
      // like a regular GET.
      _outstanding_gets_per_dest[flit->dest]++;
      _outstanding_inbound_data_per_dest[flit->dest] += flit->read_requested_data_size;

      ++_outstanding_global_get_requests;
      _outstanding_global_get_req_inbound_data += flit->read_requested_data_size;
    }
    // No outbound metering for RGET_GET_REPLY (already accounted for by RGET_REQUEST).


    // Stats only: Update maxes
    if (_outstanding_xactions_per_dest[flit->dest] > _max_outstanding_xactions_per_dest_stat) {
      _max_outstanding_xactions_per_dest_stat = _outstanding_xactions_per_dest[flit->dest];
    }
    if (_outstanding_xactions_all_dests_stat > _max_outstanding_xactions_all_dests_stat) {
      _max_outstanding_xactions_all_dests_stat = _outstanding_xactions_all_dests_stat;
    }
    if (_outstanding_outbound_data_per_dest[flit->dest] > _max_total_outstanding_data_per_dest_stat) {
      _max_total_outstanding_data_per_dest_stat = _outstanding_outbound_data_per_dest[flit->dest];
    }
    if (_outstanding_outbound_data_all_dests_stat > _max_total_outstanding_outbound_data_all_dests_stat) {
      _max_total_outstanding_outbound_data_all_dests_stat = _outstanding_outbound_data_all_dests_stat;
    }


    unsigned int opb_hash = get_opb_hash(flit->dest, flit->packet_seq_num);
    if (_opb_occupancy_map.count(opb_hash) == 0) {
      _opb_occupancy_map[opb_hash] = 1;
    } else {
      assert(_opb_occupancy_map[opb_hash] < _opb_ways);
      _opb_occupancy_map[opb_hash]++;
    }

    if (flit->head) {
      flit->transmit_attempts = 1;
    }
  }

  // Put the copy in the OPB.  We will return the transmittable copy.
  Flit * opb_flit_copy = Flit::New();
  opb_flit_copy->copy(flit);
  // These two data members are used to determine when to clear a packet
  // that requires a response from the OPB.  If the transaction requires
  // a response, then both attributes must be true.
  opb_flit_copy->ack_received = false;
  opb_flit_copy->response_received = false;

  // Set the injection time of the flit in the OPB for timeout detection.
  opb_flit_copy->itime = _cur_time;
  opb_flit_copy->first_itime = _cur_time;
  opb_flit_copy->expire_time = _cur_time + _retry_timer_timeout;
  // Only push packets onto the timer expiration queue.
  if (opb_flit_copy->head) {
    _retry_timer_expiration_queue.push_back({_cur_time + _retry_timer_timeout, opb_flit_copy->dest, opb_flit_copy->packet_seq_num});
  }

  _outstanding_packet_buffer[opb_flit_copy->dest].push_back(opb_flit_copy);

  if (opb_flit_copy->head) {
    // If this is a head flit, then increment the opb packet count.
    _opb_pkt_occupancy++;
  } else {
    // Non-head flits temporarily hold the dest for convenience in putting
    // it into the right place in the OPB, but we need to set it to -1
    // before sending so we don't use it to "cheat" in our routing, since
    // a real non-head flit will not carry the dest.
    opb_flit_copy->debug_dest = opb_flit_copy->dest;
    opb_flit_copy->dest = -1;

  }
}

void EndPoint::update_flit_ack_if_needed_for_ecn(Flit * flit) {
  if (_mypolicy_constant.policy == HC_ECN_POLICY) {
    // If there's congestion
    if (occupied_size() + _mypolicy_endpoint.reserved_space > _mypolicy_constant.ecn_threshold){
        flit->ecn_congestion_detected = true;
    }
  }
}

void EndPoint::insert_piggybacked_acks(Flit * flit) {
  if (flit->head) {
    // Piggyback acks for messages that we've received from this remote node.
    // But this flit has to be destined for that same node that the ack must
    // be returned to.
    if (_ack_response_state[flit->dest].outstanding_ack_type_to_return == ACK) {

      if (_debug_enabled) {
        cout << _cur_time << ": " << Name() << ": Sending piggybacked ACK to dest node "
             << flit->dest << " with ack_seq_num: "
             << _ack_response_state[flit->dest].last_valid_seq_num_recvd_and_ackd
             << " on packet: " << flit->pid << " (fid: " << flit->id << ")" << endl;
      }

      if (_mypolicy_constant.policy == HC_MY_POLICY){

        flit->ack_seq_num = _ack_response_state[flit->dest].last_valid_seq_num_recvd_and_ackd;
        if (queue_depth_over_threshold()){
            flit->nack_seq_num = flit->ack_seq_num;
        } else {
            flit->nack_seq_num = -1;
        }
        if (_ack_response_state[flit->dest].last_valid_seq_num_recvd_and_ready_to_ack >
            _ack_response_state[flit->dest].last_valid_seq_num_recvd_and_ackd){
          _ack_response_state[flit->dest].last_valid_seq_num_recvd_and_ackd ++;
        } else {
          _ack_response_state[flit->dest].packets_recvd_since_last_ack = 0;
        }
      } else {
        flit->ack_seq_num = _ack_response_state[flit->dest].last_valid_seq_num_recvd;
        flit->nack_seq_num = -1;
        _ack_response_state[flit->dest].packets_recvd_since_last_ack = 0;
      }

      if (_mypolicy_constant.policy == HC_ECN_POLICY){
        update_flit_ack_if_needed_for_ecn(flit);
      }

      if (_trace_debug){
        if (flit->nack_seq_num == -1){
          //_parent->debug_trace_file << _cur_time << ",ACKPIG," << _nodeid << "," <<
            //flit->ack_seq_num << endl;
          _parent->debug_trace_file << _cur_time << ",ACKPIGDEST," << _nodeid << "," <<
            flit->ack_seq_num << "," << flit->dest << endl;
        } else {
          //_parent->debug_trace_file << _cur_time << ",ACKPIGD," << _nodeid << "," <<
            //flit->ack_seq_num << endl;
          _parent->debug_trace_file << _cur_time << ",ACKPIGDDEST," << _nodeid << "," <<
            flit->ack_seq_num << "," << flit->dest << endl;
        }
      }

      flit->sack = false;
      // Always piggyback acks, even duplicates (so leave the state in ACK)
      _ack_response_state[flit->dest].time_last_valid_unacked_packet_recvd = 999999999; // Clear it
      _ack_response_state[flit->dest].time_last_ack_sent = _cur_time;
    } else if (_ack_response_state[flit->dest].outstanding_ack_type_to_return == NACK) {

      if (_debug_enabled) {
        cout << _cur_time << ": " << Name() << ": Sending piggybacked NACK to dest node "
              << flit->dest << " with seq_num: "
             << _ack_response_state[flit->dest].last_valid_seq_num_recvd
             << " on packet: " << flit->pid << " (fid: " << flit->id << ")" << endl;
      }

      if (_mypolicy_constant.policy == HC_MY_POLICY){
        // IF we have a nack, we ack all received sequence numbe and update the acked variable
        flit->nack_seq_num = _ack_response_state[flit->dest].last_valid_seq_num_recvd;
        // We reserve space for first packet coming from the initiator after NACK
        target_reserve_put_space_for_initiator_if_needed(flit->dest);
      } else {
        if (HC_HOMA_POLICY != _mypolicy_constant.policy) {
          flit->nack_seq_num = _ack_response_state[flit->dest].last_valid_seq_num_recvd;
        } else {
          flit->nack_seq_num = -1;
        }
      }
      flit->ack_seq_num = -1;
      flit->sack = false;

      if (_trace_debug){
        _parent->debug_trace_file << _cur_time << ",NACKPIGDEST," << _nodeid << "," <<  flit->nack_seq_num <<
          "," << flit->dest<< endl;
      }
      // We only send one NACK, so flip it back to ACK.
      _ack_response_state[flit->dest].outstanding_ack_type_to_return = ACK;
      _ack_response_state[flit->dest].time_last_valid_unacked_packet_recvd = 999999999; // Clear it
      _ack_response_state[flit->dest].packets_recvd_since_last_ack = 0;
        if(_nodeid == 1){
          cout << "DDD" << endl;
        }
      _nacks_sent++;

    } else if (_ack_response_state[flit->dest].outstanding_ack_type_to_return == SACK) {
      unsigned long masked_sack_vec = _ack_response_state[flit->dest].sack_vec & _sack_vec_mask;

//      if (_debug_enabled) {
      if (_debug_sack) {
        cout << _cur_time << ": " << Name() << ": Sending piggybacked SACK to dest node "
              << flit->dest << " with seq_num: "
             << _ack_response_state[flit->dest].last_valid_seq_num_recvd
             << ", sack_vec: 0x" << hex << masked_sack_vec << dec
             << ", on packet: " << flit->pid << " (fid: " << flit->id << ")" << endl;
      }
      if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",SACKPIG," << _nodeid << "," << flit->dest <<
          "," << flit-> id << "," << flit->pid <<
          "," << _ack_response_state[flit->dest].last_valid_seq_num_recvd << endl;
      }


//      }

// FIXME: Set ACK or NACK field???
// TODO: delayed ack
      flit->sack = true;
      // Limit the # of bits we can send in the sack_vec.
      flit->sack_vec = masked_sack_vec;
      flit->ack_seq_num = _ack_response_state[flit->dest].last_valid_seq_num_recvd;
// FIXME: Update _ack_response_state
      // We only send one SACK, so flip it back to ACK.
      _ack_response_state[flit->dest].outstanding_ack_type_to_return = ACK;
      _ack_response_state[flit->dest].time_last_valid_unacked_packet_recvd = 999999999; // Clear it
      _ack_response_state[flit->dest].packets_recvd_since_last_ack = 0;
      _sacks_sent++;

    }
  }
}

bool EndPoint::has_priority_standalone_ack() {
  // This is for my policy, if a standalone has waited for too long and still not sent, we force send it

  if (_mypolicy_constant.policy == HC_MY_POLICY){
    for(unsigned int initiator = 0; initiator < _endpoints; initiator++) {
      if ((_ack_response_state[initiator].outstanding_ack_type_to_return == ACK) ||
          (_ack_response_state[initiator].outstanding_ack_type_to_return == NACK) ||
          (_ack_response_state[initiator].outstanding_ack_type_to_return == SACK)) {

        bool ack_cond = ((_ack_response_state[initiator].time_last_valid_unacked_packet_recvd +
                _cycles_before_standalone_ack_has_priority) <= _cur_time);
        if (ack_cond) return true;
      }
    }
  }
  return false;
}

Flit * EndPoint::manufacture_standalone_ack(int subnet, BufferState * dest_buf) {
  // If we don't have any "real" flits to send, but we do have acks to return,
  // consider manufacturing standalone acks.  Note that standalone acks don't
  // get put into the OPB.
  Flit * ack_flit = NULL;

  // Loop through the dests and see if we have any acks to send that have
  // hit their limit.
  // FIXME: Fairness: After a ready ack was found, start the next search at the
  // very next entry instead of 0.
  for(unsigned int initiator = 0; initiator < _endpoints; initiator++) {
    if ((_ack_response_state[initiator].outstanding_ack_type_to_return == ACK) ||
        (_ack_response_state[initiator].outstanding_ack_type_to_return == NACK) ||
        (_ack_response_state[initiator].outstanding_ack_type_to_return == SACK)) {

      bool ack_cond1 = ((_ack_response_state[initiator].time_last_valid_unacked_packet_recvd +
              _cycles_before_standalone_ack) <= _cur_time);
      bool ack_cond2 = (_ack_response_state[initiator].packets_recvd_since_last_ack >= _packets_before_standalone_ack);

      // Speculative ack shared by another connection
      bool ack_cond3 = (_mypolicy_connections[initiator].speculative_ack_allowance_size);
      //Speculative ack timed out
      bool ack_cond4 = _mypolicy_connections[initiator].earliest_accum_ack_shared_time != -1 &&
        _mypolicy_connections[initiator].earliest_accum_ack_shared_time +
        _mypolicy_constant.time_before_shared_ack_timeout < _cur_time;

      // Check to see if we've hit the cycle limit to send a standalone ack.
      if ( ack_cond1 || ack_cond2 || ack_cond3 || ack_cond4) {

        if(_nodeid == 1){
          cout << _cur_time << ": HEHE " << endl;
        }


        // If we're using policy, we may have to skip if it's an ACK
        if (_mypolicy_constant.policy == HC_MY_POLICY &&
            _ack_response_state[initiator].outstanding_ack_type_to_return == ACK){
          if (_mypolicy_constant.speculative_ack_enabled){
            if (!ack_cond1 && !ack_cond2 && ack_cond3){
              mypolicy_update_periodic_ack_occupancy(initiator,
                  _mypolicy_connections[initiator].speculative_ack_allowance_size);
              _mypolicy_connections[initiator].speculative_ack_allowance_size = 0;
            }
          }
        }
        _mypolicy_connections[initiator].earliest_accum_ack_shared_time = -1;

        // Queue up a flit
        ack_flit = Flit::New();
        ack_flit->type = Flit::CTRL_TYPE;
        ack_flit->subnetwork = 0;
        ack_flit->src = _nodeid;
        ack_flit->dest = initiator;
        if (_ack_response_state[initiator].outstanding_ack_type_to_return == ACK) {

          if (_mypolicy_constant.policy == HC_MY_POLICY) {
            ack_flit->ack_seq_num = _ack_response_state[initiator].last_valid_seq_num_recvd_and_ackd;
          } else {
            ack_flit->ack_seq_num = _ack_response_state[initiator].last_valid_seq_num_recvd;
          }

          if (_mypolicy_constant.policy == HC_ECN_POLICY){
            update_flit_ack_if_needed_for_ecn(ack_flit);
          }

          if (queue_depth_over_threshold()){
              ack_flit->nack_seq_num = ack_flit->ack_seq_num;
          } else {
              ack_flit->nack_seq_num = -1;
          }
          ack_flit->sack = false;

        } else if (_ack_response_state[initiator].outstanding_ack_type_to_return == NACK) {

          // IF we have a nack, we ack all received sequence numbe and update the acked variable
          if (_mypolicy_constant.policy != HC_HOMA_POLICY) {
            ack_flit->nack_seq_num = _ack_response_state[initiator].last_valid_seq_num_recvd;
          } else {
            ack_flit->nack_seq_num = -1;
          }
          ack_flit->ack_seq_num = -1;
          ack_flit->sack = false;
          _nacks_sent++;
          if (_mypolicy_constant.policy == HC_MY_POLICY){
            // We sent a standalone NACK, so we should reserve space
            target_reserve_put_space_for_initiator_if_needed(initiator);
          }

        } else { // SACK
          //TODO delayed ack
          ack_flit->ack_seq_num = _ack_response_state[initiator].last_valid_seq_num_recvd;
          ack_flit->nack_seq_num = -1;
          ack_flit->sack = true;
          ack_flit->sack_vec = _ack_response_state[initiator].sack_vec;
          _nacks_sent++;
          cout << _cur_time << ": " << Name() << ": Sending standalone SACK to dest node "
               << ack_flit->dest << " with ack_seq_num: " << ack_flit->ack_seq_num << ", and sack_vec: 0x"
               << hex << ack_flit->sack_vec << dec << endl;
        }
        // FIXME: Standalone acks are not associated with any traffic class, so use a
        // class of 0 for now.  Will need to fix this later if we start using
        // traffic classes.
        ack_flit->cl = 0;
        ack_flit->vc = 0;
        ack_flit->head = 1;  // Standalone ack is a single-flit packet
        ack_flit->tail = 1;
        ack_flit->size = 1;

        // Stop iterating
//cout << _cur_time << ": " << Name() << ": Found initiator to send standalone ack to: " << initiator
//     << " with type: " << _ack_response_state[initiator].outstanding_ack_type_to_return
//     << " last seq num recvd: " << _ack_response_state[initiator].last_valid_seq_num_recvd
//     << " and last packet recvd time: " << _ack_response_state[initiator].time_last_valid_unacked_packet_recvd << endl;
        initiator = _endpoints;
      }
    }
  }

  if (ack_flit) {
    // We can only send this flit if we have an output VC available
    if (_debug_enabled) {
      cout << _cur_time << ": " << Name()
           << ": Calling find_available_output_vc_for_packet from manufacture_standalone_ack" << endl;
    }
    find_available_output_vc_for_packet(ack_flit);

    if (_debug_enabled) {
      if (ack_flit->ack_seq_num != -1) {
        if (!ack_flit->sack) {
          cout << _cur_time << ": " << Name() << ": Sending standalone ACK to dest node "
               << ack_flit->dest << " with ack_seq_num: " << ack_flit->ack_seq_num << endl;
        } else {
          cout << _cur_time << ": " << Name() << ": Sending standalone SACK to dest node "
               << ack_flit->dest << " with ack_seq_num: " << ack_flit->ack_seq_num << ", and sack_vec: 0x"
               << hex << ack_flit->sack_vec << dec << endl;
        }
      } else if (ack_flit->nack_seq_num != -1) {
        cout << _cur_time << ": " << Name() << ": Sending standalone NACK to dest node "
             << ack_flit->dest << " with nack_seq_num: " << ack_flit->nack_seq_num << endl;
        if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",NACKALONE," << _nodeid << "," <<
            ack_flit->nack_seq_num << endl;
        }
      } else {
        cout << _cur_time << ": " << Name() << ": ERROR: Attempting to send "
        "standalone ctrl message to: " << ack_flit->dest << ", with ACK and NACK both -1" << endl;
        exit(1);
      }
    }
    if (_trace_debug){
      if (ack_flit->ack_seq_num != -1) {
        if (!ack_flit->sack) {
          if (ack_flit->ack_seq_num == ack_flit->nack_seq_num){
            _parent->debug_trace_file << _cur_time << ",ACKALONEDDEST," << _nodeid << "," <<
              ack_flit->ack_seq_num << "," << ack_flit->dest << endl;
          } else {
            _parent->debug_trace_file << _cur_time << ",ACKALONEDEST," << _nodeid << "," <<
              ack_flit->ack_seq_num << "," << ack_flit->dest << endl;
          }
          //_parent->debug_trace_file << _cur_time << ",ACKALONE," << _nodeid << "," << ack_flit->ack_seq_num << endl;
        } else {
          _parent->debug_trace_file << _cur_time << ",SACKALONE," << _nodeid << "," << ack_flit->dest <<
          "," << ack_flit->ack_seq_num << endl;
        }
      } else if (ack_flit->nack_seq_num != -1) {
          _parent->debug_trace_file << _cur_time << ",NACKALONEDEST," << _nodeid << "," << ack_flit->nack_seq_num <<
            "," << ack_flit->dest << endl;
      } else {
        if(_mypolicy_constant.policy == HC_HOMA_POLICY){
          // not sure how it reached here
        } else {
          exit(1);
        }
      }
    }

    if (_mypolicy_constant.policy == HC_MY_POLICY) {
      if (_ack_response_state[ack_flit->dest].last_valid_seq_num_recvd_and_ready_to_ack >
          _ack_response_state[ack_flit->dest].last_valid_seq_num_recvd_and_ackd){
        _ack_response_state[ack_flit->dest].last_valid_seq_num_recvd_and_ackd ++;
      } else {
        _ack_response_state[ack_flit->dest].packets_recvd_since_last_ack = 0;
      }
    } else {
      _ack_response_state[ack_flit->dest].packets_recvd_since_last_ack = 0;
    }

    // Clear the ack response tracker
    _ack_response_state[ack_flit->dest].outstanding_ack_type_to_return = ACK;
    _ack_response_state[ack_flit->dest].time_last_valid_unacked_packet_recvd = 999999999; // Clear it

    _parent->_standalone_acks_transmitted++;

    _ack_response_state[ack_flit->dest].time_last_ack_sent = _cur_time;
  }

  return ack_flit;
}


void EndPoint::find_available_output_vc_for_packet(Flit * cf) {
  if (cf->watch) {
    *gWatchOut << _cur_time << " | " << Name() << " | "
               << "Finding output VC for flit " << cf->id
               << ":" << endl;
  }
  if (_debug_enabled) {
    cout << _cur_time << ": " << Name() << ": find_available_output_vc_for_packet(): flit: "
         << cf->id << ", head: " << cf->head << ", vc: " << cf->vc << endl;
  }

  cf->vc = cf->dest % _parent->_vcs;
}


void EndPoint::_ReceiveFlit(int subnet, Flit * flit) {
  if (flit->watch) {
    *gWatchOut << _cur_time << " | "
               << Name() << " | "
               << "Ejecting flit " << flit->id
               << " (packet " << flit->pid << ")"
               << " from network on VC " << flit->vc
               << "." << endl;
  }

  if (_trace_debug){
    //_parent->debug_trace_file << _cur_time << ",EJ," << _nodeid << "," << flit->packet_seq_num << endl;

    if (flit->type == Flit::RGET_GET_REQUEST){
        _parent->debug_trace_file << _cur_time << ",RGETREQUESTR," << _nodeid << "," << flit->packet_seq_num << endl;
    } else if  (flit->type == Flit::RGET_GET_REPLY) {
        _parent->debug_trace_file << _cur_time << ",RGETREPLYR," << _nodeid << "," << flit->packet_seq_num << endl;
    } else if  (flit->type == Flit::WRITE_REQUEST) {
        _parent->debug_trace_file << _cur_time << ",WRITEDESTR," << _nodeid << "," << flit->packet_seq_num <<
          "," << flit->src << endl;
        //_parent->debug_trace_file << _cur_time << ",WRITER," << _nodeid << "," << flit->packet_seq_num << endl;
    } else if  (flit->type == Flit::RGET_REQUEST) {
        _parent->debug_trace_file << _cur_time << ",RGETR," << _nodeid << "," << flit->packet_seq_num << endl;
    } else if  (flit->type == Flit::READ_REQUEST) {
        _parent->debug_trace_file << _cur_time << ",READREQUESTR," << _nodeid << "," << flit->packet_seq_num << endl;
    } else if  (flit->type == Flit::READ_REPLY) {
        _parent->debug_trace_file << _cur_time << ",READREPLYR," << _nodeid << "," <<
          flit->packet_seq_num << endl;
    }
  }

  // Check if this flit reached the correct endpoint.
  if (flit->head && (flit->dest != _nodeid)) {
    cout << _cur_time << ": " << Name() << ": ERROR: Received flit (id:" << flit->id << ") intended for dest " << flit->dest << endl;
  }

  // Note that we don't queue up the flit itself, because it may get retired and
  // freed before we process its ack fields.
  if ((flit->ack_seq_num != -1) || (flit->nack_seq_num != -1)) {
    recvd_ack_record local_record = {.time = (_cur_time + (int)_ack_processing_latency),
                                     .subnet = subnet, .target = flit->src, .ack_seq_num = flit->ack_seq_num,
                                     .nack_seq_num = flit->nack_seq_num, .flit_id = flit->id,
                                     .is_standalone = (flit->type ==  Flit::CTRL_TYPE),
                                     .sack = flit->sack, .sack_vec = flit->sack_vec};
    _received_ack_queue.push_back(local_record);
  }

  if (_mypolicy_constant.policy == HC_ECN_POLICY) {
    if (flit->tail) {
      _mypolicy_connections[flit->src].ecn_total ++;
      if (flit->ecn_congestion_detected){
        _mypolicy_connections[flit->src].ecn_count ++;
      }
    }
  }

  // Push it into the incoming queue for credit and protocol processing.
  _incoming_flit_queue[subnet].push_back(flit);

//  if (((_parent->_sim_state == TrafficManager::warming_up) || (_parent->_sim_state == TrafficManager::running)) &&
  if ((_parent->_sim_state == TrafficManager::running) &&
      (flit->type != Flit::CTRL_TYPE)) {
    ++_received_flits[flit->cl];
    if (flit->head) {
      ++_received_packets[flit->cl];
      if ((flit->type == Flit::READ_REPLY) ||
          (flit->type == Flit::WRITE_REQUEST) ||
          (flit->type == Flit::WRITE_REQUEST_NOOP) ||
          (flit->type == Flit::ANY_TYPE) ||  // Early tests use ANY_TYPE as PUTs/WRITEs
          (flit->type == Flit::RGET_GET_REPLY)) {
        // Subtract 2 flits for the header
        _received_data_flits += flit->size - 2;
      }
    }

  }
}


void EndPoint::process_received_ack_queue() {
  while (!_received_ack_queue.empty()) {
    // Process acks in order.  If we encounter a flit that isn't ready, do not
    // process any younger flits, and instead bail out of the loop.
    if (_received_ack_queue.front().time > _cur_time) {
      break;
    }
    recvd_ack_record record = _received_ack_queue.front();
    assert(record.time == _cur_time);

    if (record.nack_seq_num != -1) {
      _nacks_received++;
    }
    if (record.sack) {
      ++_sacks_received;
    }
    process_received_acks(record);

    _received_ack_queue.pop_front();
  }

  // Host control timeout logic if no ack has been received
  for(unsigned int initiator = 0; initiator < _endpoints; initiator++) {
    if (_mypolicy_constant.policy == HC_MY_POLICY) {
      if (_mypolicy_connections[initiator].time_last_ack_recvd +
          _mypolicy_constant.time_before_halt_state_timeout <= _cur_time &&
          _mypolicy_connections[initiator].halt_active){
        _mypolicy_connections[initiator].halt_active = false;
        _mypolicy_connections[initiator].send_allowance_counter_size = 0;
        _mypolicy_connections[initiator].halt_state = -1;
        _mypolicy_connections[initiator].time_last_ack_recvd = 999999999;
        if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",HOSTCTRLACTIVEDEST," << _nodeid <<
          "," << _mypolicy_connections[initiator].halt_active << "," << initiator << endl;
          _parent->debug_trace_file << _cur_time << ",HOSTCTRLHALTDEST," << _nodeid <<
          "," << _mypolicy_connections[initiator].halt_state << "," << initiator << endl;
        }
      }
    }
  }
}

void EndPoint::reset_buffer_occupancy(){
  for(unsigned int initiator = 0; initiator < _endpoints; initiator++) {
    _mypolicy_connections[initiator].periodic_buffer_occupancy = 0;
  }
  _mypolicy_endpoint.periodic_total_occupancy = 0;
  _mypolicy_endpoint.total_packet_occupy = 0;
  _mypolicy_endpoint.next_fairness_request_time = _cur_time +
    _mypolicy_constant.fairness_sampling_time;
  _mypolicy_endpoint.next_fairness_reset_time = _cur_time +
    _mypolicy_constant.fairness_reset_period;
}

void EndPoint::reset_ack_occupancy(){
  for(unsigned int initiator = 0; initiator < _endpoints; initiator++) {
    _mypolicy_connections[initiator].periodic_ack_occupancy = 0;
  }
  _mypolicy_endpoint.next_fairness_reset_time = _cur_time +
    _mypolicy_constant.fairness_reset_period;
}


void EndPoint::mypolicy_update_periodic_ack_occupancy(int source, int size){
  if (_cur_time > _mypolicy_endpoint.next_fairness_reset_time){
    reset_ack_occupancy();
  }
  _mypolicy_connections[source].periodic_ack_occupancy += size;
}

void EndPoint::mypolicy_update_periodic_occupancy(put_wait_queue_record * entrance, put_wait_queue_record * exit){

  if (_cur_time > _mypolicy_endpoint.next_fairness_reset_time){
    reset_buffer_occupancy();
  }
  int entrance_src = entrance? entrance->src: -1;

  if (entrance_src >= 0) {
    _mypolicy_connections[entrance_src].periodic_buffer_occupancy += entrance->size;
    _mypolicy_endpoint.periodic_total_occupancy += entrance->size;
    _mypolicy_endpoint.total_packet_occupy++;
    if (_trace_debug){
      _parent->debug_trace_file << _cur_time << ",BUFFOCC," << entrance_src <<
      "," << (int)(occupancy_ratio_period(entrance_src)* 100) << endl;
      _parent->debug_trace_file << _cur_time << ",BUFFOCCTOTAL," << entrance_src <<
      "," << _mypolicy_endpoint.periodic_total_occupancy << endl;
    }
  }

}

void EndPoint::target_reserve_put_space_for_initiator_if_needed(int initiator) {

    if (!_mypolicy_connections[initiator].space_after_NACK_reserved){
      _mypolicy_connections[initiator].space_after_NACK_reserved = true;
      _mypolicy_endpoint.reserved_space += _mypolicy_constant.NACK_reservation_size;

      if (_debug_enabled) {
        cout << _cur_time << ": " << Name() << ": Reserving space for " << initiator <<
          " final reserved space " <<  _mypolicy_endpoint.reserved_space << " " <<
        _mypolicy_connections[initiator].space_after_NACK_reserved << endl;
      }
    } else {
      if (_debug_enabled) {
          cout << _cur_time << ": " << Name() <<
            ": NACK sent again after space already reserved" << endl;
      }
    }

}


void EndPoint::mypolicy_update_initiator_state_with_ack(recvd_ack_record record, int acked_size){

  mypolicy_host_control_connection_record * my_state = &_mypolicy_connections[record.target];

  my_state->time_last_ack_recvd = _cur_time;

  if (record.ack_seq_num == -1){
    // NACK
    my_state->halt_state = _mypolicy_constant.max_ack_before_send_packet;
    my_state->halt_active = true;
    if (!my_state->send_allowance_counter_size)
      my_state->send_allowance_counter_size += acked_size;
    my_state->must_retry_at_least_one_packet = true;

    if (_trace_debug){
        _parent->debug_trace_file << _cur_time << ",HOSTCTRLACTIVEDEST," << _nodeid <<
        "," << _mypolicy_connections[record.target].halt_active << "," << record.target << endl;
        _parent->debug_trace_file << _cur_time << ",HOSTCTRLHALTDEST," << _nodeid <<
        "," << _mypolicy_connections[record.target].halt_state << "," << record.target << endl;
        _parent->debug_trace_file << _cur_time << ",HOSTCTRLALLOWANCEDEST," << _nodeid <<
        "," << _mypolicy_connections[record.target].send_allowance_counter_size << "," << record.target
        << endl;
    }
    return;
  }

  bool duplicate_ack = my_state->last_valid_ack_seq_num_recvd >= record.ack_seq_num;

  // It's not duplicate, but there should be a forced duplicate right before
  bool partial_incremented_ack = record.ack_seq_num == record.nack_seq_num && !duplicate_ack;
  bool congested = record.ack_seq_num == record.nack_seq_num;

  if (duplicate_ack && !record.is_standalone &&
      (record.ack_seq_num != record.nack_seq_num)){
       // If it's a piggy back ack, ignore it if it's a duplicate ack
       // Unless ack_seq_num == nack_seq_num, then it's still valid
       return;
  }


  if (_trace_debug){
    _parent->debug_trace_file << _cur_time << ",HOSTCTRLDUP," << _nodeid << "," <<
       duplicate_ack << endl;
  }
  if (!duplicate_ack && !to_be_acked_packets_contain_put(record.target, record.ack_seq_num)) {
    // We got a normal ack but not a put one
    return;
  }

  int before_halt_state = my_state->halt_state;
  int before_halt_active = my_state->halt_active;
  int before_counter_size = my_state->send_allowance_counter_size;


  if (my_state->halt_active) {
    // Flood gate active

    if (my_state->halt_state > 0){
        // Flood gate slightly open
      if (duplicate_ack){
        //Received duplicate ack
        if(my_state->halt_state > 1){
          // If it is speeding up, should slow down immediately
          // Don't immediately if it's halt state 1 because of Dack suppressing
          //my_state->send_allowance_counter = 0;
        }
        my_state->halt_state = -1;
      } else { //Incremented ACK
        if (partial_incremented_ack){
          // nack == ack , congested
          // There is a forced duplicate ack right before so halt_state should be 0
          my_state->halt_state = 0;
        } else {
            //Received normal ack
            if (my_state->halt_state == _mypolicy_constant.max_packet_send_per_ack){
              // Opening floodgate
              my_state->halt_active = false;
              //my_state->send_allowance_counter = 0;
            } else {
              my_state->halt_state ++;
            }
        }
      }
    } else {
      if (duplicate_ack){
        if (my_state->halt_state > _mypolicy_constant.max_ack_before_send_packet){
          my_state->halt_state--;
        }
      } else if (partial_incremented_ack){
        my_state->halt_state = 0;
      } else {
            //// Packets sent after NACK should not speed up send rate
            my_state->halt_state ++;
      }
    }

    if (!duplicate_ack) {
      if (my_state->halt_state > 0){
        //my_state->send_allowance_counter = (my_state->halt_state + 1);
        my_state->send_allowance_counter_size += 2 * acked_size;
      } else {
        my_state->send_allowance_counter_size += acked_size;
      }
    } else {
      if (_mypolicy_constant.speculative_ack_enabled){
        if (record.is_standalone  &&
            _retry_state_tracker[record.target].dest_retry_state == NACK_BASED){
          my_state->send_allowance_counter_size += acked_size;
        }
      }
    }
  } else {
    // Flood gate opened
    if (duplicate_ack && congested){
        my_state->halt_active = true;
        my_state->halt_state = -1;
    } else if (partial_incremented_ack){
        my_state->halt_active = true;
        my_state->halt_state = 0;
        my_state->send_allowance_counter_size += acked_size;
    } else {

    }
  }

    if (_trace_debug){
      if (my_state->halt_state != before_halt_state){
        _parent->debug_trace_file << _cur_time << ",HOSTCTRLHALTDEST," << _nodeid <<
        "," << _mypolicy_connections[record.target].halt_state << "," << record.target << endl;
      }
      if (my_state->halt_active != before_halt_active){
        _parent->debug_trace_file << _cur_time << ",HOSTCTRLACTIVEDEST," << _nodeid <<
        "," << _mypolicy_connections[record.target].halt_active << "," << record.target << endl;
      }

      if (my_state->send_allowance_counter_size != before_counter_size){
        _parent->debug_trace_file << _cur_time << ",HOSTCTRLALLOWANCEDEST," << _nodeid <<
        "," << _mypolicy_connections[record.target].send_allowance_counter_size << "," << record.target
        << endl;
      }
    }

  my_state->last_valid_ack_seq_num_recvd  = record.ack_seq_num;
}

void EndPoint::process_pending_inbound_response_queue() {
  while (!_pending_inbound_response_queue.empty()) {
    // Process acks in order.  If we encounter a flit that isn't ready, do not
    // process any younger flits, and instead bail out of the loop.
    if (_pending_inbound_response_queue.front().time > _cur_time) {
      break;
    }
    pending_rsp_record record = _pending_inbound_response_queue.front();
    if (record.type == Flit::READ_REPLY) {
      mark_response_received_in_opb(record.source, record.req_seq_num);
    } else if (record.type == Flit::RGET_GET_REQUEST) {
      mark_response_received_in_opb(record.source, record.req_seq_num);
    } else {
      cout << _cur_time << ": " << Name() << ": ERROR!!!  process_pending_inbound_response_queue() "
           << "saw unexpected response type: " << flit_type_strings[record.type] << endl;
      exit(1);
    }
    _pending_inbound_response_queue.pop_front();
  }
}

void EndPoint::update_dequeued_state(put_wait_queue_record r) {

  assert(r.remaining_process_size == 0.0);
  if (_mypolicy_endpoint.acked_data_in_queue >= r.size){
     // This packet has already been acked. So just need to take it out
     _mypolicy_endpoint.acked_data_in_queue -= r.size;
  } else {
    // This packet has not been acked, so still need acked even though it's dequeued
    _mypolicy_endpoint.data_dequeued_but_need_acked += r.size;
  }

  _put_buffer_meta.remaining += r.size;
  if (_trace_debug){
    _parent->debug_trace_file << _cur_time << ",DEQPUTDEST," << _nodeid << "," << r.size
      << "," << r.src << endl;
  }

  if (_mypolicy_constant.policy == HC_MY_POLICY) {
    shift_load_balance_queue_to_data_queue_if_needed();
  }
  _packets_dequeued++;

  mypolicy_update_periodic_occupancy(NULL, &r);

  if (_trace_debug){
    _parent->debug_trace_file << _cur_time << ",PUTQDEPTH," << _nodeid << "," << _put_buffer_meta.queue_size - _put_buffer_meta.remaining<< endl;
    _parent->debug_trace_file << _cur_time << ",PUTDEQACCUM," << _nodeid << "," << _packets_dequeued << endl;
  }

  if (_debug_enabled) {
      cout << _cur_time << ": " << Name() << ": Dequeuing from put put queue, new depth: " <<
                _put_buffer_meta.queue_size - _put_buffer_meta.remaining << endl;
  }

}

void EndPoint::update_host_bandwidth(){
    // Change logic of host congestion in this function

  double fast = _mypolicy_constant.host_bandwidth_high;
  double slow = _mypolicy_constant.host_bandwidth_low;

  //double fast = 100.0 * 2.5 / (gFlitSize * 8);
  //double slow = 20.0 * 2.5 / (gFlitSize * 8);

  if (_cur_time >= _mypolicy_endpoint.next_change_bandwidth_time){

    if (_mypolicy_endpoint.host_congestion_enabled){
      if(_mypolicy_endpoint.host_bandwidth_is_slow){
        _mypolicy_endpoint.current_host_bandwith = fast;
      } else {
        _mypolicy_endpoint.current_host_bandwith = slow;
      }
    } else {
      _mypolicy_endpoint.current_host_bandwith = fast;
    }
    double gen = (*_mypolicy_endpoint.interarrival_logn)(_mypolicy_endpoint.generator);

    // TODO: bad. hardcoded at the moment
    //int change_interval = 5000;

    if (_mypolicy_endpoint.host_bandwidth_is_slow) {
      // time to drain
      _mypolicy_endpoint.next_change_bandwidth_time = _cur_time + 2000;
    }
    else {
      // Time to build up
      _mypolicy_endpoint.next_change_bandwidth_time = _cur_time + 10000;
    }

    if (_trace_debug){
      _parent->debug_trace_file << _cur_time << ",HOSTBAND," << _nodeid << "," <<
        (int)(_mypolicy_endpoint.current_host_bandwith * (32 * 8) / 2.5)<< endl;
    }

    assert( ceil(gen) > 0 );
    _mypolicy_endpoint.host_bandwidth_is_slow = !_mypolicy_endpoint.host_bandwidth_is_slow;
  }

}

void EndPoint::process_put_queue() {

  double this_cycle_processing_power = _mypolicy_endpoint.current_host_bandwith;

  while(_put_buffer_meta.queue.size() && this_cycle_processing_power > 0.0){
      put_wait_queue_record r = _put_buffer_meta.queue.front();
      _put_buffer_meta.queue.pop_front();
      double remaining = r.remaining_process_size;

      double processing_power_used =
        (remaining > this_cycle_processing_power)? this_cycle_processing_power: remaining;

      // New processing power after this packet
      this_cycle_processing_power -= processing_power_used;

      double new_remaining = remaining - processing_power_used;
      r.remaining_process_size = new_remaining;

      if (new_remaining > 0.0) {
        // Still have something left, put back in queue
        _put_buffer_meta.queue.push_front(r);
        assert(this_cycle_processing_power == 0.0);
        } else {

        if (_mypolicy_constant.policy == HC_HOMA_POLICY){
          HomaAckQueueRecord(r);
            if (_trace_debug){
              _parent->debug_trace_file << _cur_time << ",PUTQDEPTH," << _nodeid << "," << _put_buffer_meta.queue_size - _put_buffer_meta.remaining<< endl;
            }
        } else {
          //Should dequeue
          update_dequeued_state(r);
        }
      }
  }
}

void EndPoint::process_delayed_ack_if_needed() {
    /*
     *
     * Check aknowledgement queue to see if there're acks ready to be sent piggybacked
     * Standalone ack variables nalso need to be updated so it would send back standalone ack if waited too long
     */

    bool finish = false;
    do {

        if (_mypolicy_endpoint.ack_queue.size() > 0) {

          to_send_ack_queue_record r = _mypolicy_endpoint.ack_queue.front();

          /*
           * Condition 1: timer expired for ack
           * Condition 2: something finishes processing, so acked data is less than threshold
           * Condition 3: below threshold, ack everything
           * Condition 4: someone is retransmistting, delay ack as much as posible (not used)
           * Condition 5: dequeued data waiting to be acked
           */

          bool cond_1 = _cur_time >= r.latest_time_to_ack;
          bool cond_2 = (occupied_size() > _mypolicy_constant.delayed_ack_threshold &&
                 _mypolicy_endpoint.acked_data_in_queue <
                 _mypolicy_constant.delayed_ack_threshold);
          bool cond_3 = (occupied_size() < _mypolicy_constant.delayed_ack_threshold);
          bool cond_5 = _mypolicy_endpoint.data_dequeued_but_need_acked > 0;
          bool cond_6 = (r.type == Flit::WRITE_REQUEST || r.type == Flit::RGET_GET_REPLY
        || r.type == Flit::READ_REPLY );

          assert(!cond_1);
          if ( cond_1 || !cond_6 || (cond_2 || cond_3 || cond_5)){

            if (cond_6){
                if (cond_5) {
                    assert(_mypolicy_endpoint.acked_data_in_queue == 0);
                    _mypolicy_endpoint.data_dequeued_but_need_acked -= r.size;
                } else {
                    _mypolicy_endpoint.acked_data_in_queue += r.size;
                }
            }

            if (_debug_enabled) {
                cout << _cur_time << ": " << Name() << ": Dequeing from ack queue. seq_num: " <<
                  r.seq_num << " from source: " <<
                  r.source << endl;
            }
            if (_trace_debug){
              _parent->debug_trace_file << _cur_time << ",ACKDEQ," << _nodeid << "," << r.seq_num << endl;
              _parent->debug_trace_file << _cur_time << ",PUTQACKED," << _nodeid <<
                "," << _mypolicy_endpoint.acked_data_in_queue << endl;
            }

            _mypolicy_endpoint.ack_queue.pop_front();

          //// Updating last_index_in_ack_queue for all initiators because we just dequeued
          //for(unsigned int initiator = 0; initiator < _endpoints; initiator++) {
            //if (_mypolicy_connections[initiator].last_index_in_ack_queue >= 0){
              //_mypolicy_connections[initiator].last_index_in_ack_queue--;
            //}
          //}

            if (r.seq_num >= _ack_response_state[r.source].last_valid_seq_num_recvd_and_ackd){
              //^ This check may be unnecessary. Seq num should always be increasing
              bool update_ack_seq_num = false;
              // Logic for coalescing request
              // If we're coalescing,
              // we don't update seq_num until the target packet has finished
              update_ack_seq_num = true;

              if (update_ack_seq_num){

                bool should_give_ack_other_dest = false;
                int other_source, other_size = 0;
                if (_mypolicy_constant.speculative_ack_enabled){
                  while (_mypolicy_endpoint.speculative_ack_queue.size()){
                    other_source = _mypolicy_endpoint.speculative_ack_queue.front().source;
                    if (other_source != r.source && !_mypolicy_connections[other_source].speculative_ack_allowance_size){
                        int other_occupancy = _mypolicy_connections[other_source].periodic_ack_occupancy;
                        int this_occupancy = _mypolicy_connections[r.source].periodic_ack_occupancy;
                        if (this_occupancy > other_occupancy){
                          should_give_ack_other_dest = true;
                          other_size = _mypolicy_endpoint.speculative_ack_queue.front().size;
                          break;
                        } else {
                          _mypolicy_endpoint.speculative_ack_queue.pop_front();
                        }
                    } else {
                        _mypolicy_endpoint.speculative_ack_queue.pop_front();
                    }
                  }
                }

                  _ack_response_state[r.source].last_valid_seq_num_recvd_and_ready_to_ack = r.seq_num;
                  if (_ack_response_state[r.source].packets_recvd_since_last_ack == 0){
                    // If already sent out all acks, then directly update ackd variable
                    assert(r.seq_num > _ack_response_state[r.source].last_valid_seq_num_recvd_and_ackd);
                    _ack_response_state[r.source].last_valid_seq_num_recvd_and_ackd = r.seq_num;

                    assert(!_ack_response_state[r.source].packets_recvd_since_last_ack);
                    if (should_give_ack_other_dest){
                      _mypolicy_endpoint.speculative_ack_queue.pop_front();
                      assert(!_mypolicy_connections[other_source].speculative_ack_allowance_size);
                      _mypolicy_connections[other_source].speculative_ack_allowance_size += other_size;
                      if (_mypolicy_connections[r.source].earliest_accum_ack_shared_time == -1){
                        _mypolicy_connections[r.source].earliest_accum_ack_shared_time = _cur_time;
                      }
                    } else {
                      mypolicy_update_periodic_ack_occupancy(r.source,  r.size);
                      _mypolicy_connections[r.source].earliest_accum_ack_shared_time = -1;
                      _ack_response_state[r.source].packets_recvd_since_last_ack++;
                      if (_ack_response_state[r.source].time_last_valid_unacked_packet_recvd == 999999999) {
                        _ack_response_state[r.source].time_last_valid_unacked_packet_recvd = _cur_time;
                      }
                    }
                  } else {
                    mypolicy_update_periodic_ack_occupancy(r.source,  r.size);
                    _mypolicy_connections[r.source].earliest_accum_ack_shared_time = -1;
                    // If there's still acked pending. Just let it play out
                    _ack_response_state[r.source].packets_recvd_since_last_ack++;
                    if (_ack_response_state[r.source].time_last_valid_unacked_packet_recvd == 999999999) {
                      _ack_response_state[r.source].time_last_valid_unacked_packet_recvd = _cur_time;
                    }
                  }

                  if (_debug_enabled) {
                      cout << _cur_time << ": " << Name() << ": Updated seq num recvd and ackd from " <<
                        r.source << " to " << r.seq_num << endl;
                  }
              }
            } else {
              assert(false && "seq number not at least the same as "
                  "_ack_response_state[r.source].last_valid_seq_num_recvd_and_ready_to_ack");
            }

            if (_debug_enabled) {
                cout << _cur_time << ": " << Name() << ": Dequeuing from ack queue w/ seq_num " <<
                  r.seq_num << " from source: " <<
                    r.source << " remaining queue_size: " << _mypolicy_endpoint.ack_queue.size() << endl;
            }
          } else {
            finish = true;
          }
        } else {
            finish = true;
        }
    } while (!finish);

}

void EndPoint::process_pending_outbound_response_queue() {
  while (!_pending_outbound_response_queue.empty()) {
    // Process acks in order.  If we encounter a flit that isn't ready, do not
    // process any younger flits, and instead bail out of the loop.
    if (_pending_outbound_response_queue.front().time > _cur_time) {
      break;
    }
    pending_rsp_record record = _pending_outbound_response_queue.front();
//    assert(record.time == _cur_time);
    if (record.type == Flit::READ_REPLY) {
      GeneratePacketFlits(record.source, record.type, record.reply_size,
                          record.time, record.record, record.cl, record.req_seq_num,
                          record.rget_data_size, record.data,
                          &_repliesPending[record.source], record.watch);
    } else if (record.type == Flit::RGET_GET_REQUEST) {
      GeneratePacketFlits(record.source, record.type, record.reply_size,
                          record.time, record.record, record.cl, record.req_seq_num,
                          record.rget_data_size, record.data,
                          &_rget_get_req_queues[record.source], record.watch);
    } else if (record.type == Flit::RGET_GET_REPLY) {
      GeneratePacketFlits(record.source, record.type, record.reply_size,
                          record.time, record.record, record.cl, record.req_seq_num,
                          0, record.data,
                          &_repliesPending[record.source], record.watch);
    }
    _pending_outbound_response_queue.pop_front();
  }
}


void EndPoint::sender_process_ecn() {
  if (_mypolicy_constant.policy == HC_ECN_POLICY) {

    if(_cur_time > _mypolicy_endpoint.ecn_next_check_period) {

      for(unsigned int target = 0; target < _endpoints; target++) {
        _mypolicy_connections[target].ecn_running_percent *= (1.0 - _mypolicy_constant.ecn_param_g);
      }

      _mypolicy_endpoint.ecn_next_check_period = _cur_time + _mypolicy_constant.ecn_period;

      for(unsigned int target = 0; target < _endpoints; target++) {
          int this_total = _mypolicy_connections[target].ecn_total;
          if (this_total){
            double this_count = (double) _mypolicy_connections[target].ecn_count;
            double new_percent = this_count / (double) this_total;
            _mypolicy_connections[target].ecn_running_percent += new_percent * _mypolicy_constant.ecn_param_g;

            double old_cwd = _mypolicy_connections[target].tcplikepolicy_cwd;
            _mypolicy_connections[target].tcplikepolicy_ssthresh =
                _mypolicy_connections[target].tcplikepolicy_ssthresh * (1 - _mypolicy_connections[target].ecn_running_percent / 2);
            _mypolicy_connections[target].tcplikepolicy_cwd =
                _mypolicy_connections[target].tcplikepolicy_cwd * (1 - _mypolicy_connections[target].ecn_running_percent);

            _mypolicy_connections[target].tcplikepolicy_cwd = max(_mypolicy_connections[target].tcplikepolicy_cwd, _mypolicy_constant.tcplikepolicy_MSS);
            _mypolicy_connections[target].tcplikepolicy_ssthresh = max(_mypolicy_connections[target].tcplikepolicy_ssthresh, _xaction_size_limit_per_dest);


            if (old_cwd != _mypolicy_connections[target].tcplikepolicy_cwd) {
              _parent->debug_trace_file << _cur_time << ",CWD," << _nodeid << "," << _mypolicy_connections[target].tcplikepolicy_cwd << "," <<
                target << endl;

            }

          }
        _mypolicy_connections[target].ecn_total = 0;
        _mypolicy_connections[target].ecn_count = 0;

      }
    }
  }
}

void EndPoint::process_received_acks(recvd_ack_record record) {
  // In the context of this endpoint looking to process acks received and match
  // them up to packets in its OPB, the endpoint sending the ack is considered
  // the target of the original transaction.
  int target = record.target;
  int ack_seq_num = record.ack_seq_num;
  int nack_seq_num = record.nack_seq_num;
  int flit_id = record.flit_id;

  // The sequence number returned in either an ack or nack indicates all packets
  // up to and including the sequence number were received correctly.  (Yes,
  // even in the nack case.)  Only one or the other can be set.
  // If both are the same, it means
  if ((ack_seq_num != -1) && (nack_seq_num != -1) &&
      (ack_seq_num != nack_seq_num && HC_MY_POLICY == _mypolicy_constant.policy)) { cout << _cur_time << ": " << Name() << ": ERROR: Received packet with both ACK "
         << "and NACK fields set!" << endl;
    exit(1);
  }

  int packet_size;

  if (_mypolicy_constant.policy == HC_MY_POLICY) {
      packet_size = calculate_to_be_acked_packet_size(target, ack_seq_num, true);
      mypolicy_update_initiator_state_with_ack(record, packet_size);
  } else {
      packet_size = calculate_to_be_acked_packet_size(target, ack_seq_num, false);
  }

  if (ack_seq_num != -1) {
    if (_debug_enabled) {
      cout << _cur_time << ": " << Name() << ": Received ACK seq_num: " << ack_seq_num
           << " from node " << target << " on flit: " << flit_id << " packet allowance: " <<
           _mypolicy_connections[target].send_allowance_counter_size << " halt active: " <<
           _mypolicy_connections[target].halt_active << endl;
    }

    if (_trace_debug){
      if(record.is_standalone){
        //_parent->debug_trace_file << _cur_time << ",ACKALONER," << _nodeid << "," << ack_seq_num << endl;
        if (ack_seq_num == nack_seq_num){
          _parent->debug_trace_file << _cur_time << ",ACKALONEDDESTR," << _nodeid << "," << ack_seq_num <<
            "," << target << endl;
        } else {
            _parent->debug_trace_file << _cur_time << ",ACKALONEDESTR," << _nodeid << "," << ack_seq_num <<
            "," << target << endl;
        }
      } else {
        if (ack_seq_num == nack_seq_num){
            //_parent->debug_trace_file << _cur_time << ",ACKPIGDR," << _nodeid << "," << ack_seq_num << endl;
            _parent->debug_trace_file << _cur_time << ",ACKPIGDDESTR," << _nodeid << "," << ack_seq_num <<
              "," << target << endl;
        } else {
            //_parent->debug_trace_file << _cur_time << ",ACKPIGR," << _nodeid << "," << ack_seq_num << endl;
            _parent->debug_trace_file << _cur_time << ",ACKPIGDESTR," << _nodeid << "," << ack_seq_num <<
              "," << target << endl;

        }
      }
    }

    clear_opb_of_acked_packets(target, ack_seq_num);

    if (_mypolicy_constant.policy == HC_MY_POLICY) {
      if (_outstanding_packet_buffer[target].empty()){
        _mypolicy_connections[target].must_retry_at_least_one_packet = true;
      }
    }

    if (_mypolicy_constant.policy == HC_TCP_LIKE_POLICY || _mypolicy_constant.policy == HC_ECN_POLICY){
      if (_mypolicy_connections[target].tcplikepolicy_cwd <
          _mypolicy_connections[target].tcplikepolicy_ssthresh) {
        // Slow start phase
        _mypolicy_connections[target].tcplikepolicy_cwd +=
          (unsigned int) min(packet_size, (int)_mypolicy_constant.tcplikepolicy_MSS);

        if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",CWD," << _nodeid << "," <<
            _mypolicy_connections[target].tcplikepolicy_cwd << "," << target<< endl;
        }
      } else {
        // Congestion avoidance phase
        _mypolicy_connections[target].tcplikepolicy_cwd +=
            _mypolicy_constant.tcplikepolicy_MSS * _mypolicy_constant.tcplikepolicy_MSS /
            _mypolicy_connections[target].tcplikepolicy_cwd;

        if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",CWD," << _nodeid << "," <<
            _mypolicy_connections[target].tcplikepolicy_cwd << "," << target << endl;
        }
      }

    }


    if (record.sack) {
      if (_debug_sack) {
        cout << GetSimTime() << ": " << Name() << ": Received SACK with ACK " << ack_seq_num
             << " and sack_vec: 0x" << hex << record.sack_vec << dec << " from node " << target
             << ". Previous retry state for dest node " << target << ": " << _retry_state_tracker[target].dest_retry_state
             << ", retry seq_num_in_progress: " << _retry_state_tracker[target].seq_num_in_progress << endl;
      }
      if (_trace_debug){
        _parent->debug_trace_file << _cur_time << ",SACKR," << _nodeid << "," << endl;
      }


      // Clear individual entries from the OPB based on the SACK vector.
      // FIXME: Maybe do this later... it shouldn't hurt anything to leave them there for now, but does prevent new transactions from being sent.


      // If there is already a SACK in-progress, then merge the new vector in.
      if (_retry_state_tracker[target].sack) {
        if (_debug_sack) {
          cout << GetSimTime() << ": " << Name() << ": Merging new sack vector (0x" << hex
               << record.sack_vec << dec << ") with base seq_num: " << (ack_seq_num + 1) << " into in-progress sack (0x"
               << hex << _retry_state_tracker[target].sack_vec << dec << ") with base seq_num: "
               << _retry_state_tracker[target].seq_num_in_progress << endl;
        }
        int base_seq_diff = (ack_seq_num + 1) - _retry_state_tracker[target].seq_num_in_progress;

        unsigned long shifted_new_vec = record.sack_vec;
        if (base_seq_diff > 0) {
          shifted_new_vec = record.sack_vec << base_seq_diff;
          // OR-in the lower bits of the old sack_vec that were not included in the new sack_vec.
          // All of the seq_nums that are lower than ack_seq_num need their
          // sack_vec bits set to 1 since they were received, as indicated by
          // the new ack_seq_num.  (Code below will make sure to leave the
          // packet currently being retransmitted.)
          unsigned int mask = ((unsigned int)1 << base_seq_diff) - 1;
          shifted_new_vec |= mask;
        } else if (base_seq_diff < 0) {
          shifted_new_vec = record.sack_vec >> -base_seq_diff;
        }

        // Check that the new vector doesn't try to clear any bits from the old
        if (((~shifted_new_vec) & _retry_state_tracker[target].sack_vec) != 0) {
          cout << GetSimTime() << ": " << Name() << ": ERROR: Received a new sack_vec from node "
               << target << ", but previously acked packets are now un-acked!  old seq_num_in_progress: "
               << _retry_state_tracker[target].seq_num_in_progress << ", old sack_vec: 0x" << hex
               << _retry_state_tracker[target].sack_vec << dec << ", new base seq_num: "
               << ack_seq_num << ", new sack_vec: 0x" << hex << record.sack_vec << ", shifted new sack_vec: 0x"
               << shifted_new_vec << dec << endl;
          exit(1);
        }


        // Clear the newly acked packets from the OPB.
        unsigned int newly_acked_vec = (~_retry_state_tracker[target].sack_vec) & shifted_new_vec;
//        cout << _cur_time << ": " << Name() << ": recvd sack_vec: 0x" << hex << record.sack_vec << dec
//             << ", ack_seq_num: " << record.ack_seq_num << endl
//             << "        in prog sack_vec: 0x" << hex << _retry_state_tracker[target].sack_vec << dec << ", in prog seq_num: "
//             << _retry_state_tracker[target].seq_num_in_progress << endl
//             << ", shifted_new_vec: 0x" << hex << shifted_new_vec << dec << ", newly_acked_vec: 0x" << hex << newly_acked_vec << dec << endl;
        unsigned int vec_idx = 1; // skip the first bit since that is the seq_num currently being retransmitted
        while (vec_idx <= _sack_vec_length) {
          if ((newly_acked_vec >> vec_idx) & 1ul) {
            cout << _cur_time << ": " << Name() << ": Clearing newly acked seq_num " << (_retry_state_tracker[target].seq_num_in_progress + vec_idx)
                 << " from OPB due to sack from node " << target << ". ack_seq_num: " << _retry_state_tracker[target].seq_num_in_progress
                 << ", sack_vec: 0x" << hex << record.sack_vec << dec << endl;
            clear_opb_of_single_packet(target, (_retry_state_tracker[target].seq_num_in_progress + vec_idx));
          }
          ++vec_idx;
        }


        // Update the old vector with the new, but don't clear the one in progress.
        if ((shifted_new_vec & 0xfffffffffffffffe) != (_retry_state_tracker[target].sack_vec & 0xfffffffffffffffe)) {
          if (_debug_sack) {
            cout << GetSimTime() << ": " << Name() << ": Received new sack_vec from node " << target
                 << " while sack retry in progress. Updating old sack_vec from: 0x" << hex
                 << _retry_state_tracker[target].sack_vec;
          }
          _retry_state_tracker[target].sack_vec |= (shifted_new_vec & 0xfffffffffffffffe);
          if (_debug_sack) {
            cout << " to: 0x" << _retry_state_tracker[target].sack_vec << dec << endl;
          }
        }

      }
      else {
        unsigned int opb_idx = 0;
        bool found = false;
        while ((opb_idx < _outstanding_packet_buffer[target].size()) &&
               !found) {
          if (_outstanding_packet_buffer[target][opb_idx]->head &&
              (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num == (ack_seq_num + 1))) {
            // Set the index of the first packet to retransmit.
            _retry_state_tracker[target].seq_num_in_progress = ack_seq_num + 1;
            _retry_state_tracker[target].nack_replay_opb_flit_index = opb_idx;
            _retry_state_tracker[target].sack = true;
            _retry_state_tracker[target].orig_sack_vec = record.sack_vec;
            _retry_state_tracker[target].sack_vec = record.sack_vec;
            _retry_state_tracker[target].orig_ack_seq_num = record.ack_seq_num;
            found = true;
//            if (_debug_enabled) {
            if (_debug_sack) {
              cout << _cur_time << ": " << Name() << ": Setting SACK replay index to " << opb_idx
                   << " for dest node " << target << endl;
            }
//            }
          }
          opb_idx++;
        }

        // Only set up the retry if we found an eligible packet in the OPB.
        if (found) {
          _pending_nack_replays.push_back(target);
          // SACKs behave very similarly to NACKs.
          _retry_state_tracker[target].dest_retry_state = NACK_BASED;
          if (_trace_debug){
              _parent->debug_trace_file << _cur_time << ",RETRYSTATE," << _nodeid << "," << _retry_state_tracker[target].dest_retry_state << endl;
          }
        } else {
          cout << _cur_time << ": " << Name() << ": Could not find a matching opb entry with seq_num "
               << ack_seq_num + 1 << " from node " << target << ", triggered by received SACK." << endl;
          exit(1);
        }


        // Clear all of the ACKed packets in the vector
        unsigned int vec_idx = 1;
        while (vec_idx <= _sack_vec_length) {
          if ((record.sack_vec >> vec_idx) & 1ul) {
//            cout << _cur_time << ": " << Name() << ": Clearing seq_num " << (record.ack_seq_num + vec_idx + 1)
//                 << " from OPB due to sack from node " << target << ". ack_seq_num: " << record.ack_seq_num
//                 << ", sack_vec: 0x" << hex << record.sack_vec << dec << endl;
            clear_opb_of_single_packet(target, (record.ack_seq_num + vec_idx + 1));
          }
          ++vec_idx;
        }

      }
    }

  } else if (nack_seq_num != -1 && ack_seq_num != nack_seq_num) {
    if (_trace_debug){
      if(record.is_standalone){
        _parent->debug_trace_file << _cur_time << ",NACKALONEDESTR," << _nodeid << "," << nack_seq_num <<
        "," << target << endl;
      } else {
        _parent->debug_trace_file << _cur_time << ",NACKPIGDESTR," << _nodeid << "," << nack_seq_num <<
        "," << target << endl;
      }
    }

    if (_mypolicy_constant.policy == HC_TCP_LIKE_POLICY || _mypolicy_constant.policy == HC_ECN_POLICY){
      _mypolicy_connections[target].tcplikepolicy_ssthresh =
          _mypolicy_connections[target].tcplikepolicy_cwd >> 1;
      _mypolicy_connections[target].tcplikepolicy_cwd =
          _mypolicy_connections[target].tcplikepolicy_cwd >> 1;

      if (_trace_debug){
        _parent->debug_trace_file << _cur_time << ",CWD," << _nodeid << "," <<
          _mypolicy_connections[target].tcplikepolicy_cwd << "," << target << endl;
      }
    }

    // If we already processing a NACK-based replay, then clear the OPB as
    // indicated by this new NACK, but don't queue up another replay.
    if (_retry_state_tracker[target].dest_retry_state == NACK_BASED
        ) {
      cout << _cur_time << ": " << Name() << ": Received NACK from dest: " << target
           << " while a NACK-based replay was already in progress.  seq_num: "
           << nack_seq_num << " on flit: " << flit_id << "first flit in OPB seq num " <<
            _outstanding_packet_buffer[target][_retry_state_tracker[target].nack_replay_opb_flit_index]->packet_seq_num
           << ".  Dropping NACK (unless both duplicate ACK signified by equal NACK and ACk seq num)." << endl;

      if (_mypolicy_constant.policy != HC_MY_POLICY) {
          clear_opb_of_acked_packets(target, nack_seq_num, false);
          _mypolicy_connections[target].pending_nack_seq_num = nack_seq_num;
      } else {
        // We can't clear it up because I need OPB information to decide whether I should allow more packets to be transmitted
        _mypolicy_connections[target].pending_nack_seq_num = nack_seq_num;
      }
    }
    // If packets are not currently timing-out, then start a NACK-based replay
    // and clear the OPB of packets ACKed by this NACK.
    // (If packets *are* currently timing-out, then we drop the NACK action
    // entirely.)
    else if (_retry_state_tracker[target].dest_retry_state != TIMEOUT_BASED) {
      if (_debug_enabled) {
        cout << _cur_time << ": " << Name() << ": Received NACK seq_num: " << nack_seq_num
             << " from dest: " << target << " on flit: " << flit_id << endl;
      }
      if (_trace_debug){
        _parent->debug_trace_file << _cur_time << ",REVNACK," << _nodeid << "," << nack_seq_num << "," << target << endl;
      }

      if (_mypolicy_constant.policy != HC_MY_POLICY) {
          // Clear the OPB first.
          clear_opb_of_acked_packets(target, nack_seq_num, false);
      } else {
        // May be we want to clear it as well, but may not matter
      }

      // Why are we pending this NACK here?
      // The reason is because there may be other nack-based retries ahead of
      // this one, or maybe we've already selected a flit for transmission.
      // Also, we don't remove a nack from the pending_nack_replays list until
      // the replay is complete, so even if there's nothing else involved, we
      // still want to put this NACK on the list.
      // So we handle an NACK in 2 steps:
      //   1. Clear the OPB according to seq_nums.  (Done above.)
      //   2. Queue up the NACK-based retry.
      _pending_nack_replays.push_back(target);
      _retry_state_tracker[target].dest_retry_state = NACK_BASED;
      if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",RETRYSTATE," << _nodeid << "," << _retry_state_tracker[target].dest_retry_state << endl;
      }
      _retry_state_tracker[target].nack_replay_opb_flit_index =
          find_opb_idx_of_seq_num_head(target, nack_seq_num + 1);

      if (_retry_state_tracker[target].nack_replay_opb_flit_index == -1) {
        cout << _cur_time << ": " << Name() << ": ERROR: Could not find a matching opb entry with seq_num "
             << nack_seq_num + 1 << ", triggered by received NACK." << endl;
        exit(1);
      }
    }
  }
}

bool EndPoint::to_be_acked_packets_contain_put(int target, int seq_num_acked, bool nack_initiated) {

  unsigned int opb_idx = 0;
  while ((opb_idx < _outstanding_packet_buffer[target].size()) &&
         (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num <= seq_num_acked)) {

    // Janky way to do this
    if (
        _outstanding_packet_buffer[target][opb_idx]->type == Flit::RGET_GET_REPLY ||
        _outstanding_packet_buffer[target][opb_idx]->type == Flit::WRITE_REQUEST ||
        _outstanding_packet_buffer[target][opb_idx]->type == Flit::READ_REPLY
        ){
        return true;
    }
    opb_idx += 1;
  }
  return false;
}

int EndPoint::calculate_to_be_acked_packet_size(int target, int seq_num_acked, bool nack_initiated,
    bool nack_non_zero) {

  if (!nack_non_zero && !nack_initiated && (_retry_state_tracker[target].dest_retry_state == NACK_BASED)) {
    return 0;
  }

  if (nack_initiated && (_retry_state_tracker[target].dest_retry_state == NACK_BASED)) {
      int accum_packet_size = 0;
      unsigned int opb_idx = 0;
      while ((opb_idx < _outstanding_packet_buffer[target].size()) &&
             (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num <= seq_num_acked)) {
        accum_packet_size = 0;
        opb_idx += calculate_to_be_acked_packet_size_by_index(target, opb_idx, seq_num_acked, accum_packet_size);
      }
      return accum_packet_size;
  } else {
      int accum_packet_size = 0;
      unsigned int opb_idx = 0;
      while ((opb_idx < _outstanding_packet_buffer[target].size()) &&
             (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num <= seq_num_acked)) {
        opb_idx += calculate_to_be_acked_packet_size_by_index(target, opb_idx, seq_num_acked, accum_packet_size);
      }
      return accum_packet_size;
  }
}

int EndPoint::calculate_to_be_acked_packet_size_by_index(int target, unsigned int opb_idx, unsigned int seq_num_acked, int & accum_packet_size) {
  // TODO: THere is a big bug in this. Nothing's erased as we loop though, so we have to fix this

  // This is modeled entirely after clear_opb_of_packet_by_index
  Flit * flit = _outstanding_packet_buffer[target][opb_idx];

  assert(flit->head);

  if ( ((((flit->type == Flit::READ_REQUEST) ||
        (flit->type == Flit::RGET_REQUEST)) && (flit->response_received)) ||
        ((flit->type != Flit::READ_REQUEST) && (flit->type != Flit::RGET_REQUEST))) &&

       ((_timedout_packet_retransmit_in_progress.dest != target) ||
        ((_timedout_packet_retransmit_in_progress.dest == target) &&
         (flit->packet_seq_num < _timedout_packet_retransmit_in_progress.seq_num)))) {

    if (((flit->type == Flit::READ_REQUEST) ||
         (flit->type == Flit::RGET_GET_REQUEST)) &&
        (!flit->ack_received)) {
    }
    accum_packet_size += flit->size;
    return flit->size;
  } else {
    if ((_timedout_packet_retransmit_in_progress.dest == target) &&
        (flit->packet_seq_num == _timedout_packet_retransmit_in_progress.seq_num) &&
        ((int)seq_num_acked >= flit->packet_seq_num)) {
      if (flit->head) {
        accum_packet_size += flit->size;
        return flit->size;
      } else {
        cout << _cur_time << ": " << Name() << ": ERROR: else condition triggered on non-head flit.  flit id: "
             << flit->id << endl;
        _EndSimulation();
        exit(1);
      }
    // If there isn't a retry in progress, make sure to mark read_requests as being acked.
    // FIXME: Make sure this works in all cases relative to the if condition above.
    } else if ((flit->type == Flit::READ_REQUEST) || (flit->type == Flit::RGET_GET_REQUEST)) {
      return flit->size;
    } else if ((flit->type == Flit::RGET_REQUEST) && (!flit->ack_received)) {
    return flit->size;
    } else {
      if (flit->head) {
        accum_packet_size += flit->size;
        return flit->size;
      } else {
        cout << _cur_time << ": " << Name() << ": ERROR: else condition triggered on non-head flit.  flit id: "
             << flit->id << endl;
        _EndSimulation();
        exit(1);
      }
    }
  }
}

void EndPoint::clear_opb_of_acked_packets(int target, int seq_num_acked, bool nack_initiated) {
  if (seq_num_acked <= 0) {
    return;
  }

  if (_outstanding_packet_buffer.count(target) == 0) {
    // FIXME: This seems possible, so should probably not be an error.
    cout << _cur_time << ": " << Name() << ": ERROR: Received ACK/NACK from node with no outstanding packets in OPB: "
         << target << ", ack_seq_num: " << seq_num_acked << endl;
    DumpOPB();
    exit(1);
  }

  // If we are currently doing a NACK-based replay for the dest that we
  // received the ACK from, then pend the ACK (do not clear the OPB) until the
  // replay is complete.
// But if this is the NACK that started the NACK-based replay, then clear the OPB first.
// maybe rename nack_initiated to pend_clear_if_in_NACK_replay and reverse the polarity.
  if (!nack_initiated && (_retry_state_tracker[target].dest_retry_state == NACK_BASED)) {
    // Overwrite with higher seq_num if a prior ack is already pending
    if (seq_num_acked > _retry_state_tracker[target].pending_ack) {
      _retry_state_tracker[target].pending_ack = seq_num_acked;
    }
    return;
  }


  // Iterate through the OPB and retire all packets with a seq_num <= acked
  // sequence number.  This outer loop iterates over packets, while the inner
  // loop iterates over all flits of a packet.

  // GET transactions require both an ACK, and a GET response.  The response
  // could come much later than the ACK.  Because of this, we can have packets
  // retire from the OPB out of order.
  unsigned int opb_idx = 0;
  while ((opb_idx < _outstanding_packet_buffer[target].size()) &&
         (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num <= seq_num_acked)) {
    opb_idx += check_and_clear_opb_of_packet_by_index(target, opb_idx, seq_num_acked);
  }

  // If we were in the middle of a timeout-based retry, and the ACK that we just
  // processed cleared everything out of the OPB for that target, then the retry
  // is complete, so clear the retry state.

  // It's more complicated now with the READ REQUESTS.  We can't just look for
  // an empty OPB.  We have to look see if any packets still resident are read
  // requests that have been acked.
  if ((_retry_state_tracker[target].dest_retry_state == TIMEOUT_BASED) &&
      (all_packets_in_opb_are_acked(target))) {
    // If the OPB is empty, then the timeout-based retry is complete.
    _retry_state_tracker[target].dest_retry_state = IDLE;

    if (_trace_debug){
        _parent->debug_trace_file << _cur_time << ",RETRYSTATE," << _nodeid << "," << _retry_state_tracker[target].dest_retry_state << endl;
    }

    if (_debug_enabled) {
      cout << _cur_time << ": " << Name() << ": Exited TIMEOUT_BASED recovery mode for dest: " << target << endl;
    }
  }
}


unsigned int EndPoint::check_and_clear_opb_of_packet_by_index(int target, unsigned int opb_idx, unsigned int seq_num_acked) {
  Flit * flit = _outstanding_packet_buffer[target][opb_idx];
  unsigned int incr_amt = 0;
  assert(flit->head);
  unsigned int packet_size = flit->size;

  // Only clear the packet from the OPB if it is not currently being retried
  // (due to a timeout since we took care of NACK-based retries above).
  //
  // Additionally, if there is a retry in progress, we can continue to clear
  // packets as long as they are older (lower seq_num and closer to the head
  // of the list) than the packet being retried.  (So the end result would be
  // that the packet being retried ends up at the head of the OPB list.)
  // If the acked sequence number is higher than the packet currently
  // retrying, then we will pend the ACK until after the retry is complete.
  //
  // Additionally, do not remove packets that are still waiting for a protocol
  // response, but update their ACK state accordingly.  (So the end result of
  // this loop could be that one of these ACKed requests is at the head of the
  // OPB list.)

  // Only clear if not read_request, or if it is a read_request, then it has
  // already received a response.
  if ( ((((flit->type == Flit::READ_REQUEST) ||
          (flit->type == Flit::RGET_REQUEST)) && (flit->response_received)) ||

        ((flit->type != Flit::READ_REQUEST) && (flit->type != Flit::RGET_REQUEST))) &&

       ((_timedout_packet_retransmit_in_progress.dest != target) ||
        ((_timedout_packet_retransmit_in_progress.dest == target) &&
         (flit->packet_seq_num < _timedout_packet_retransmit_in_progress.seq_num)))) {

    if (((flit->type == Flit::READ_REQUEST) ||
         (flit->type == Flit::RGET_GET_REQUEST)) &&
        // Only decrement if the ack hasn't already been received (which could
        // have happened in the else condition below or by a previously-received
        // ACK).
        (!flit->ack_received)) {

      _outstanding_xactions_all_dests_stat--;  // Stat only
      _outstanding_outbound_data_all_dests_stat -= flit->size;  // Stat only

      // No need to set ack_received here since we're just going to clear the packet from the OPB.
    }
    clear_opb_of_packet_by_index(target, opb_idx);
    // No need to increment opb_idx, since the removal of the last packet will
    // place the next one at the same index.

  }

  // This packet is not ready to be removed, either because there is a
  // retransmit in progress and this packet has a higher seq_num than the
  // packet being retransmitted, OR, the packet in the OPB is a request for
  // data that and is waiting for a response.
  // If it's the former, then we need to update the pending ack state to
  // ensure that this packet is removed once the retransmit is complete.
  // If it's the latter, then we need to update the state of the OPB entry
  // to indicate that the ACK has been received and update the expiration
  // time to wait for the response.  Additionally, READ_REQUESTS and
  // RGET_GET_REQUESTS need to decrement the appropriate injection limiters.
  // (Which in all other cases is done when the packet is removed from the OPB
  // in clear_opb_of_packet_by_index.)
  else {
    // If we have a TIMEOUT-based retransmit in progress, and we cleared all of
    // the older packets in the loop above, (so that the retransmitting packet
    // is now at the head of the OPB) we have to allow the in-progress packet to
    // complete, so pend the ack.
    if ((_timedout_packet_retransmit_in_progress.dest == target) &&
        (flit->packet_seq_num == _timedout_packet_retransmit_in_progress.seq_num) &&
        ((int)seq_num_acked >= flit->packet_seq_num)) {

      // Store the ACK for use after the packet has completed retransmission.
      // This stops the clearing of all younger packets too.

      // In the case that there was already an ACK pending, always use the
      // highest numbered packet ack.  (If there was no previous ACK, the
      // pending_ack value will be -1, and the condition will still work.)
      if ((int)seq_num_acked > _retry_state_tracker[target].pending_ack) {
        cout << _cur_time << ": " << Name() << ": Found in-progress retry of dest: "
             << target << " and seq_num: "
             << _timedout_packet_retransmit_in_progress.seq_num
             << " while processing ACK to clear it.  Storing ACK on seq_num: "
             << seq_num_acked << " while retry completes." << endl;
        if (_debug_enabled) {
          DumpOPB();
        }
        _retry_state_tracker[target].pending_ack = seq_num_acked;
      }
//      opb_idx += flit->size;
      incr_amt = flit->size;
    // If there isn't a retry in progress, make sure to mark read_requests as being acked.
    // FIXME: Make sure this works in all cases relative to the if condition above.
    } else if ((flit->type == Flit::READ_REQUEST) || (flit->type == Flit::RGET_GET_REQUEST)) {

      // If an ack was not previously received for this transaction, label it
      // as acked and update all state, including the new expire_time.  (Note
      // that we don't want newer acks to continually reset this time.)
      if (!flit->ack_received) {
        // We know this is a head flit from the assert above.
        _outstanding_xactions_all_dests_stat--;  // Stat only
        _outstanding_outbound_data_all_dests_stat -= flit->size;  // Stat only
        _response_timer_expiration_queue.push_back({_cur_time + _response_timer_timeout,
                                                    flit->dest,
                                                    flit->packet_seq_num});
        while (opb_idx < _outstanding_packet_buffer[target].size()) {
          flit = _outstanding_packet_buffer[target][opb_idx];
          flit->ack_received = true;
          flit->ack_received_time = _cur_time;
          flit->expire_time = _cur_time + _response_timer_timeout;

          // Stop before we get to the next packet.
          if (flit->tail) {
            opb_idx = _outstanding_packet_buffer[target].size();
          } else {
            ++opb_idx;
          }
        }
      }
      incr_amt = packet_size;
    } else if ((flit->type == Flit::RGET_REQUEST) && (!flit->ack_received)) {

      _response_timer_expiration_queue.push_back({_cur_time + _rget_req_pull_timeout,
                                                  flit->dest,
                                                  flit->packet_seq_num});

      while (opb_idx < _outstanding_packet_buffer[target].size()) {
        flit = _outstanding_packet_buffer[target][opb_idx];
        flit->ack_received = true;
        flit->ack_received_time = _cur_time;
        flit->expire_time = _cur_time + _rget_req_pull_timeout;

        // Stop before we get to the next packet.
        if (flit->tail) {
          opb_idx = _outstanding_packet_buffer[target].size();
        } else {
          ++opb_idx;
        }
      }
      incr_amt = packet_size;

    } else {
      if (flit->head) {
        incr_amt = flit->size;
      } else {
        cout << _cur_time << ": " << Name() << ": ERROR: else condition triggered on non-head flit.  flit id: "
             << flit->id << endl;
        _EndSimulation();
        exit(1);
      }
    }
  }

  return incr_amt;
}


bool EndPoint::all_packets_in_opb_are_acked(const int target) {
  if (_outstanding_packet_buffer[target].size() == 0) {
    return true;
  }

  unsigned int opb_idx = 0;
  while ((opb_idx < _outstanding_packet_buffer[target].size())) {
    // Only check the head flit
    if (_outstanding_packet_buffer[target][opb_idx]->head) {

      // If the packet is not expecting a response, but still present in the
      // OPB, then we know it hasn't been acked.
      if ((_outstanding_packet_buffer[target][opb_idx]->type != Flit::READ_REQUEST) &&
          (_outstanding_packet_buffer[target][opb_idx]->type != Flit::RGET_REQUEST) &&
          (_outstanding_packet_buffer[target][opb_idx]->type != Flit::RGET_GET_REQUEST)) {
        return false;
      // If the packet is expecting a response, check the ack_received state.
      } else if (!_outstanding_packet_buffer[target][opb_idx]->ack_received) {
        return false;
      }
    }
    opb_idx++;
  }
  // Didn't find any unacked packets.
  return true;
}


void EndPoint::clear_opb_of_single_packet(int target, int seq_num_acked) {
  if (seq_num_acked <= 0) {
    return;
  }

  if (_outstanding_packet_buffer.count(target) == 0) {
    // FIXME: This seems possible, so should probably not be an error.
    cout << _cur_time << ": " << Name() << ": ERROR: Received ACK/NACK from node with no outstanding packets in OPB: "
         << target << ", ack_seq_num: " << seq_num_acked << endl;
    DumpOPB();
    exit(1);
  }

//  cout << _cur_time << ": " << Name() << ": Clearing OPB of single packet seq_num: " << seq_num_acked << " for node " << target << endl;

  // Iterate through the OPB and retire all packets with a seq_num <= acked
  // sequence number.  This outer loop iterates over packets, while the inner
  // loop iterates over all flits of a packet.

  // GET transactions require both an ACK, and a GET response.  The response
  // could come much later than the ACK.  Because of this, we can have packets
  // retire from the OPB out of order.
  unsigned int opb_idx = 0;
  while ((opb_idx < _outstanding_packet_buffer[target].size()) &&
         (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num != seq_num_acked)) {
    ++opb_idx;
  }

  if ((opb_idx < _outstanding_packet_buffer[target].size()) &&
         (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num == seq_num_acked)) {
    check_and_clear_opb_of_packet_by_index(target, opb_idx, seq_num_acked);
  }
}


void EndPoint::clear_opb_of_packet_by_index(const int target, const unsigned int opb_idx) {
  // Capture the seq_num for the hash calculation.
  unsigned int clearing_seq_num = _outstanding_packet_buffer[target][opb_idx]->packet_seq_num;
  Flit::FlitType type = _outstanding_packet_buffer[target][opb_idx]->type;
  int size = _outstanding_packet_buffer[target][opb_idx]->size;
  unsigned int read_requested_data_size =
    _outstanding_packet_buffer[target][opb_idx]->read_requested_data_size;


  // Clear all the flits of the packet
  // Note that when we remove flits, we do not increment opb_idx for the next
  // iteration, since the younger flits will be moved into the same position.
  while ((opb_idx < _outstanding_packet_buffer[target].size()) &&
         (!_outstanding_packet_buffer[target][opb_idx]->tail)) {
    clear_opb_of_flit_by_index(target, opb_idx);
  }

  // Now also clear the tail flit...
  if ((_outstanding_packet_buffer[target].empty()) ||
      (!_outstanding_packet_buffer[target][opb_idx]->tail)) {
    cout << _cur_time << ": " << Name() << ": ERROR: Attempted to remove packet for dest: "
         << target << ", with seq_num "
         << clearing_seq_num << " from OPB, but did not find tail flit!" << endl;
    DumpOPB(target);
  }

  clear_opb_of_flit_by_index(target, opb_idx);


  if ((type == Flit::WRITE_REQUEST) || (type == Flit::WRITE_REQUEST_NOOP) || (type == Flit::ANY_TYPE)) {
    _outstanding_xactions_per_dest[target]--;
    _outstanding_put_data_per_dest[target] -= size;
    _new_write_ack_data_per_dest[target] += size;
    _outstanding_xactions_all_dests_stat--;  // Stat only
    _outstanding_outbound_data_per_dest[target] -= size;
      if (_trace_debug){
        //_parent->debug_trace_file << _cur_time << ",OUTDATAPERDEST," << _nodeid <<
          //"," << _outstanding_outbound_data_per_dest[target] << endl;
        //_parent->debug_trace_file << _cur_time << ",OUTXACTIONPERDEST," << _nodeid <<
          //"," << _outstanding_xactions_per_dest[target] << endl;
      }
    _outstanding_outbound_data_all_dests_stat -= size;  // Stat only
  }
  else if (type == Flit::READ_REQUEST) {
    _outstanding_gets_per_dest[target]--;
    _outstanding_inbound_data_per_dest[target] -= read_requested_data_size;

    _outstanding_global_get_req_inbound_data -= read_requested_data_size;
    --_outstanding_global_get_requests;
  }
  else if (type == Flit::READ_REPLY) {
    _outstanding_xactions_per_dest[target]--;
    _outstanding_xactions_all_dests_stat--;  // Stat only
    _outstanding_outbound_data_per_dest[target] -= size;
      if (_trace_debug){
        //_parent->debug_trace_file << _cur_time << ",OUTDATAPERDEST," << _nodeid <<
          //"," << _outstanding_outbound_data_per_dest[target] << endl;
        //_parent->debug_trace_file << _cur_time << ",OUTXACTIONPERDEST," << _nodeid <<
          //"," << _outstanding_xactions_per_dest[target] << endl;
      }
    _outstanding_outbound_data_all_dests_stat -= size;  // Stat only
  }
  else if (type == Flit::RGET_REQUEST) {
    if (!_use_new_rget_metering) {
      _outstanding_rget_reqs_per_dest[target]--;
    }

    // Although RGET_REQUESTS incremented these 2 limiters:
    //   _outstanding_xactions_per_dest
    //   _outstanding_outbound_data_per_dest
    // they only get decremented when the RGET_GET_REPLY is retired.

    // FIXME for deadlock: Add additional metering on RGET_GET_LIMIT::data_size using data_transfer_size

  }
  else if (type == Flit::RGET_GET_REQUEST) {
    // Additional metering on GET_RATE_LIMIT both # and data size, just like a regular GET
    _outstanding_gets_per_dest[target]--;
    _outstanding_inbound_data_per_dest[target] -= read_requested_data_size;

    --_outstanding_global_get_requests;
    _outstanding_global_get_req_inbound_data -= read_requested_data_size;
  }
  else if (type == Flit::RGET_GET_REPLY) {
    _outstanding_xactions_per_dest[target]--;
    _outstanding_xactions_all_dests_stat--;  // Stat only

    // The original RGET_REQUEST incremented the data limits by the size of the
    // data transfer being requested.  The RGET_GET_REPLY closes it out by
    // decrementing by its own size.
    _outstanding_outbound_data_per_dest[target] -= size;
      if (_trace_debug){
        //_parent->debug_trace_file << _cur_time << ",OUTDATAPERDEST," << _nodeid <<
          //"," << _outstanding_outbound_data_per_dest[target] << endl;
        //_parent->debug_trace_file << _cur_time << ",OUTXACTIONPERDEST," << _nodeid <<
          //"," << _outstanding_xactions_per_dest[target] << endl;
      }
    _outstanding_outbound_data_all_dests_stat -= size;  // Stat only

    if (_use_new_rget_metering) {
      _outstanding_rget_reqs_per_dest[target]--;
      _outstanding_rget_inbound_data_per_dest[target] -= size;
    }

    _outstanding_put_data_per_dest[target] -= size;
    _new_write_ack_data_per_dest[target] += size;
  }

  _opb_pkt_occupancy--;

  unsigned int opb_hash = get_opb_hash(target, clearing_seq_num);
  assert(_opb_occupancy_map.count(opb_hash) > 0);
  assert(_opb_occupancy_map[opb_hash] > 0);
  _opb_occupancy_map[opb_hash]--;
}


void EndPoint::clear_opb_of_flit_by_index(const int target, const unsigned int opb_idx) {
  Flit * retiring_flit = _outstanding_packet_buffer[target][opb_idx];

  if (retiring_flit->watch) {
    *gWatchOut << _cur_time << " | " << Name() << " | Retiring flit "
               << retiring_flit->id << " from OPB: head: "
               << retiring_flit->head << ", tail: " << retiring_flit->tail
               << ", size: " << retiring_flit->size << ", src: "
               << retiring_flit->src << ", dest: " << retiring_flit->dest
               << ", seq_num: " << retiring_flit->packet_seq_num << endl;
  }

  // Before telling TM to retire the flit, set the "accepted time", which
  // is now really the "acknowledged time".  This allows the TM to
  // properly calculate the "packet latency" (from creation time to
  // completion).
  retiring_flit->atime = _cur_time;

  // For SWM, we need to get information about the flit in order to reply

  if (gSwm && retiring_flit->head && retiring_flit->data) {
    if (retiring_flit->type == Flit::WRITE_REQUEST){
      _parent->_injection_process[retiring_flit->cl]->eject(retiring_flit->data->Reply());
    }
  }

  if (retiring_flit->head) {
    ++_packets_retired_full_sim;
    if (_parent->_sim_state == TrafficManager::running) {
      ++_packets_retired;
    }
    if ((retiring_flit->type == Flit::READ_REPLY) ||
        (retiring_flit->type == Flit::WRITE_REQUEST) ||
        (retiring_flit->type == Flit::WRITE_REQUEST_NOOP) ||
        (retiring_flit->type == Flit::ANY_TYPE) ||  // Early tests use ANY_TYPE as PUTs/WRITEs
        (retiring_flit->type == Flit::RGET_GET_REPLY)) {
      // Subtract 2 flits for the header
      _data_flits_retired_full_sim += retiring_flit->size - 2;
      if (_parent->_sim_state == TrafficManager::running) {
        _data_flits_retired += retiring_flit->size - 2;
      }
    }
  }
  // Tell the traffic manager that the acked flit is now retired so it can
  // do the accounting.
  _parent->_RetireFlit(retiring_flit, _nodeid);
  ++_flits_retired_full_sim;
  if (_parent->_sim_state == TrafficManager::running) {
    ++_flits_retired;
  }

  // Return the flit to the flit pool
  retiring_flit->Free();
  // Remove the flit from the middle of the opb flit list
  _outstanding_packet_buffer[target].erase(_outstanding_packet_buffer[target].begin() + opb_idx);


  // If there is a retry in progress, we need to update the opb_index due
  // to the ACK removing them.
  // We used to simply decrement the index since we always removed flits from
  // the front, but with the addition of read requests, we now have to check the
  // index being cleared versus the index being replayed.
  if ((_retry_state_tracker[target].dest_retry_state == NACK_BASED) &&
      (_retry_state_tracker[target].nack_replay_opb_flit_index > 0) &&
      (opb_idx < (unsigned int)_retry_state_tracker[target].nack_replay_opb_flit_index)) {
    _retry_state_tracker[target].nack_replay_opb_flit_index--;

    cout << _cur_time << ": " << Name() << ": ACK received during NACK-replay. "
         << "Decremented nack_replay_opb_flit_index to: "
         << _retry_state_tracker[target].nack_replay_opb_flit_index << endl;
  }
}


int EndPoint::find_opb_idx_of_seq_num_head(int target, int seq_num) {
  unsigned int opb_idx = 0;
//cout << GetSimTime() << ": " << Name() << ": Looking in OPB for seq_num: " << seq_num << " for target " << target << endl;

  while (opb_idx < _outstanding_packet_buffer[target].size()) {
    if (_outstanding_packet_buffer[target][opb_idx]->head &&
        (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num == seq_num)) {
      return opb_idx;
    }
    ++opb_idx;
  }
  // Not found
DumpOPB(target);

  return -1;
}


int EndPoint::sack_vec_next_retrans(unsigned int sack_vec) {
  // 1 indicates a received packet.  Upper zeroes with no other 1s are ignored.
  unsigned int sack_idx = 0;
  while ((sack_vec & 0x1) && (sack_idx <= _sack_vec_length)) {
    sack_vec >>= 1;
    ++sack_idx;
  }
  if (sack_idx > _sack_vec_length) {
    // no zeroes found.
    return -1;
  } else {
    // Found a zero.  Now check if there are any higher 1s.
    // If not, ignore this zero.
    if (sack_vec == 0) {
      return -1;
    } else {
      return sack_idx;
    }
  }
}


void EndPoint::_ReceiveCredit(int subnet, Credit * cred) {
  if (_use_crediting) {
    _buf_states[subnet]->ProcessCredit(cred);
  }
  // Now that we've accounted for it, return the credit object to the pool.
  cred->Free();
}


Credit * EndPoint::_ProcessReceivedFlits(int subnet, Flit * & received_flit_ptr) {
  Credit * cred = NULL;

  // Add logic here to decide when to return a credit.

  // For now, if we have a flit to pop off the incoming flit list, then
  // we'll return the credit for it immediately.
  // FIXME: Add delays in the future.

  if (!_incoming_flit_queue[subnet].empty()) {

    received_flit_ptr = _incoming_flit_queue[subnet].front();
    _incoming_flit_queue[subnet].pop_front();

    if (received_flit_ptr->watch) {
      *gWatchOut << _cur_time << " | "
                 << Name() << " | "
                 << "Injecting credit for VC " << received_flit_ptr->vc
                 << " into subnet " << subnet
                 << "." << endl;
    }
    cred = Credit::New();
    cred->vc.insert(received_flit_ptr->vc);

    if (received_flit_ptr->watch) {
      cout << _cur_time << ": " << Name() << ": Received flit: head: "
           << received_flit_ptr->head << ", tail: " << received_flit_ptr->tail
           << ", size: " << received_flit_ptr->size << ", src: "
           << received_flit_ptr->src << ", dest: " << received_flit_ptr->dest
           << ", seq_num: " << received_flit_ptr->packet_seq_num << endl;
    }

    if (received_flit_ptr->type != Flit::CTRL_TYPE) {
      if (received_flit_ptr->head) {
        // If this is the beginning of a multi-flit packet, then record the src
        // and packet_seq_num to compare with the subsequent flits.
        if (!received_flit_ptr->tail) {
          _incoming_packet_src = received_flit_ptr->src;
          _incoming_packet_pid = received_flit_ptr->pid;
          _incoming_packet_seq = received_flit_ptr->packet_seq_num;
        }
        // Capture the packet size, but deduct 1 for the head flit.
        _incoming_packet_flit_countdown = received_flit_ptr->size - 1;
        _incoming_packet_flit_total = received_flit_ptr->size;

//        if (received_flit_ptr->type == Flit::RGET_REQUEST) {
//          cout << _cur_time << ": " << Name() << ": Received RGET request!" << endl;
//        }

      } else {
        // If this is not a head flit, then check that it matches the src and
        // packet_seq_num of the last head flit received.
        if (received_flit_ptr->src != _incoming_packet_src) {
          cout << _cur_time << ": " << Name()
               << ": ERROR: Received non-head flit (id: " << received_flit_ptr->id
               << ", pid: " << received_flit_ptr->pid
               << ") with src != src of last head flit (pid: " << _incoming_packet_pid << ")!  ("
               << received_flit_ptr->src << " != " << _incoming_packet_src << ")"
               << endl;
          exit(1);
        }
        if (received_flit_ptr->pid != _incoming_packet_pid) {
          cout << _cur_time << ": " << Name()
               << ": ERROR: Received non-head flit (id: " << received_flit_ptr->id
               << ", pid: " << received_flit_ptr->pid
               << ") with pid != pid of last head flit!  ("
               << received_flit_ptr->pid << " != " << _incoming_packet_pid << ")"
               << endl;
          exit(1);
        }
        if (received_flit_ptr->packet_seq_num != _incoming_packet_seq) {
          cout << _cur_time << ": " << Name()
               << ": ERROR: Received non-head flit (id: " << received_flit_ptr->id
               << ", pid: " << received_flit_ptr->pid
               << ") with packet_seq_num != packet_seq_num of last head flit!  ("
               << received_flit_ptr->packet_seq_num << " != " << _incoming_packet_seq
               << ")" << endl;
          exit(1);
        }
        // Decrement the flit count
        _incoming_packet_flit_countdown--;
      }
    }

    // Get the sequence number from the received flit and queue it up for return
    // to the initiator.
    if ((received_flit_ptr->tail) && (received_flit_ptr->type != Flit::CTRL_TYPE)) {
      if (_incoming_packet_flit_countdown != 0) {
        cout << _cur_time << ": " << Name() << ": ERROR: Did not receive all flits for packet!"
             << " Expected " << _incoming_packet_flit_countdown << " more.  Packet num: "
             << received_flit_ptr->pid << endl;
        exit(1);
      } else {

        assert(_mypolicy_endpoint.latency->_need_percentile);
        double tmp = _cur_time - received_flit_ptr->ctime;
        if (received_flit_ptr->ctime >= _parent->_reset_time){
            _mypolicy_endpoint.latency->AddSample(tmp);
        }

        // Always enqueue to load balancing queue first, unless space reserved
        // All packets have to go through this
        if (_mypolicy_constant.policy == HC_MY_POLICY &&
            _mypolicy_constant.load_balance_queue_enabled){
          // Load balancing queue is not empty
          shift_load_balance_queue_to_data_queue_if_needed();
          assert(received_flit_ptr->tail || received_flit_ptr->head);

          insert_packet_into_load_balance_queue_or_put_queue(
              received_flit_ptr, _incoming_packet_flit_total);
        } else {
          if (_mypolicy_constant.policy == HC_HOMA_POLICY) {
            HomaEnqueue(received_flit_ptr, _incoming_packet_flit_total);
            if (_trace_debug){
              _parent->debug_trace_file << _cur_time << ",PUTQDEPTH," << _nodeid << "," << _put_buffer_meta.queue_size - _put_buffer_meta.remaining<< endl;
            }
          } else {
            UpdateAckAndReadResponseState(received_flit_ptr, _incoming_packet_flit_total);
          }
        }
      }

      _incoming_packet_src = -1;
      _incoming_packet_pid = -1;
      _incoming_packet_seq = -1;
      _incoming_packet_flit_countdown = -1;
    }

//    cout << _cur_time << ": " << Name() << ": received_flit_ptr: " << received_flit_ptr << endl;

    // This is a copy of the flit held in the initiator's OPB, so free it here,
    // but don't indicate to the traffic manager that it is retired, as that
    // will be done when it is actually removed from the OPB (after being acked).
    received_flit_ptr->Free();
  }

  return cred;
}

void EndPoint::setupNackState(int source, int seq_num, Flit * flit, int packet_size){

    if (_parent->_sim_state == TrafficManager::running) {
      _put_buffer_meta.packet_dropped += packet_size;
    }
    _put_buffer_meta.packet_dropped_full += packet_size;
    // Only send NACK once, until we get back on track.
    // or if we just dropped the packet that is expected, we still send
    //if (!_ack_response_state[source].already_nacked_bad_seq_num) {
    if (!_ack_response_state[source].already_nacked_bad_seq_num
        || (seq_num == (_ack_response_state[source].last_valid_seq_num_recvd + 1))
        ) {
      _ack_response_state[source].outstanding_ack_type_to_return = NACK;
      _ack_response_state[source].already_nacked_bad_seq_num = true;

      if (_mypolicy_constant.policy == HC_MY_POLICY &&
          _ack_response_state[source].time_last_valid_unacked_packet_recvd ==
          999999999) {
        _ack_response_state[source].time_last_valid_unacked_packet_recvd = _cur_time;
      }
    } else {
      if (_mypolicy_constant.policy == HC_MY_POLICY) {
          // Allowing to speculatively send ack to dropped packets
        if (_mypolicy_endpoint.speculative_ack_queue.size() < _mypolicy_constant.speculative_ack_queue_size){
          _mypolicy_endpoint.speculative_ack_queue.push_back({
            flit->type, seq_num,
              _cur_time + _retry_timer_timeout - _estimate_round_trip_cycle, packet_size, source}
             );
        } else {
        }
      }
    }

    if (!_mypolicy_connections[source].initiator_retransmitting){
      // If it's previously not retransmitting, now it's retransmitting
      _mypolicy_endpoint.num_initiator_retransmitting++;
      _mypolicy_connections[source].initiator_retransmitting = true;
    }
    // Keep track of highest bad seq num received to see whether it's retransmitting
    if (_mypolicy_connections[source].highest_bad_seq_num_from_initiator <
        seq_num){
      _mypolicy_connections[source].highest_bad_seq_num_from_initiator = seq_num;
    }
}

void EndPoint::insert_packet_into_load_balance_queue_or_put_queue(Flit * flit, int packet_size){

  assert(HC_HOMA_POLICY != _mypolicy_constant.policy);
  deque<load_balance_queue_record> * lbq = &_put_buffer_meta.load_balance_queue;
  put_buffer_metadata * pbm = &_put_buffer_meta;

    // This should be called only after shift_load_balance_queue_to_data_queue_if_needed

    if ((int) packet_size > pbm->remaining - _mypolicy_endpoint.reserved_space ||
        lbq->size()
    ){
        // put queue full for this packet
        // Or there're packets in front of this in lbq, so can't skip in front
        // So putting it in load balance queue
        if (packet_size <= pbm->load_balance_queue_remaining){
            // load balance queue Still has space
            Flit * cflit = Flit::New();
            cflit->copy_target(flit);
            lbq->push_back({cflit, packet_size});
            pbm->load_balance_queue_remaining -= packet_size;
            if (_trace_debug) {
              _parent->debug_trace_file << _cur_time << ",LBQRM," <<
                _nodeid << "," << lbq_occupied_size() << endl;
            }
        } else {
          // No space, so must drop something with the lowest nack
          int lowest_nack_count;
          int lowest_nack_src = -1;

          // Pick the one with the lower NACK count to drop
          if (!_ack_response_state[flit->src].already_nacked_bad_seq_num){
            for(deque<load_balance_queue_record>::iterator packet = lbq->begin();
                packet != lbq->end(); ++ packet){
                if (_ack_response_state[packet->flit->src].already_nacked_bad_seq_num){
                  //lowest_nack_count = _mypolicy_connections[packet->flit->src].put_drop_counter;
                  lowest_nack_src = packet->flit->src;
                  break;
                }
                if (lowest_nack_src == -1 || (lowest_nack_src != packet->flit->src && lowest_nack_count >
                  _mypolicy_connections[packet->flit->src].put_drop_counter)) {
                  lowest_nack_count = _mypolicy_connections[packet->flit->src].put_drop_counter;
                  lowest_nack_src = packet->flit->src;
                }
            }
          }

          if (lowest_nack_src == -1) {
            if (_mypolicy_connections[flit->src].space_after_NACK_reserved){
              // If new flit has reserved space
              bool originally_ack = _ack_response_state[flit->src].outstanding_ack_type_to_return == ACK;
                UpdateAckAndReadResponseState(flit, packet_size);
                if (originally_ack){
                  assert(_ack_response_state[flit->src].outstanding_ack_type_to_return == ACK);
                }
                return;

            } else {
              lowest_nack_count = _mypolicy_connections[flit->src].put_drop_counter;
              lowest_nack_src = flit->src;
            }
          }
          unsigned int debug_cnt = 0;
          // Drop all packets pertaining to this connection
          // Otherwise, we should drop the new flit
          for(deque<load_balance_queue_record>::iterator packet = lbq->begin();
              packet != lbq->end();){
              if (lowest_nack_src == packet->flit->src) {
                if (!_ack_response_state[packet->flit->src].already_nacked_bad_seq_num){
                  _mypolicy_connections[packet->flit->src].put_drop_counter ++;
                  if (_trace_debug) {
                    _parent->debug_trace_file << _cur_time << ",DROPPUTCOUNT," <<
                      _nodeid << "," << _mypolicy_connections[packet->flit->src].put_drop_counter +
                      packet->flit->src * 10 << endl;
                  }
                }
                int before_remaining = pbm->remaining;
                // Force drop
                packet->flit->packet_seq_num = INT_MAX;
                UpdateAckAndReadResponseState(packet->flit, packet->size);
                assert(before_remaining == pbm->remaining);
                assert(_ack_response_state[packet->flit->src].outstanding_ack_type_to_return == NACK ||
                    _ack_response_state[packet->flit->src].already_nacked_bad_seq_num);
                pbm->load_balance_queue_remaining += packet->size;
                assert(_ack_response_state[packet->flit->src].outstanding_ack_type_to_return == NACK);
                packet = lbq->erase(packet);
                debug_cnt+=packet_size;
              } else packet ++;
          }
          if (_trace_debug) {
            _parent->debug_trace_file << _cur_time << ",LBQRM," <<
              _nodeid << "," << lbq_occupied_size() << endl;
          }

          if (_trace_debug) {
            _parent->debug_trace_file << _cur_time << ",LBQDROPPED," <<
              _nodeid << "," << debug_cnt << endl;
          }
          // Deal with original new flit
          if (packet_size > pbm->load_balance_queue_remaining || (flit->src == lowest_nack_src)){
            // if after dropping (or not) there's still no space in load balance queue, then drop curent packet

              if (!_ack_response_state[flit->src].already_nacked_bad_seq_num) {
                _mypolicy_connections[flit->src].put_drop_counter ++;
                if (_trace_debug) {
                  _parent->debug_trace_file << _cur_time << ",DROPPUTCOUNT," <<
                    _nodeid << "," << _mypolicy_connections[flit->src].put_drop_counter +
                    flit->src * 10 << endl;
                }
              }
            int before_remaining = pbm->remaining;
            UpdateAckAndReadResponseState(flit, packet_size);
            assert(before_remaining == pbm->remaining);

            assert(_ack_response_state[flit->src].outstanding_ack_type_to_return == NACK ||
                _ack_response_state[flit->src].already_nacked_bad_seq_num);
          } else {

            assert(flit->tail || flit->head);
            Flit * cflit = Flit::New();
            cflit->copy_target(flit);
            lbq->push_back({cflit, packet_size});
            pbm->load_balance_queue_remaining -= packet_size;
            if (_trace_debug) {
              _parent->debug_trace_file << _cur_time << ",LBQRM," <<
                _nodeid << "," << lbq_occupied_size()<< endl;
            }
          }
        }
    } else {
      // put queue not full
      assert(
          lbq_occupied_size() == 0 ||
          pbm->remaining < lbq_occupied_size()
      );
      UpdateAckAndReadResponseState(flit, _incoming_packet_flit_total);
  }
}

void EndPoint::shift_load_balance_queue_to_data_queue_if_needed(){
  assert(_mypolicy_constant.policy == HC_MY_POLICY);

  // Lbq: load balance queue
  // pq: put queue
  deque<load_balance_queue_record> * lbq = &_put_buffer_meta.load_balance_queue;
  put_buffer_metadata * pbm = &_put_buffer_meta;


  bool lbq_nempty = pbm->load_balance_queue_remaining != (int) pbm->load_balance_queue_size;

  //cout << _cur_time << _nodeid << ": shift function called lbq" << lbq_occupied_size() << " pq " << occupied_size() << endl;
  if (lbq_nempty){
      assert(lbq->size());
      deque<load_balance_queue_record>::iterator packet = lbq->begin();
      while (packet != lbq->end()) {
          if (_mypolicy_connections[packet->flit->src].space_after_NACK_reserved) {
              // Dequeue any packet that has reserved space
              pbm->load_balance_queue_remaining += packet->size;
              assert(packet->flit->tail || packet->flit->head);
              UpdateAckAndReadResponseState(packet->flit, packet->size);
              if (_trace_debug) {
                  _parent->debug_trace_file << _cur_time << ",LBQRM," <<
                  _nodeid << "," << lbq_occupied_size() << endl;
              }

              packet = lbq->erase(packet);
            } else {
              ++packet;
          }
      }

      while(
        // lbq is not empty
        (pbm->load_balance_queue_remaining != (int) pbm->load_balance_queue_size) &&
        //  and pq has enough space
        ((lbq->front()).size <= pbm->remaining -_mypolicy_endpoint.reserved_space)
      ){

        load_balance_queue_record record = lbq->front();

        lbq->pop_front();
        pbm->load_balance_queue_remaining += record.size;
        if (_trace_debug) {
          _parent->debug_trace_file << _cur_time << ",LBQRM," <<
            _nodeid << "," << lbq_occupied_size() << endl;
        }
        assert(record.flit->tail || record.flit->head);
        UpdateAckAndReadResponseState(record.flit, record.size);
        record.flit->Free();
      }
  }
}


void EndPoint::HomaEnqueue(Flit * flit, int packet_size){
  int source = flit->src;
  int seq_num = flit->packet_seq_num;

  if (_mypolicy_constant.policy != HC_MY_POLICY) {
    if (((flit->type == Flit::WRITE_REQUEST) ||
          (flit->type == Flit::Flit::RGET_GET_REPLY) ||
          (flit->type == Flit::Flit::READ_REPLY)
      ) && packet_size > _put_buffer_meta.remaining -
        _mypolicy_endpoint.reserved_space) {
          if (_debug_enabled) {
            // No space reserved, must drop
            cout << _cur_time << ": " << Name() <<
              ": Dropping Put packet pid " << flit->pid <<
              " seq_num: " << flit->packet_seq_num << "from " << source <<
              " because put wait queue is full with occupancy " <<
              _put_buffer_meta.queue_size - _put_buffer_meta.remaining <<
              " total_size: " << _put_buffer_meta.queue_size <<
              " remaining size: " << _put_buffer_meta.remaining << endl;
          }
          return;
      }
  }


  if (flit->type == Flit::WRITE_REQUEST || flit->type == Flit::RGET_GET_REPLY ||
      flit->type == Flit::Flit::READ_REPLY){

      // Need this info until response is sent
      Flit * homa_flit = Flit::New();
      homa_flit->copy_target(flit);

      put_wait_queue_record put_record = {flit->pid, packet_size, source, (double) packet_size, homa_flit};

      _put_buffer_meta.queue.push_back(put_record);
      _put_buffer_meta.remaining -= packet_size;
  }
}

void EndPoint::HomaAckQueueRecord(put_wait_queue_record r){
  int source = r.src;
  Flit * flit = r.homa_flit;
  int seq_num = flit->packet_seq_num;
  int packet_size = r.size;

  // GOOD CASE: We received a packet with seq_num 1 greater than the last.
  // The first sequence number received is 0, and the struct is initialized to
  // -1, so this works for all cases.
  if (seq_num == (_ack_response_state[source].last_valid_seq_num_recvd + 1)) {

    if (_mypolicy_constant.policy != HC_MY_POLICY){
        if (_ack_response_state[source].time_last_valid_unacked_packet_recvd == 999999999) {
          _ack_response_state[source].time_last_valid_unacked_packet_recvd = _cur_time;
        }
    }

    _ack_response_state[source].last_valid_seq_num_recvd = seq_num;

    // To keep track of whether an initiator is retransmitting
    if (_mypolicy_connections[source].initiator_retransmitting){
      assert(seq_num <= _mypolicy_connections[source].highest_bad_seq_num_from_initiator);
      if (seq_num == _mypolicy_connections[source].highest_bad_seq_num_from_initiator){
        _mypolicy_connections[source].initiator_retransmitting = false;
        _mypolicy_endpoint.num_initiator_retransmitting--;
        assert(_mypolicy_endpoint.num_initiator_retransmitting >= 0);
      }
    }

    if (_mypolicy_constant.policy != HC_MY_POLICY){
    // To match hardware, only increment when we receive good packets.
        _ack_response_state[source].packets_recvd_since_last_ack++;
    } else {
    }

    // Every time we get a good sequence number, reset the "_already_nacked" state.
    _ack_response_state[source].already_nacked_bad_seq_num = false;
    _ack_response_state[source].outstanding_ack_type_to_return = ACK;

    QueueResponse(flit);
  }
  else if (seq_num < (_ack_response_state[source].last_valid_seq_num_recvd + 1)) {
    // If we set this state to ACK here, is it possible that it will overwrite
    // a NACK state that we need to address???
    _ack_response_state[source].outstanding_ack_type_to_return = ACK;
    ++_duplicate_packets_received_full_sim;
    _duplicate_flits_received_full_sim += packet_size;
    if (_parent->_sim_state == TrafficManager::running) {
      ++_duplicate_packets_received;
      _duplicate_flits_received += packet_size;
    }
  }
  else if (seq_num > (_ack_response_state[source].last_valid_seq_num_recvd + 1)) {
    setupNackState(source, seq_num, flit, packet_size);

    ++_bad_packets_received_full_sim;
    _bad_flits_received_full_sim += packet_size;
    if (_parent->_sim_state == TrafficManager::running) {
      ++_bad_packets_received;
      _bad_flits_received += packet_size;
    }

  }

  _put_buffer_meta.remaining += r.size;

  // This is only for the homa case
  flit->Free();
}

// Called when a full packet was received (upon receipt of the tail flit).
// This endpoint will update its internal storage of the last packet received,
// and the ACKs/NACKs that it needs to send back to the source.
// Receipts of standalone ACKs do not call this function.

  // This function can be called with only queuing logic, and only ack logic.
  // Or both. Normal protocol level ack responses will be sent out immediately
  // packet is in buffer, but if you don't want that, you can do it after queueing
  // then you'd need to separate queu and ack logic
void EndPoint::UpdateAckAndReadResponseState(Flit * flit, int packet_size
) {
  assert(HC_HOMA_POLICY != _mypolicy_constant.policy);
  int source = flit->src;
  int seq_num = flit->packet_seq_num;

  // If flit is a put, check the length of put queue
  // If put queue is full, then drop it and do nothing further

  // Record when we received this packet to later determine if we need to
  // manufacture a standalone ack.
  //
  // Don't overwrite this if there is an earlier packet that hasn't been acked
  // yet.  In other words, this timestamp is only captured by the first packet
  // received after the last ack sent.
  // Previously, this condition was not present, and it was causing the
  // simulation to never end because the initiator kept retrying packets (due
  // to timeout) but the receiver was never acking them because:
  //   1. It was done sending packets to that node and was waiting for things
  //      to drain, so had to rely on standalone ACK generation.
  //   2. The retries from the initiator kept arriving and updating this
  //      timestamp, so we never hit the standalone ack generation timeout.

  _ack_response_state[source].time_last_valid_packet_recvd = _cur_time;

  // GOOD CASE: We received a packet with seq_num 1 greater than the last.
  // The first sequence number received is 0, and the struct is initialized to
  // -1, so this works for all cases.
  if (seq_num == (_ack_response_state[source].last_valid_seq_num_recvd + 1)) {
    if (_mypolicy_constant.policy != HC_MY_POLICY){
        if (_ack_response_state[source].time_last_valid_unacked_packet_recvd == 999999999) {
          _ack_response_state[source].time_last_valid_unacked_packet_recvd = _cur_time;
        }
    }

    if (_mypolicy_constant.policy != HC_MY_POLICY) {
      if (((flit->type == Flit::WRITE_REQUEST) ||
            (flit->type == Flit::Flit::RGET_GET_REPLY) ||
            (flit->type == Flit::Flit::READ_REPLY)
        ) && packet_size > _put_buffer_meta.remaining -
          _mypolicy_endpoint.reserved_space) {

        // There is no normal space (unreserved) left
        if (_mypolicy_connections[source].space_after_NACK_reserved &&
            packet_size < _put_buffer_meta.remaining){
          // Space reserved, and there's reserved space
          _mypolicy_connections[source].space_after_NACK_reserved = false;
          _mypolicy_endpoint.reserved_space -=
            _mypolicy_constant.NACK_reservation_size;
          if (_debug_enabled){
            cout << _cur_time << ": " << Name() <<
            ": Admitting because of reservation from " << source <<
            " final reserved space " <<  _mypolicy_endpoint.reserved_space <<
            endl;
          }
        } else {
          if (_debug_enabled) {
            // No space reserved, must drop
            cout << _cur_time << ": " << Name() <<
              ": Dropping Put packet pid " << flit->pid <<
              " seq_num: " << flit->packet_seq_num << "from " << source <<
              " because put wait queue is full with occupancy " <<
              _put_buffer_meta.queue_size - _put_buffer_meta.remaining <<
              " total_size: " << _put_buffer_meta.queue_size <<
              " remaining size: " << _put_buffer_meta.remaining << endl;
          }
          if (_trace_debug) {
            _parent->debug_trace_file << _cur_time << ",DROPPUT," <<
              _nodeid << "," << flit->packet_seq_num << endl;
          }
          // IF this is the last packet, we still wanna

          setupNackState(source, seq_num, flit, packet_size);
          return;
        }
      } else if (_mypolicy_connections[source].space_after_NACK_reserved) {
        // Space is reserved when not needed, freeing the space
        _mypolicy_connections[source].space_after_NACK_reserved = false;
        _mypolicy_endpoint.reserved_space -=
        _mypolicy_constant.NACK_reservation_size;
      }
    }

    _ack_response_state[source].last_valid_seq_num_recvd = seq_num;

    // To keep track of whether an initiator is retransmitting
    if (_mypolicy_connections[source].initiator_retransmitting){
      assert(seq_num <= _mypolicy_connections[source].highest_bad_seq_num_from_initiator);
      if (seq_num == _mypolicy_connections[source].highest_bad_seq_num_from_initiator){
        _mypolicy_connections[source].initiator_retransmitting = false;
        _mypolicy_endpoint.num_initiator_retransmitting--;
        assert(_mypolicy_endpoint.num_initiator_retransmitting >= 0);
      }
    }

    if (_trace_debug){
      _parent->debug_trace_file << _cur_time << ",GOODPACKETREV," <<
        _nodeid << "," << seq_num <<  endl;
    }

    if (flit->watch) {
      *gWatchOut << _cur_time << " | " << Name() << " | " <<
        "Receving good flit " << flit->id
                 << " at time " << _parent->_time << endl;
    }

    if (flit->type == Flit::WRITE_REQUEST || flit->type == Flit::RGET_GET_REPLY
        || flit->type == Flit::READ_REPLY ){
        if (_mypolicy_constant.policy == HC_MY_POLICY){
          // If delay acked is used, we only increment this when dequeue from ack queue
          // But we add the ack to the ack queue
          to_send_ack_queue_record rec =
            {flit->type, seq_num, _cur_time + _retry_timer_timeout - _estimate_round_trip_cycle, packet_size, source};

          if (_trace_debug){
              _parent->debug_trace_file << _cur_time << ",ACKENQ," << _nodeid << "," << rec.seq_num << endl;
          }

          //int offset_after_acked_packet = _mypolicy_connections[source].last_index_in_ack_queue + 1;
          // Start searching for insert form here
          //int to_insert_occupancy = _mypolicy_connections[source].periodic_buffer_occupancy;
          //int to_insert_occupancy = _mypolicy_connections[source].periodic_ack_occupancy;
          //while(offset_after_acked_packet < _mypolicy_endpoint.ack_queue.size()){
            //assert(_mypolicy_endpoint.ack_queue[offset_after_acked_packet].source != source);
            //int to_be_cut_occupancy =
              //_mypolicy_connections[
              //_mypolicy_endpoint.ack_queue[offset_after_acked_packet].source].periodic_ack_occupancy;
            ////cout << "to insert: " << to_insert_occupancy << " to be cut: " << to_be_cut_occupancy <<
              ////" index: " << offset_after_acked_packet << endl;
            //if (to_be_cut_occupancy > to_insert_occupancy){
              //break;
            //} else {
              //offset_after_acked_packet ++;
            //}
          //}
          //if (!_mypolicy_constant.ack_queue_preemptive_insertion_enabled){
            //// Always insert at the end if not enabled
            //offset_after_acked_packet = _mypolicy_endpoint.ack_queue.size();
          //}

          ////e.g. total_size = 10 - 0 to 9
          //// last index in ack queue = 8 - [9, 10] = 9 + [0, 1]
          //// last index in ack queue = 9 - [10] = 10 + [0]
          //// last index in ack queue = 7 - [8, 9, 10] = 8 + [0,1,2]
          //// last index in ack queue = -1 - [0, ... , 10] = 0 + [0,..,10]

          //int offset_after_acked_packet = _mypolicy_connections[source].last_index_in_ack_queue + 1 +
              //RandomInt(_mypolicy_endpoint.ack_queue.size() - _mypolicy_connections[source].last_index_in_ack_queue - 1);

          //int offset_after_acked_packet = _mypolicy_endpoint.ack_queue.size();

          //if (_debug_enabled) {
              //cout << _cur_time << ": " << Name() << ": Enqueuing to ack queue w/ seq_num" <<
              //rec.seq_num << "from source: " <<
              //rec.source << " latest out time: " << rec.latest_time_to_ack << "at position" << offset_after_acked_packet << endl;
          //}

          //// Found location to insert
          //// Increment last_index_in_ack_queue for the other sources
          //for(unsigned int initiator = 0; initiator < _endpoints; initiator++) {
            //if (initiator == source){
              //_mypolicy_connections[source].last_index_in_ack_queue = offset_after_acked_packet;
            //} else {
              //if (_mypolicy_connections[initiator].last_index_in_ack_queue >= offset_after_acked_packet){
                //_mypolicy_connections[initiator].last_index_in_ack_queue++;
              //}
            //}
          //}

          //_mypolicy_endpoint.ack_queue.insert(_mypolicy_endpoint.ack_queue.begin() + offset_after_acked_packet, rec);

          _mypolicy_endpoint.ack_queue.push_back(rec);
          ////Oracle
          //if (_mypolicy_endpoint.ack_queue.size()){
              //sort(_mypolicy_endpoint.ack_queue.begin(),
                   //_mypolicy_endpoint.ack_queue.end(),
                     //[this](const to_send_ack_queue_record & a, const to_send_ack_queue_record & b){

                     //if (this->_mypolicy_connections[a.source].periodic_ack_occupancy ==
                         //this->_mypolicy_connections[b.source].periodic_ack_occupancy)
                     //{
                        //return a.seq_num < b.seq_num;
                     //} else {
                       //return this->_mypolicy_connections[a.source].periodic_ack_occupancy <
                               //this->_mypolicy_connections[b.source].periodic_ack_occupancy;
                     //}
                     //}
              //);

          //}

          if (_cur_time > _mypolicy_endpoint.next_fairness_reset_time){
            reset_ack_occupancy();
          }
        }

        if (_mypolicy_constant.policy != HC_MY_POLICY){
        // To match hardware, only increment when we receive good packets.
            _ack_response_state[source].packets_recvd_since_last_ack++;
        } else {
        }
    } else {
        if (_mypolicy_constant.policy == HC_MY_POLICY) {
            // Since the puts and rgets share the same sequence number, so we're just going to ack everything for now
            //_ack_response_state[source].last_valid_seq_num_recvd_and_ackd = _ack_response_state[source].last_valid_seq_num_recvd;
            to_send_ack_queue_record rec = {flit->type, seq_num,
              _cur_time + _retry_timer_timeout - _estimate_round_trip_cycle, packet_size, source};
            _mypolicy_endpoint.ack_queue.push_back(rec);
            //_mypolicy_connections[source].last_index_in_ack_queue = _mypolicy_endpoint.ack_queue.size() - 1;
        } else {
          _ack_response_state[source].packets_recvd_since_last_ack++;

        }
    }

    // Every time we get a good sequence number, reset the "_already_nacked" state.
    _ack_response_state[source].already_nacked_bad_seq_num = false;


    // SACK
    if (_sack_enabled) {
      if (_ack_response_state[source].sack_vec & 0x1) {
        cout << GetSimTime() << ": " << Name() << ": ERROR: Received previously lost oldest packet from node "
             << source << " with seq_num " << seq_num << ", but rx sack_vec LSB was 1: 0x" << hex
             << _ack_response_state[source].sack_vec << dec << " with last valid seq_num recvd: "
             << _ack_response_state[source].last_valid_seq_num_recvd << endl;
        exit(1);
      }

      if (_debug_sack) {
        cout << GetSimTime() << ": " << Name() << ": Received previously lost oldest packet from node " << source << " with seq_num "
             << seq_num << ", old rx sack_vec: 0x" << hex << _ack_response_state[source].sack_vec
             << ", new rx sack_vec: 0x" << (_ack_response_state[source].sack_vec >> 1) << dec << endl;
      }
      _ack_response_state[source].sack_vec >>= 1;


// All of these were successfully received.  Now process them...?
// FIXME: We also need to generate responses...
      while ((_ack_response_state[source].sack_vec & 0x1)) {
        if (_debug_sack) {
          cout << GetSimTime() << ": " << Name() << ": Clearing already received seq_num from node "
               << source << ", old rx sack_vec: 0x" << hex << _ack_response_state[source].sack_vec
               << ", new rx sack_vec: 0x" << (_ack_response_state[source].sack_vec >> 1) << dec << endl;
        }
        _ack_response_state[source].sack_vec >>= 1;
        //TODO: delayed ack
        _ack_response_state[source].last_valid_seq_num_recvd++;
        _ack_response_state[source].packets_recvd_since_last_ack++;

      }
      if ((_ack_response_state[source].sack_vec == 0) && _debug_sack) {
        cout << GetSimTime() << ": " << Name() << ": Received all lost packets from source "
             << source << ".  Exiting SACK handling." << endl;
      }
    }

    _ack_response_state[source].outstanding_ack_type_to_return = ACK;


    QueueResponse(flit);

    if (flit->type == Flit::WRITE_REQUEST || flit->type == Flit::RGET_GET_REPLY ||
        flit->type == Flit::Flit::READ_REPLY){
        put_wait_queue_record put_record = {flit->pid, packet_size, source, (double) packet_size, flit};
        _put_buffer_meta.queue.push_back(put_record);
        _put_buffer_meta.remaining -= packet_size;

        if (_mypolicy_constant.policy == HC_MY_POLICY) {

          mypolicy_update_periodic_occupancy(&put_record, NULL);
          // This is now allowed because packets can be processed but still unacked
          assert( occupied_size() >= _mypolicy_endpoint.acked_data_in_queue);

          // manufacture ack if over threshold
          // TODO: may not wanna add reserved space here because it's always duplicating ACKs
          if (queue_depth_over_threshold()){
            _ack_response_state[source].packets_recvd_since_last_ack++;

            if (_ack_response_state[source].time_last_valid_unacked_packet_recvd == 999999999) {
              _ack_response_state[source].time_last_valid_unacked_packet_recvd = _cur_time;
            }
          }
        }
        if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",PUTQDEPTH," << _nodeid <<
            "," << occupied_size() << endl;
          if (_mypolicy_constant.policy == HC_MY_POLICY)
            _parent->debug_trace_file << _cur_time << ",PUTQACKED," << _nodeid <<
              "," << _mypolicy_endpoint.acked_data_in_queue << endl;
        }
        if (_debug_enabled) {
            cout << _cur_time << ": " << Name() << ": Enqueueing to put put queue, new depth: " <<
                      _put_buffer_meta.queue_size - _put_buffer_meta.remaining << " acked_depth : "
                      << _mypolicy_endpoint.acked_data_in_queue << endl;
        }
    }
  }

  // (MOSTLY) NO ACTION CASE: Received a packet with a seq_num that we've
  // already received.
  // We'll still send piggybacked ACKs with the highest received seq_num, so
  // this doesn't really change anything.  Just don't increment the ack_seq_num
  // or reset already_nacked.
  // (Note that we still need to set up a potential standalone ACK (which is
  // done unconditionally above) in this case because of the following scenario:
  //   1. Test is over, everything is quiescing, this node (B) has no more real
  //      packets to send to the initiator (A).
  //   2. This node, B, had previously sent an ACK to A, but it is still in
  //      transit.
  //   3. In the meantime, the initiator, A, did a retransmit and is resending
  //      packets that B has already received.
  //   4. B receives the old packets and has to assume that the ACK it just sent
  //      got dropped (even though it might still be in transit).  So the last
  //      action has to be that B sends another ACK (as a standalone packet).
  else if (seq_num < (_ack_response_state[source].last_valid_seq_num_recvd + 1)) {
    // If we set this state to ACK here, is it possible that it will overwrite
    // a NACK state that we need to address???
    _ack_response_state[source].outstanding_ack_type_to_return = ACK;

    if (_debug_enabled) {
      cout << _cur_time << ": " << Name() << ": Received packet with previously-received seq_num: "
        << seq_num << ", from dest: " << source << endl;
    }

    if (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0)) {
      *gWatchOut << _cur_time << " | " << Name() << " | Received duplicate packet: " << flit->pid << " with seq_num: " << seq_num << endl;
    }

    ++_duplicate_packets_received_full_sim;
    _duplicate_flits_received_full_sim += flit->size;
    if (_parent->_sim_state == TrafficManager::running) {
      ++_duplicate_packets_received;
      _duplicate_flits_received += flit->size;
    }
  }

  // BAD CASE: Received a packet with seq_num greater than the expected num.
  // Only send a NACK if the latest packet seq_num is more than 1 greater than
  // what we've already seen.  (Ie, one or more packets in between were never
  // seen.)
  else if (seq_num > (_ack_response_state[source].last_valid_seq_num_recvd + 1)) {

    if (_sack_enabled) {

      // SACK vector encoding:
      // 0 indicates missing seq_nums.  1 indicates a seq_num that was received.
      // Trailing zeroes (in the MSB positions) are ignored.


      unsigned int num_missing = (seq_num - _ack_response_state[source].last_valid_seq_num_recvd) - 1;

      // Do not exceed the length of the sack_vec.
      if ((num_missing + 1) <= _max_receivable_pkts_after_drop) {

        // Regardless of whether we triggered a SACK, keep updating the received
        // sack_vec until we run out of bits in the vector.
        // Append to existing sack_vec.
        // The sack_vec is organized with the oldest seq_num pos in the LSB.  As
        // new packets are received, we keep shifting them up in bit position and
        // ORing them into the existing vector.  When new packets are received,
        // the 1 is left-shifted into place, always leaving zeroes in the lower
        // positions.  This ignores any previously received good packets, but the
        // ORing with the previous vector value preserves them.
        _ack_response_state[source].sack_vec |= ((unsigned long)1 << num_missing);

        _ack_response_state[source].outstanding_ack_type_to_return = SACK;

        if (_debug_sack) {
          cout << GetSimTime() << ": " << Name() << ": Received OOO packets from node " << source
               << ". last_valid_seq_num_recvd: " << _ack_response_state[source].last_valid_seq_num_recvd
               << ", latest seq_num recvd: " << seq_num
               << ", new rx sack_vec: 0x" << hex << _ack_response_state[source].sack_vec << dec << endl;
        }

        // Queue up responses to out of order packets, rather than waiting for
        // them to complete in order.  This is easier since we don't have to
        // store information for later.
        QueueResponse(flit);

      } else {
        // FIXME: We have exceeded the length of the sack_vec.... now what???
        cout << GetSimTime() << ": " << Name() << ": ERROR: Exceeded sack vector length!  "
             << "vec: 0x" << hex << _ack_response_state[source].sack_vec << dec << ", last_valid_seq_num_recvd: "
             << _ack_response_state[source].last_valid_seq_num_recvd << ", recvd seq_num: " << seq_num
             << ", from node " << source << endl;
// Do nothing.  Wait for sender timeout to fix.
//        exit(1);
      }

    } else {
      setupNackState(source, seq_num, flit, packet_size);
    }

    if (_debug_enabled) {
      cout << _cur_time << ": " << Name() << ": Received bad packet: " << flit->pid <<
        " seq_num: " << seq_num << ", from dest: "
           << source << ", expected seq_num: " << (_ack_response_state[source].last_valid_seq_num_recvd + 1)
           << endl;
    }

    if (_trace_debug){
      _parent->debug_trace_file << _cur_time << ",BADSEQ," << _nodeid << ","
        << flit->packet_seq_num << endl;
    }

    if (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0)) {
      *gWatchOut << _cur_time << " | " << Name() << " | Received bad packet: " << flit->pid <<
        " with seq_num: " << seq_num << ". Expecting seq_num: " <<
        (_ack_response_state[source].last_valid_seq_num_recvd + 1) << endl;
    }

    ++_bad_packets_received_full_sim;
    _bad_flits_received_full_sim += flit->size;
    if (_parent->_sim_state == TrafficManager::running) {
      ++_bad_packets_received;
      _bad_flits_received += flit->size;
    }

//    cout << _cur_time << ": " << Name() << ": Received non-sequential packet sequence number from dest: "
//         << source << ".  Previous seq_num: " << _ack_response_state[source].last_valid_seq_num_recvd
//         << ".  Received seq_num: " << seq_num << endl;
  }

// There is a corner case where the receiver is done sending new messages to a
// remote node, but that node is still waiting to get the acks back for the
// messages it sent.  Normally, we always piggyback acks on "real" messages,
// but in this case, there is nothing to piggyback on.  Standalone acks can
// still be used, but we only queue them up if we have received a new sequence
// number.
// This is resolved by the initiator timing out and resending...
//
// But we need to allow standalones to also go when we get older sequence
// numbers again... in order to do that, we need to set ACK here...
// actually, just don't set it to NONE above when we manufacture a standalone.

}


void EndPoint::QueueResponse(Flit * flit) {
  unsigned int source = flit->src;
  int seq_num = flit->packet_seq_num;
  Flit::FlitType type = flit->type;
  int read_size = flit->read_requested_data_size;
  int response_to_seq_num = flit->response_to_seq_num;
  bool record = flit->record;

  if (flit->data) {
      _parent->_injection_process[flit->cl]->eject(flit->data);
  } else {
  }

  ++_good_packets_received_full_sim;
  _good_flits_received_full_sim += flit->size;
  if (_parent->_sim_state == TrafficManager::running) {
    ++_good_packets_received;
    _good_flits_received += flit->size;

    if (flit->type == Flit::WRITE_REQUEST ||
        flit->type == Flit::RGET_GET_REPLY) {
      ++_good_packets_write_received;
        if (_trace_debug){
          _parent->debug_trace_file << _cur_time << ",ACCUMGOODPACKETREV," << _nodeid <<
            "," << _good_packets_write_received << endl;
        }
    }
  }
  if ((flit->type == Flit::READ_REPLY) ||
      (flit->type == Flit::WRITE_REQUEST) ||
      (flit->type == Flit::WRITE_REQUEST_NOOP) ||
      (flit->type == Flit::ANY_TYPE) ||  // Early tests use ANY_TYPE as PUTs/WRITEs
      (flit->type == Flit::RGET_GET_REPLY)) {
    _good_data_flits_received_full_sim += flit->size - 2;  // subtract 2 flits for the header
    if (_parent->_sim_state == TrafficManager::running) {
      _good_data_flits_received += flit->size - 2;
    }
  }

  if (_debug_enabled) {
    cout << _cur_time << ": " << Name() << ": Received good packet with +1 incremented seq_num: " << seq_num
         << ", from dest: " << source << ", with type: " << flit_type_strings[type] << endl;

  }

  if (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0)) {
    *gWatchOut << _cur_time << " | " << Name() << " | Received good packet: " << flit->pid
               << " with seq_num: " << seq_num << endl;
  }

  WorkloadMessagePtr pdata = 0;
  if(gSwm) {
     auto reply = flit->data->Reply();
     read_size = reply->Size();
     pdata = reply;
  }
// Only queue up a read response if the request was cleanly received.
  // NOTE: This requires that the tail flit has the correct flit type!!!
  if (type == Flit::READ_REQUEST) {
      pending_rsp_record local_record = {source, Flit::READ_REPLY, read_size,
                                         _cur_time + (int)_req_processing_latency, record, 0, seq_num, 0,
                                         pdata,
                                         (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0))};
      _pending_outbound_response_queue.push_back(local_record);

      if (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0)) {
        *gWatchOut << _cur_time << " | " << Name() << " | Received READ_REQUEST from " << source
                   << " with seq_num " << seq_num << endl;
      }
  } else {
    if (type == Flit::READ_REPLY) {
//        mark_response_received_in_opb(source, response_to_seq_num);
      pending_rsp_record local_record = {source, Flit::READ_REPLY, read_size,
                                         _cur_time + (int)_rsp_processing_latency, record, 0,
                                         response_to_seq_num, 0, pdata, false};
      _pending_inbound_response_queue.push_back(local_record);

    } else if (type == Flit::RGET_REQUEST) {
      pending_rsp_record local_record = {source, Flit::RGET_GET_REQUEST, _parent->_read_request_size[0],
                                         _cur_time + (int)_rget_processing_latency, record, 0, seq_num, read_size,
                                         pdata,
                                         (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0))};
      _pending_outbound_response_queue.push_back(local_record);

      if (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0)) {
        *gWatchOut << _cur_time << " | " << Name() << " | Received RGET_REQUEST from " << source
                   << " with seq_num " << seq_num << endl;
      }
    } else if (type == Flit::RGET_GET_REQUEST) {
//        mark_response_received_in_opb(source, response_to_seq_num);

      // Handle the inbound side.
      pending_rsp_record local_record = {source, Flit::RGET_GET_REQUEST, _parent->_read_request_size[0],
                                         _cur_time + (int)_rsp_processing_latency, record, 0, response_to_seq_num, read_size,
                                          pdata, false};
      _pending_inbound_response_queue.push_back(local_record);

//        cout << _cur_time << ": " << Name() << ": Received RGET_GET_REQUEST from "
//             << source << " with response_to_seq_num: " << response_to_seq_num << endl;

      // Finally, after passing the desired read size around, we actually
      // build the transaction of that size to return the data.
      // Handle the response generation.
      local_record = {source, Flit::RGET_GET_REPLY, read_size,
                      _cur_time + (int)_req_processing_latency, record, 0, seq_num, 0, pdata,
                      (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0))};
      _pending_outbound_response_queue.push_back(local_record);

      if (gWatchOut && (_parent->_packets_to_watch.count(flit->pid) > 0)) {
        *gWatchOut << _cur_time << " | " << Name() << " | Received RGET_GET_REQUEST from " << source
                   << " with seq_num " << seq_num << endl;
      }
    } else if (type == Flit::RGET_GET_REPLY) {

//        cout << _cur_time << ": " << Name() << ": Received RGET_GET_REPLY from "
//             << source << " with response_to_seq_num: " << response_to_seq_num << endl;

    }
  }
}


void EndPoint::mark_response_received_in_opb(int target, int response_to_seq_num) {
  if (_outstanding_packet_buffer.count(target) == 0) {
    cout << _cur_time << ": " << Name() << ": ERROR: Received response packet from node "
         << "with no outstanding packets in OPB." << endl;
    exit(1);
  }

  unsigned int opb_idx = 0;
  while (opb_idx < _outstanding_packet_buffer[target].size()) {
    assert(_outstanding_packet_buffer[target][opb_idx]->head);

    if (((_outstanding_packet_buffer[target][opb_idx]->type == Flit::READ_REQUEST) ||
         (_outstanding_packet_buffer[target][opb_idx]->type == Flit::RGET_REQUEST)) &&
         (_outstanding_packet_buffer[target][opb_idx]->packet_seq_num == response_to_seq_num)) {
      _outstanding_packet_buffer[target][opb_idx]->response_received = true;

      if (_debug_enabled) {
        cout << _cur_time << ": " << Name() << ": Received response for "
             << flit_type_strings[_outstanding_packet_buffer[target][opb_idx]->type] << " request (seq_num: "
             << _outstanding_packet_buffer[target][opb_idx]->packet_seq_num
             << ") from dest: " << target << endl;

      }
      if (_trace_debug){
        _parent->debug_trace_file << _cur_time << ",REVRESP," << _nodeid <<
          "," << _outstanding_packet_buffer[target][opb_idx] -> src << "," << _outstanding_packet_buffer[target][opb_idx]-> dest <<
          "," << _outstanding_packet_buffer[target][opb_idx] -> pid << "," << _outstanding_packet_buffer[target][opb_idx]-> id <<
          "," << flit_type_strings[_outstanding_packet_buffer[target][opb_idx]->type] <<
          "," << _outstanding_packet_buffer[target][opb_idx]->packet_seq_num << endl;
      }

      // FIXME:
      // For RGET_REQUEST handling, if we've received the RGET_GET_REQUEST, we
      // deallocate the OPB entry immediately, knowing that we'll be generating
      // an RGET_GET_REPLY that will allocate its own entry.  However, we may
      // need to add a check here to ensure that there is an OPB entry
      // available...  That's what the hardware does.  But in this simulator,
      // we'll just pend the response until an OPB entry becomes available...
      // Another thing to consider is whether we want to track this second part
      // of the transaction flow in the total RGET latency.  We're not really
      // tracking that right now, but may want to...
      if (_outstanding_packet_buffer[target][opb_idx]->ack_received) {
        clear_opb_of_packet_by_index(target, opb_idx);
      }
      break;

    } else {
      // Skip over body flits.
      // Note that we may be in the process of transmitting a packet, in which
      // case, the entire packet will not yet be in the opb.  So we also need
      // to look for the end of the buffer.
      while ((opb_idx < _outstanding_packet_buffer[target].size()) &&
             (!_outstanding_packet_buffer[target][opb_idx]->tail)) {
        opb_idx++;
      }
      if ((opb_idx < _outstanding_packet_buffer[target].size()) &&
          (_outstanding_packet_buffer[target][opb_idx]->tail)) {
        opb_idx++;
      }
    }
  }
}


bool EndPoint::InjectionBuffersEmpty(int c) {
  for (unsigned int buf_num = 0; buf_num < _num_injection_queues; buf_num++) {
    if (!_injection_buffer[c][buf_num].empty()) {
      return false;
    }
  }
  return true;
}


bool EndPoint::InjectionBuffersNotEmptyButAllBlockedOnTimeout(int c) {
  bool all_blocked_on_timeout_count = true;
  if (InjectionBuffersEmpty(c)) {
    all_blocked_on_timeout_count = false;
  } else {
    for (unsigned int buf_num = 0; buf_num < _num_injection_queues; buf_num++) {
      if ((!_injection_buffer[c][buf_num].empty()) && (_retry_state_tracker[buf_num].dest_retry_state != TIMEOUT_BASED)) {
        all_blocked_on_timeout_count = false;
        break;
      }
    }
  }

  return all_blocked_on_timeout_count;
}


bool EndPoint::_InjectionQueueDrained(int c) {

  //for (int cl = 0; cl < _classes; cl++) {
    //if (!_parent->_injection_process[cl]->is_done(_nodeid)){
      //return false;
    //}
  //}
  return _qdrained[c];
}

bool EndPoint::_EndPointProcessingFinished(){
  return _put_buffer_meta.queue.empty();
}


bool EndPoint::_OPBDrained() {
  map<int, deque< Flit * > >::iterator dest_iter;
  for ( dest_iter = _outstanding_packet_buffer.begin();
        dest_iter != _outstanding_packet_buffer.end();
        dest_iter++ ) {
    if ( !_outstanding_packet_buffer[dest_iter->first].empty() ) {
      return false;
    }
  }
  return true;
}


bool EndPoint::PendingRepliesDrained() {
  unsigned int total_flits =0;
  for (unsigned int response_queue = 0; response_queue < _num_response_queues; response_queue++) {
    total_flits += _repliesPending[response_queue].size();
  }

  return (total_flits == 0);
}


bool EndPoint::PendingRgetGetRequestQueuesDrained() {
  unsigned int total_flits =0;
  for (unsigned int queue_idx = 0; queue_idx < _num_rget_get_req_queues; queue_idx++) {
    total_flits += _rget_get_req_queues[queue_idx].size();
  }

  return (total_flits == 0);
}


bool EndPoint::_AcksToReturn() {
  for (unsigned int initiator = 0; initiator < _endpoints; initiator++) {
    if (_ack_response_state[initiator].time_last_valid_unacked_packet_recvd != 999999999) {
      if (_debug_enabled) {
        cout << _cur_time << ": " << Name() << ": Simulation ending: Still waiting "
             << "for standalone timeout to send ACK to initiator: "
             << initiator << ". Last packet received in cycle: "
             << _ack_response_state[initiator].time_last_valid_unacked_packet_recvd
             << ", cycles to timeout: "
             << (_ack_response_state[initiator].time_last_valid_unacked_packet_recvd +
                 _cycles_before_standalone_ack) - _cur_time
             << ", packets recvd since last ack: "
             << _ack_response_state[initiator].packets_recvd_since_last_ack
             << endl;
      }
      return true;
    }
  }
  return false;
}


void EndPoint::_ClearStats(){
  _parent->_ClearStats();

  cout << "endpoint " << _nodeid << " clearing stats" << endl;

  _packets_retired = 0;
  _packets_retired_full_sim = 0;
  _flits_retired = 0;
  _flits_retired_full_sim = 0;
  _data_flits_retired = 0;
  _data_flits_retired_full_sim = 0;
  _good_packets_received = 0;
  _good_packets_write_received = 0;
  _good_flits_received = 0;
  _good_data_flits_received = 0;
  _good_packets_received_full_sim = 0;
  _good_flits_received_full_sim = 0;
  _good_data_flits_received_full_sim = 0;
  _duplicate_packets_received = 0;
  _duplicate_flits_received = 0;
  _duplicate_packets_received_full_sim = 0;
  _duplicate_flits_received_full_sim = 0;
  _bad_packets_received = 0;
  _bad_flits_received = 0;
  _bad_packets_received_full_sim = 0;
  _bad_flits_received_full_sim = 0;
  _flits_dropped_for_rget_conversion = 0;
  _packets_dequeued = 0;
  _put_buffer_meta.packet_dropped = 0;
  _put_buffer_meta.packet_dropped_full = 0;

  _mypolicy_endpoint.latency->Clear();

#define CLEAR(n) fill(n.begin(), n.end(), 0)

  CLEAR(_generated_packets);
  CLEAR(_generated_packets);
  CLEAR(_generated_flits);
  CLEAR(_injected_flits);
  CLEAR(_sent_flits);
  CLEAR(_sent_packets);
  CLEAR(_new_sent_flits);
  CLEAR(_new_sent_packets);
  CLEAR(_received_flits);
  CLEAR(_received_packets);

}

bool EndPoint::_EndSimulation() {
  bool checks_passed = true;

  if (!gShouldSkipDrain) {
    // Perform end-of-run checks
    for (unsigned int initiator = 0; initiator < _endpoints; initiator++) {
      if (_ack_response_state[initiator].time_last_valid_unacked_packet_recvd != 999999999) {
        cout << Name() << ": ERROR: Still waiting to send ACK with seq_num: "
            << _ack_response_state[initiator].last_valid_seq_num_recvd
            << " to initiator: "
            << initiator << ". Last packet received in cycle: "
            << _ack_response_state[initiator].time_last_valid_unacked_packet_recvd
            << ", time last packet recvd: " << _ack_response_state[initiator].time_last_valid_packet_recvd
            << ", time last ack sent: " << _ack_response_state[initiator].time_last_ack_sent
            << ", packets recvd since last ack: " << _ack_response_state[initiator].packets_recvd_since_last_ack << endl;
        checks_passed = false;
      }
    }

    if ((_incoming_packet_src != -1) || (_incoming_packet_pid != -1) ||
        (_incoming_packet_seq != -1) || (_incoming_packet_flit_countdown != -1)) {
      cout << Name() << ": ERROR: Incoming packet processing not quiesced. _incoming_packet_src: "
          << _incoming_packet_src << ", _incoming_packet_pid: " << _incoming_packet_pid
          << ", _incoming_packet_seq: " << _incoming_packet_seq
          << ", _incoming_packet_flit_countdown: " << _incoming_packet_flit_countdown << endl;
      checks_passed = false;
    }

    if (!_OPBDrained()) {
      cout << Name() << ": ERROR: OPB not drained!" << endl;
      DumpOPB();
      checks_passed = false;
    }

    for (int cl = 0; cl < _classes; cl++) {
      if (!InjectionBuffersEmpty(cl)) {
        cout << Name() << ": ERROR: Injection buffers not drained!" << endl;
        DumpInjectionQueues();
        checks_passed = false;
      }
    }

    if (!PendingRepliesDrained()) {
      cout << Name() << ": ERROR: Pending replies queue not drained!" << endl;
      DumpReplyQueues();
      checks_passed = false;
    }

    if (!PendingRgetGetRequestQueuesDrained()) {
      cout << Name() << ": ERROR: RGET GET Request queues not drained!" << endl;
      DumpRgetGetRequestQueues();
      checks_passed = false;
    }

    // Check that all outstanding counters are back to 0
    for (unsigned int dest = 0; dest < _endpoints; dest++) {
      if (_outstanding_xactions_per_dest[dest] != 0) {
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: _outstanding_xactions_per_dest["
            << dest << "] != 0.  Value = " << _outstanding_xactions_per_dest[dest] << endl;
        checks_passed = false;
      }
      if (_outstanding_put_data_per_dest[dest] != 0) {
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: _outstanding_put_data_per_dest["
            << dest << "] != 0.  Value = " << _outstanding_put_data_per_dest[dest] << endl;
        checks_passed = false;
      }
      if (_outstanding_outbound_data_per_dest[dest] != 0) {
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: _outstanding_outbound_data_per_dest["
            << dest << "] != 0.  Value = " << _outstanding_outbound_data_per_dest[dest] << endl;
        checks_passed = false;
      }
      if (_outstanding_inbound_data_per_dest[dest] != 0) {
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: _outstanding_inbound_data_per_dest["
            << dest << "] != 0.  Value = " << _outstanding_inbound_data_per_dest[dest] << endl;
        checks_passed = false;
      }
      if (_outstanding_gets_per_dest[dest] != 0) {
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: _outstanding_gets_per_dest["
            << dest << "] != 0.  Value = " << _outstanding_gets_per_dest[dest] << endl;
        checks_passed = false;
      }
      if (_outstanding_rget_reqs_per_dest[dest] != 0) {
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: _outstanding_rget_reqs_per_dest["
            << dest << "] != 0.  Value = " << _outstanding_rget_reqs_per_dest[dest] << endl;
        checks_passed = false;
      }
      if (_outstanding_rget_inbound_data_per_dest[dest] != 0) {
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: _outstanding_rget_inbound_data_per_dest["
            << dest << "] != 0.  Value = " << _outstanding_rget_inbound_data_per_dest[dest] << endl;
        checks_passed = false;
      }

      if (!_put_buffer_meta.queue.empty()){
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: PUt Wait Queue still has "
            << _put_buffer_meta.queue.size() << " items." << endl;
        checks_passed = false;
      }

      if (_mypolicy_endpoint.acked_data_in_queue || _mypolicy_endpoint.data_dequeued_but_need_acked){
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: Acked data in put wait queue  or data_dequeued_but_need_acked:"
            << _mypolicy_endpoint.acked_data_in_queue << endl;
        checks_passed = false;
      }

      if (_put_buffer_meta.queue_size != (unsigned int) _put_buffer_meta.remaining){
        cout << _cur_time << ": " << Name() << ": ERROR: End of run check: Put queue remaining is " <<
          _put_buffer_meta.remaining <<
          "not same as original size"
            << _put_buffer_meta.queue.size() << " items." << endl;
        checks_passed = false;
      }

    }
  }

  assert(_mypolicy_endpoint.latency->_need_percentile);

  // Stats from TM do no include startup and drain phases
  double time_delta = (double)(_parent->_drain_time - _parent->_reset_time);
  int cl = 0;
  cout << Name() << ":" << endl;
  cout << "  Injected packet rate average (including retrans, excluding standalone acks)     = " << (double)_sent_packets[cl]/time_delta
       << "   (" << _sent_packets[cl] << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Injected new packet rate average (including retrans, excluding standalone acks) = " << (double)_new_sent_packets[cl]/time_delta
       << "   (" << _new_sent_packets[cl] << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Retired packet rate average (steady-state)                                      = " << (double)_packets_retired/time_delta
       << "   (" << _packets_retired << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Accepted packet rate average (including retrans, excluding standalone acks)     = " << (double)_received_packets[cl]/time_delta
       << "   (" << _received_packets[cl] << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Accepted GOOD packet rate average (steady-state)                                = " << (double)_good_packets_received/time_delta
       << "   (" << _good_packets_received << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Accepted GOOD write packet rate average (steady-state)                          = " << (double)_good_packets_write_received/time_delta
       << "   (" << _good_packets_write_received << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Injected flit rate average (including retrans, excluding standalone acks)       = " << (double)_sent_flits[cl]/time_delta
       << "   (" << _sent_flits[cl] << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Injected new flit rate average (excluding retrans, excluding standalone acks)   = " << (double)_new_sent_flits[cl]/time_delta
       << "   (" << _new_sent_flits[cl] << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Retired flit rate average (steady-state)                                        = " << (double)_flits_retired/time_delta
       << "   (" << _flits_retired << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Accepted flit rate average (including retrans, excluding standalone acks)       = " << (double)_received_flits[cl]/time_delta
       << "   (" << _received_flits[cl] << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Accepted GOOD flit rate average (steady-state)                                  = " << (double)_good_flits_received/time_delta
       << "   (" << _good_flits_received << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Injected data flit rate average (including retrans, excluding standalone acks)       = " << (double)_sent_data_flits/time_delta
       << "   (" << _sent_data_flits << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Injected new data flit rate average (excluding retrans, excluding standalone acks)   = " << (double)_new_sent_data_flits/time_delta
       << "   (" << _new_sent_data_flits << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Retired data flit rate average (steady-state)                                        = " << (double)_data_flits_retired/time_delta
       << "   (" << _data_flits_retired << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Accepted data flit rate average (including retrans, excluding standalone acks)       = " << (double)_received_data_flits/time_delta
       << "   (" << _received_data_flits << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  Accepted GOOD data flit rate average (steady-state)                                  = " << (double)_good_data_flits_received/time_delta
       << "   (" << _good_data_flits_received << " from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;

  cout << "  Average Latency (steady-state)                                  = " << (double)_mypolicy_endpoint.latency->Average()
       << "   ( from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  50\% Latency (steady-state)                                  = " << (double)_mypolicy_endpoint.latency->Percentile(0.5)
       << "   ( from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;
  cout << "  95\% Latency (steady-state)                                  = " << (double)_mypolicy_endpoint.latency->Percentile(0.95)
       << "   ( from " << _parent->_reset_time << " to " << _parent->_drain_time << ")" << endl;

  cout << "  Average generated packet size (flits during steady-state)      = " << (double)_generated_flits[0]/(double)_generated_packets[0] << endl;
  cout << "  Packets generated (steady-state)                               = " << _generated_packets[0] << endl;
  cout << "  Packets generated (full sim)                                   = " << _generated_packets_full_sim << endl;
  cout << "  Flits generated (steady-state)                                 = " << _generated_flits[0] << endl;
  cout << "  Flits generated (full sim)                                     = " << _generated_flits_full_sim << endl;
  cout << "  Flits discarded for rget conversion (full sim)                 = " << _flits_dropped_for_rget_conversion << endl;
  cout << "  Max outstanding xactions to any single target                  = " << _max_outstanding_xactions_per_dest_stat << endl;
  cout << "  Max outstanding xactions to all targets                        = " << _max_outstanding_xactions_all_dests_stat << endl;
  cout << "  Max total outstanding data to any single target (in flits)     = " << _max_total_outstanding_data_per_dest_stat << endl;
  cout << "  Max total outstanding data to all targets (in flits)           = " << _max_total_outstanding_outbound_data_all_dests_stat << endl;
  cout << "  Packets retired (steady-state)                                 = " << _packets_retired << endl;
  cout << "  Packets retired (full sim)                                     = " << _packets_retired_full_sim << endl;
  cout << "  Flits retired (steady-state)                                   = " << _flits_retired << endl;
  cout << "  Flits retired (full sim)                                       = " << _flits_retired_full_sim << endl;
  cout << "  Data flits retired (steady-state)                              = " << _data_flits_retired << endl;
  cout << "  Data flits retired (full sim)                                  = " << _data_flits_retired_full_sim << endl;
  cout << "  Good packets received (steady-state)                           = " << _good_packets_received << endl;
  cout << "  Good packets received (full sim)                               = " << _good_packets_received_full_sim << endl;
  cout << "  Good flits received (steady-state)                             = " << _good_flits_received << endl;
  cout << "  Good flits received (full sim)                                 = " << _good_flits_received_full_sim << endl;
  cout << "  Good data flits received (steady-state)                        = " << _good_data_flits_received << endl;
  cout << "  Good data flits received (full sim)                            = " << _good_data_flits_received_full_sim << endl;
  cout << "  Duplicate packets received (steady-state)                      = " << _duplicate_packets_received << endl;
  cout << "  Duplicate flits received (steady-state)                        = " << _duplicate_flits_received << endl;
  cout << "  Duplicate packets received (full sim)                          = " << _duplicate_packets_received_full_sim << endl;
  cout << "  Duplicate flits received (full sim)                            = " << _duplicate_flits_received_full_sim << endl;
  cout << "  Bad packets received (steady-state)                            = " << _bad_packets_received << endl;
  cout << "  Bad flits received (steady-state)                              = " << _bad_flits_received << endl;
  cout << "  Bad packets received (full sim)                                = " << _bad_packets_received_full_sim << endl;
  cout << "  Bad flits received (full sim)                                  = " << _bad_flits_received_full_sim << endl;
  cout << "  NACKs sent (full sim)                                          = " << _nacks_sent << endl;
  cout << "  NACKs received (full sim)                                      = " << _nacks_received << endl;
  cout << "  SACKs sent (full sim)                                          = " << _sacks_sent << endl;
  cout << "  SACKs received (full sim)                                      = " << _sacks_received << endl;
  cout << "  Packet retry timer expirations (full sim)                      = " << _retry_timeouts << endl;
  cout << "  Packets retransmitted (steady-state)                           = " << _packets_retransmitted << endl;
  cout << "  Flits retransmitted (steady-state)                             = " << _cycles_retransmitting << endl;
  cout << "  Average Flits retransmitted (steady-state)                     = " << (double) _cycles_retransmitting / time_delta << endl;
  cout << "  Packets retransmitted (full sim)                               = " << _packets_retransmitted_full_sim << endl;
  cout << "  Flits retransmitted (full sim)                                 = " << _flits_retransmitted_full_sim << endl;
  cout << "  Max packet retries (full sim)                                  = " << _max_packet_retries_full_sim << endl;
  cout << "  PUTs converted to RGETs                                        = " << _puts_converted_to_rgets << endl;
  cout << "  Host Congestion active                                         = " << _mypolicy_endpoint.host_congestion_enabled << endl;
  cout << "  Packets dropped (full sim)                                     = " << (double) _put_buffer_meta.packet_dropped_full << endl;
  cout << "  Packets dropped (steady-state)                                 = " << (double) _put_buffer_meta.packet_dropped/time_delta << endl;
  cout << "  steady-state time                                               = " << (int) time_delta << endl;

  cout << "  Cycles packet generation not attempted (steady-state)          = " << _cycles_generation_not_attempted << endl;
  cout << "  Cycles packet generation attempted but blocked (steady-state)  = " << _cycles_gen_attempted_but_blocked << endl;
  cout << "  Cycles new flit not injected (steady-state)                    = " << _cycles_new_flit_not_injected << endl;
  cout << "    Cycles link available, but no new flits present (steady-state) = " << _cycles_link_avail_no_new_flits << endl;
  cout << "    Cycles retransmitting (steady-state)                           = " << _cycles_retransmitting << endl;
  cout << "    Cycles flit staging blocked due to packet penalty (ss)         = " << _cycles_new_flit_not_injected_due_to_packet_processing_penalty << endl;
  cout << "    Cycles flit staging blocked due to staging buffer full (ss)    = " << _cycles_new_flit_not_injected_due_to_staging_buffer_full << endl;
  cout << "    Cycles new injects present but blocked (steady-state)          = " << _cycles_inj_present_but_blocked << endl;
  cout << "      Cycles all new injects blocked on timeouts (steady-state)      = " << _cycles_inj_all_blocked_on_timeout << endl;
  cout << "      OPB Insertion Conflicts (targets * cycles)                     = " << _opb_insertion_conflicts << endl;
  cout << "      Request injects blocked by xaction limit (targets * cycles)    = " << _req_inj_blocked_on_xaction_limit << endl;
  cout << "      Request injects blocked by size limit (targets * cycles)       = " << _req_inj_blocked_on_size_limit << endl;
  cout << "      Request injects blocked by WS tokens (cycles)                  = " << _req_inj_blocked_on_ws_tokens << endl;
  cout << "      GET req injects blocked by get limit (targets * cycles)        = " << _read_req_inj_blocked_on_xaction_limit << endl;
  cout << "      GET req injects blocked by get size limit (targets * cycles)   = " << _read_req_inj_blocked_on_size_limit << endl;
  cout << "      GET req injects blocked by global GET request limit (t * c)    = " << _get_req_inj_blocked_on_global_request_limit << endl;
  cout << "      GET req injects blocked by global inbound data limit (t*c)     = " << _get_req_inj_blocked_on_global_get_data_limit << endl;
  cout << "      Response injects blocked by xaction limit (targets * cycles)   = " << _resp_inj_blocked_on_xaction_limit << endl;
  cout << "      Response injects blocked by size limit (targets * cycles)      = " << _resp_inj_blocked_on_size_limit << endl;
  cout << "      Response injects blocked by WS tokens (cycles)                 = " << _resp_inj_blocked_on_ws_tokens << endl;
  cout << "      RGET req injects blocked by xaction limit (targets * cycles)   = " << _rget_req_inj_blocked_on_xaction_limit << endl;
  cout << "      RGET req injects blocked by RGET req limit (targets * cycles)  = " << _rget_req_inj_blocked_on_rget_req_limit << endl;
  cout << "      RGET req injects blocked by size limit (targets * cycles)      = " << _rget_req_inj_blocked_on_size_limit << endl;
  cout << "      RGET req injects blocked by inbound data limit (targets * cycles) = " << _rget_req_inj_blocked_on_inbound_data_limit << endl;
  cout << "      RGET GET req injects blocked by get limit (targets * cycles)       = " << _rget_get_req_inj_blocked_on_get_limit << endl;
  cout << "      RGET GET req injects blocked by inbound data limit (targets * cycles) = " << _rget_get_req_inj_blocked_on_inbound_data_limit << endl;
  cout << "      RGET GET req injects blocked by global GET request limit (t * c)   = " << _rget_get_req_inj_blocked_on_global_request_limit << endl;
  cout << "      RGET GET req injects blocked by global inbound data limit (t*c)    = " << _rget_get_req_inj_blocked_on_global_get_data_limit << endl;
  cout << "      RGET GET req injects blocked by WS tokens (cycles)                 = " << _rget_get_req_inj_blocked_on_ws_tokens << endl;
  if (_cycles_new_flit_not_injected !=
      (_cycles_inj_present_but_blocked + _cycles_link_avail_no_new_flits + _cycles_retransmitting +
       _cycles_new_flit_not_injected_due_to_packet_processing_penalty +
       _cycles_new_flit_not_injected_due_to_staging_buffer_full)) {
    cout << "    UNCHARACTERIZED IDLE CYCLES: "
         << (int)(_cycles_new_flit_not_injected - (_cycles_inj_present_but_blocked + _cycles_link_avail_no_new_flits + _cycles_retransmitting +
                                                   _cycles_new_flit_not_injected_due_to_packet_processing_penalty +
                                                   _cycles_new_flit_not_injected_due_to_staging_buffer_full))
         << endl;
  }


  return checks_passed;
}


unsigned int EndPoint::_GetFlitsDroppedForRgetConversion() {
  return _flits_dropped_for_rget_conversion;
}


void EndPoint::DumpAckQueue() {
    cout << "Ack Queue Dump for node: " << _nodeid << endl;

  for (unsigned int i = 0; i < _received_ack_queue.size(); i++) {
    // dest_iter->first is the dest id
    cout << "[seqnum: " << _received_ack_queue[i].ack_seq_num << " src: " <<
      _received_ack_queue[i].target << "]"<< endl;
  }
}
void EndPoint::DumpOPB() {
  cout << _cur_time << ": " << Name() << ": OPB Dump" << endl;
  cout << "  Dest:  Retry State, Retry Index :   Seq Num: Flits" << endl;

  // Iterate over all keys (dest ids) in the OPB.
  map<int, deque< Flit * > >::iterator dest_iter;
  deque<Flit *>::iterator flit_iter;
  for ( dest_iter = _outstanding_packet_buffer.begin();
        dest_iter != _outstanding_packet_buffer.end();
        dest_iter++ ) {
    // dest_iter->first is the dest id
    DumpOPB(dest_iter->first);
  }
}

void EndPoint::DumpOPB(int dest) {
  cout << _cur_time << ": " << Name() << ": OPB Dump for dest: " << dest << endl;

  if (!_outstanding_packet_buffer[dest].empty()) {
    cout << "  " << dest << ":  ";

    // Show retry state
    cout << "Retry state:" << _retry_state_tracker[dest].dest_retry_state << ", OPB flit index:"
         << _retry_state_tracker[dest].nack_replay_opb_flit_index << ".    ";

    // For each dest, show the outstanding packets
    for ( deque<Flit *>::iterator flit_iter = _outstanding_packet_buffer[dest].begin();
          flit_iter != _outstanding_packet_buffer[dest].end();
          flit_iter++ ) {

      if ((*flit_iter)->head) {
        if ((*flit_iter)->type == Flit::READ_REPLY) {
          cout << "      " << (*flit_iter)->packet_seq_num << " (" << flit_type_strings[(*flit_iter)->type]
               << ": rspto:" << (*flit_iter)->response_to_seq_num << "): ";
        } else {
//        cout << "      " << (*flit_iter)->pid << ": ";
          cout << "      " << (*flit_iter)->packet_seq_num << " (" << flit_type_strings[(*flit_iter)->type]
               << " pkt " << (*flit_iter)->pid << "): ";
//        cout << "      " << (*flit_iter)->src << ": ";
//        cout << "      " << (*flit_iter)->ctime << ": ";
        }
      }
      cout << (*flit_iter)->id << " (" << (*flit_iter)->expire_time << "), ";

    }
    cout << endl;
  }
}

void EndPoint::DumpInjectionQueues() {
  /*unsigned int flits = 0;*/

  for (int cl = 0; cl < _classes; cl++) {
    for (unsigned int target = 0; target < _endpoints; target++) {
      if (_injection_buffer[cl][target].size() > 0) {
        cout << _cur_time << ": " << Name() << ": Injection Queue Dump for target: " << target
             << ", # flits: " << _injection_buffer[cl][target].size() << endl;
        /*flits++;*/

        for ( list<Flit *>::iterator flit_iter = _injection_buffer[cl][target].begin();
              flit_iter != _injection_buffer[cl][target].end();
              flit_iter++ ) {
          cout << "   pid: " << (*flit_iter)->pid << ", id: " << (*flit_iter)->id << ", dest: "
               << (*flit_iter)->dest << ", type: " << flit_type_strings[(*flit_iter)->type]
               << ", resp_to_seq_num: " << (*flit_iter)->response_to_seq_num
               << ", ctime: " << (*flit_iter)->ctime
               << endl;
        }
      }
    }
  }

//  if (flits == 0) {
//    cout << _cur_time << ": " << Name() << ": Injection Queues all empty" << endl;
//  }
}

void EndPoint::DumpReplyQueues() {
  for (int cl = 0; cl < _classes; cl++) {
    for (unsigned int target = 0; target < _endpoints; target++) {
      if (_repliesPending[target].size() > 0) {
        cout << _cur_time << ": " << Name() << ": Reply Queue Dump for target: " << target
             << ", # flits: " << _repliesPending[target].size() << endl;

        for ( list<Flit *>::iterator flit_iter = _repliesPending[target].begin();
              flit_iter != _repliesPending[target].end();
              flit_iter++ ) {
cout << "   pid: " << (*flit_iter)->pid << ", id: " << (*flit_iter)->id << ", dest: "
     << (*flit_iter)->dest << ", head: " << (*flit_iter)->head << ", tail: " << (*flit_iter)->tail
     << ", type: " << flit_type_strings[(*flit_iter)->type]
     << ", resp_to_seq_num: " << (*flit_iter)->response_to_seq_num
     << ", ctime: " << (*flit_iter)->ctime
     << endl;
        }

      }
    }
  }
}

void EndPoint::DumpRgetGetRequestQueues() {
  for (unsigned int queue_idx = 0; queue_idx < _num_rget_get_req_queues; queue_idx++) {
    if (_rget_get_req_queues[queue_idx].size() > 0) {
      cout << _cur_time << ": " << Name() << ": RGET GET Request Queue Dump for target: " << queue_idx
           << ", # flits: " << _rget_get_req_queues[queue_idx].size() << endl;
      for ( list<Flit *>::iterator flit_iter = _rget_get_req_queues[queue_idx].begin();
              flit_iter != _rget_get_req_queues[queue_idx].end();
              flit_iter++ ) {
        cout << "   pid: " << (*flit_iter)->pid << ", id: " << (*flit_iter)->id << ", dest: "
             << (*flit_iter)->dest << ", head: " << (*flit_iter)->head << ", tail: " << (*flit_iter)->tail
             << ", type: " << flit_type_strings[(*flit_iter)->type]
             << ", resp_to_seq_num: " << (*flit_iter)->response_to_seq_num
             << ", ctime: " << (*flit_iter)->ctime
             << endl;
      }
    }
  }
}

void EndPoint::DumpPacketSequenceTracker() {
  cout << _cur_time << ": " << Name() << ": Packet sequence number" << endl;
  cout << "  Dest  |  CurNum" << endl;
  map<int, int>::iterator dest_iter;
  for ( dest_iter = _packet_seq_num.begin();
        dest_iter != _packet_seq_num.end();
        dest_iter++ ) {
    cout << "      " << dest_iter->first << "  |  " << dest_iter->second << endl;
  }
}

void EndPoint::DumpWeightedSchedulerState() {
  cout << _cur_time << ": " << Name() << ": WS State:   ";
  for (unsigned int queue_type = NEW_CMD_Q; queue_type < NUM_QUEUE_TYPES; ++queue_type) {
    if (queue_type == NEW_CMD_Q) {
      for (unsigned int queue_idx = 0; queue_idx < _num_injection_queues; queue_idx++) {
        printf("%3d, ", _weighted_sched_queue_tokens[NEW_CMD_Q][queue_idx]);
      }
    } else if (queue_type == READ_REPLY_Q) {
      for (unsigned int queue_idx = 0; queue_idx < _num_response_queues; queue_idx++) {
        printf("%3d, ", _weighted_sched_queue_tokens[READ_REPLY_Q][queue_idx]);
      }
    } else {
      for (unsigned int queue_idx = 0; queue_idx < _num_rget_get_req_queues; queue_idx++) {
        printf("%3d, ", _weighted_sched_queue_tokens[RGET_GET_REQ_Q][queue_idx]);
      }
    }
  }

  cout << endl;
}
