#ifndef _ENDPOINT_HPP_
#define _ENDPOINT_HPP_

#include <list>
#include <map>
#include <set>
#include <cassert>
#include <fstream>
#include <random>

#include "trafficmanager.hpp"
#include "packet_reply_info.hpp"
#include "random_utils.hpp"
#include "wkld_msg.hpp"


// EndPoints function as both the initiator of transactions and the receiver.
class EndPoint : public TimedModule {


private:
  int _nodeid;
  TrafficManager * _parent;
  int _cur_time;

  unsigned int _endpoints;
  int _subnets;
  int _classes;

  bool _debug_enabled;

  bool _trace_debug;

  // Config
  int _cycles_before_standalone_ack;
  // If that much time has elapsed, then we should just send a standalone ack before sending a packet
  int _cycles_before_standalone_ack_has_priority;
  int _packets_before_standalone_ack;
  unsigned int _opb_max_pkt_occupancy;  // In packets
  unsigned int _opb_pkt_occupancy;
  unsigned int _inj_buf_depth;
  bool _use_crediting;
  int _retry_timer_timeout;
  int _max_retry_attempts;
  int _response_timer_timeout;
  int _rget_req_pull_timeout;
  unsigned int _xaction_limit_per_dest;            // RATE_LIMIT.reliable_pkts
  unsigned int _get_limit_per_dest;                // GET_RATE_LIMIT.outstanding_pkts
  unsigned int _rget_req_limit_per_dest;           // RGET.pkt_credit
  unsigned int _xaction_size_limit_per_dest;       // RATE_LIMIT.reliable_size
  unsigned int _get_inbound_size_limit_per_dest;   // GET_RATE_LIMIT.outstanding_data
  unsigned int _rget_inbound_size_limit_per_dest;  // RGET.data_credit
  unsigned int _global_get_request_limit;          // GLOBAL_GET_RATE_LIMIT.outstd_pkts
  unsigned int _global_get_req_size_limit;         // GLOBAL_GET_RATE_LIMIT.outstd_data
  bool _use_new_rget_metering;
  bool _sack_enabled;
  unsigned int _sack_vec_length;
  unsigned long _sack_vec_mask;
  bool _debug_sack;
  unsigned int _max_receivable_pkts_after_drop;

  enum tx_queue_type_e {NULL_Q = -1, NEW_CMD_Q, READ_REPLY_Q, RGET_GET_REQ_Q, NUM_QUEUE_TYPES};
  vector<string> flit_type_strings;
  vector<string> retry_mode_strings;
  tx_queue_type_e _new_packet_transmission_in_progress;
  unsigned int _min_packet_processing_penalty;
  unsigned int _next_packet_injection_blocked_until;
  unsigned int _num_flits_waiting_to_inject;
  unsigned int _max_flits_waiting_to_inject;
  struct flit_time_pair {
    Flit * flit;
    int time;
    bool new_flit;
  };
  queue< flit_time_pair > _flits_waiting_to_inject;

  unsigned int _ack_processing_latency;
  unsigned int _rsp_processing_latency;
  unsigned int _req_processing_latency;
  unsigned int _rget_processing_latency;
  float _put_to_rget_conversion_rate;

  // TODO: add select PUT_NOOP logic
  bool _put_to_noop;

  // Vectored by class
  vector<int64_t> _qtime;
  vector<bool> _qdrained;
  // (Inner vector by dest)
  vector< vector < list < Flit * > > > _injection_buffer;
  vector< vector < unsigned int > > _full_packets_in_inj_buf;
  vector<long> _generated_packets;
  long _generated_packets_full_sim;
  vector<long> _generated_flits;
  long _generated_flits_full_sim;
  vector<long> _injected_flits; // per class
  vector<long> _sent_flits;
  vector<long> _sent_packets;
  long _sent_data_flits;
  vector<long> _new_sent_flits;
  vector<long> _new_sent_packets;
  long _new_sent_data_flits;
  vector<long> _received_flits;
  vector<long> _received_packets;
  long _received_data_flits;
  vector<int> _last_class;  // subnets
  vector<vector<int> > _last_vc;  // subnets,classes

  // Vectored by subnets
  vector<BufferState *> _buf_states;
  vector< list < Flit * > > _incoming_flit_queue;
//  list<PacketReplyInfo*> _repliesPending;
  vector< list< Flit * > > _repliesPending;

  // RGET GET Request queues at the target
  vector< list< Flit * > > _rget_get_req_queues;



  // ********************* Tracking of packet transmission *********************
  // Vectored by dest
  // An endpoint must keep sequence numbers separate for each dest.
  map<int, int> _packet_seq_num;
  map<int, int> _recvd_seq_num_vec;
  // Temporary storage for packets that have been "moved aside" from the main
  // _injection_buffer because the target was in the middle of a retry.
  // This structure only needs to hold a maximum of one packet per dest because
  // the condition that requires it is a very narrow corner case.  (Once we are
  // in a retry state, we prevent the generation of new packets to the given
  // dest, but we may have previously generated the packet right before the
  // retry began, and the new packet has not started injecting into the network
  // yet.)  We need to move the packet aside so packets targeting other dests
  // can be generated.
//  map<unsigned int, list< Flit * > > _pending_packet_injections;
  unsigned int _num_injection_queues;
  unsigned int _inj_buf_rr_idx;
  unsigned int _num_response_queues;
  unsigned int _rsp_buf_rr_idx;
  unsigned int _num_rget_get_req_queues;
  unsigned int _rget_get_req_buf_rr_idx;
  unsigned int _num_tx_queues;
  tx_queue_type_e _tx_queue_type_rr_selector;
  enum tx_arb_mode_e {ROUND_ROBIN, WEIGHTED};
  tx_arb_mode_e _tx_arb_mode;
  // Per queue
  vector< vector<int> > _weighted_sched_queue_tokens;
  int _weighted_sched_req_init_tokens;
  int _weighted_sched_rsp_init_tokens;
  int _weighted_sched_incr_tokens;
  int _weighted_sched_rsp_slots_per_req_slot;
  bool _debug_ws;

  struct timer_struct {
    int time;
    int dest;
    int seq_num;
  };
  list<timer_struct> _retry_timer_expiration_queue;
  list<timer_struct> _response_timer_expiration_queue;


  // For E2E reliability.  Hold packets here until an ack is received.
  // Outer structure is for each destination.
  // Middle is for each packet.
  // Inner is the list of flits in the packet.
  map<int, deque< Flit * > > _outstanding_packet_buffer;

  map<int, unsigned int> _outstanding_xactions_per_dest;      // RATE_LIMIT.reliable_pkts
  map<int, unsigned int> _outstanding_put_data_per_dest;
  map<int, deque<unsigned int> > _outstanding_put_data_samples_per_dest;
  map<int, unsigned int> _new_write_ack_data_per_dest;
  map<int, deque<int> > _new_write_ack_data_samples_per_dest;
  map<int, unsigned int> _periods_since_last_transition;
  int _enable_adaptive_rget;
  int _rget_convert_sample_period;
  float _rget_convert_unacked_perc;
  float _rget_revert_acked_perc;
  unsigned int _rget_convert_num_samples;
  unsigned int _rget_min_samples_since_last_transition;
  unsigned int _rget_convert_min_data_before_convert;
  map<unsigned int, bool>         _converting_puts_to_rgets;
  unsigned int _outstanding_xactions_all_dests_stat;
  map<unsigned int, unsigned int> _outstanding_gets_per_dest;          // GET_RATE_LIMIT.outstanding_pkts
  map<unsigned int, unsigned int> _outstanding_rget_reqs_per_dest;     // RGET.pkt_credit
  map<unsigned int, unsigned int> _outstanding_outbound_data_per_dest; // RATE_LIMIT.reliable_size
  unsigned int _outstanding_outbound_data_all_dests_stat;
  map<unsigned int, unsigned int> _outstanding_inbound_data_per_dest;  // GET_RATE_LIMIT.outstanding_pkts
  map<unsigned int, unsigned int> _outstanding_rget_inbound_data_per_dest;  // RGET.data_credit

  unsigned int _outstanding_global_get_requests;          // GLOBAL_GET_RATE_LIMIT.outstd_pkts
  unsigned int _outstanding_global_get_req_inbound_data;  // GLOBAL_GET_RATE_LIMIT.outstd_data

  map<unsigned int, unsigned int> _opb_occupancy_map;
  unsigned int _opb_ways;
  unsigned int _opb_dest_idx_mask;
  unsigned int _opb_seq_num_idx_mask;
  unsigned int _opb_seq_num_bits;
  unsigned int _packet_gen_attempts;
  unsigned int _opb_insertion_conflicts;
  unsigned int _req_inj_blocked_on_xaction_limit;
  unsigned int _req_inj_blocked_on_size_limit;
  unsigned int _req_inj_blocked_on_ws_tokens;
  unsigned int _read_req_inj_blocked_on_xaction_limit;
  unsigned int _read_req_inj_blocked_on_size_limit;
  unsigned int _resp_inj_blocked_on_xaction_limit;
  unsigned int _resp_inj_blocked_on_size_limit;
  unsigned int _resp_inj_blocked_on_ws_tokens;
  unsigned int _rget_req_inj_blocked_on_xaction_limit;
  unsigned int _rget_req_inj_blocked_on_rget_req_limit;
  unsigned int _rget_req_inj_blocked_on_size_limit;
  unsigned int _rget_req_inj_blocked_on_inbound_data_limit;
  unsigned int _rget_get_req_inj_blocked_on_get_limit;
  unsigned int _rget_get_req_inj_blocked_on_inbound_data_limit;
  unsigned int _rget_get_req_inj_blocked_on_ws_tokens;
  unsigned int _rget_get_req_inj_blocked_on_global_request_limit;
  unsigned int _rget_get_req_inj_blocked_on_global_get_data_limit;
  unsigned int _get_req_inj_blocked_on_global_request_limit;
  unsigned int _get_req_inj_blocked_on_global_get_data_limit;

  unsigned int _packets_retired;
  unsigned int _packets_retired_full_sim;
  unsigned int _flits_retired;
  unsigned int _flits_retired_full_sim;
  unsigned int _data_flits_retired;
  unsigned int _data_flits_retired_full_sim;
  unsigned int _retry_timeouts;
  unsigned int _good_packets_received;
  unsigned int _good_packets_write_received; // Includes both write and rget_get_reply
  unsigned int _good_flits_received;
  unsigned int _good_data_flits_received;
  unsigned int _good_packets_received_full_sim;
  unsigned int _good_flits_received_full_sim;
  unsigned int _good_data_flits_received_full_sim;
  unsigned int _duplicate_packets_received;
  unsigned int _duplicate_flits_received;
  unsigned int _duplicate_packets_received_full_sim;
  unsigned int _duplicate_flits_received_full_sim;
  unsigned int _bad_packets_received;
  unsigned int _bad_flits_received;
  unsigned int _bad_packets_received_full_sim;
  unsigned int _bad_flits_received_full_sim;
  unsigned int _flits_dropped_for_rget_conversion;
  unsigned int _packets_dequeued;

  // *********************** Tracking of retransmissions ***********************
  // Vectored by dest, the value indicates which entry in the OPB is being
  // retransmitted.
  enum dest_retry_state_enum {IDLE, NACK_BASED, SACK_BASED, TIMEOUT_BASED};
  struct dest_retry_record {
    dest_retry_state_enum dest_retry_state;
    int nack_replay_opb_flit_index;
    // If a packet is in the process of a timeout-based retransmission when an
    // ACK/NACK is received, wait until retransmission of that packet completes
    // before clearing it from the OPB.  Such pending acks are stored here.
    // Only 1 needs to be stored since we take the highest ACK received.

    // (Note that if an ack is received while a nack-based replay is in
    // progress, we pend the ACK here, regardless of where in the retry
    // sequence we are.)
    int pending_ack;

    bool sack;
    unsigned long orig_sack_vec;
    unsigned long sack_vec;
    int seq_num_in_progress;
    int orig_ack_seq_num;
  };
  struct retrans_record {
    int dest;
    int seq_num;
  };
  retrans_record _timedout_packet_retransmit_in_progress;
  map<unsigned int, dest_retry_record> _retry_state_tracker;
  // Need to store all nacks received that have not yet been serviced
  deque<int> _pending_nack_replays;

  struct recvd_ack_record {
    int time;
    int subnet;
    int target;
    int ack_seq_num;
    int nack_seq_num;
    long flit_id;
    bool is_standalone;
    bool sack;
    unsigned long sack_vec;
  };
  deque<recvd_ack_record> _received_ack_queue;

  struct pending_rsp_record {
    unsigned int source;
    Flit::FlitType type;
    int reply_size;
    int time;
    bool record;
    int cl;
    int req_seq_num;
    int rget_data_size;
    WorkloadMessagePtr data;
    bool watch;
  };
  deque<pending_rsp_record> _pending_inbound_response_queue;
  deque<pending_rsp_record> _pending_outbound_response_queue;

  // Put queue performance modeling
  struct put_wait_queue_record {
    long pid;
    int size;
    int src;
    double remaining_process_size;
    Flit * homa_flit;
  };
  struct load_balance_queue_record {
    Flit * flit;
    int size;
  };




  struct mypolicy_host_control_connection_record {

    // -----------------------------------------------------------------------
    // Target states
    // -----------------------------------------------------------------------

    int periodic_buffer_occupancy;
    int periodic_ack_occupancy;

    // This is used to keep track wither initiator is retransmitting
    int highest_bad_seq_num_from_initiator;
    bool initiator_retransmitting;

    // To keep track of nack sent to each connection
    int put_drop_counter;

    // Keeps track of the latest position in the ack queue for a particular flow, so we can't insert anything before that
    int last_index_in_ack_queue;

    bool space_after_NACK_reserved;

    // When we dequeue from sepeculative_ack_queue, we suppress an ack that is
    // supposed to go to one stream, send one to the stream that needs it
    // By updating the speculative alllowance size to the value dequeued from
    // speculative_ack_queue
    // This variable is cleared back to zero when an ack has been sent back
    int speculative_ack_allowance_size;
    int earliest_accum_ack_shared_time;

    // -----------------------------------------------------------------------
    //  Initiator states
    // -----------------------------------------------------------------------

    int ecn_count;
    int ecn_total;
    double ecn_running_percent;

    int halt_active;
    int halt_state;

    // For controlling sending packet frequency
    int send_allowance_counter_size;
    bool must_retry_at_least_one_packet;

    // For timing out in halt state if havne't received ack for a long time
    int time_last_ack_recvd;

    // Keep track of the ack sequence number that was last received
    // Used to halt sending packets if host side is congested
    int last_valid_ack_seq_num_recvd;

    // This is for receiving multiple NACKs, need to pend change of opb_idx so that
    // current packet can finish sending (can't cut in the middle of a packet)
    // Normally -1
    int pending_nack_seq_num;

    // Storing for TCP-like, not making a new struct for now
    unsigned int tcplikepolicy_cwd;
    unsigned int tcplikepolicy_ssthresh;

  };

  struct to_send_ack_queue_record {
    int type;
    int seq_num;
    int latest_time_to_ack;
    int size;
    int source;
  };

  struct mypolicy_host_control_endpoint_record {

      // -----------------------------------------------------------------------
      //  Target states
      // -----------------------------------------------------------------------

      deque<to_send_ack_queue_record> ack_queue;
    // when a NACK replay is in progress form initiator,
    // and another packet is dropped, we enqueue speculative ack
    // to be sent back if needed in the expense of streams that have receiveed
    // more acks
    // There is a finite queue depth this can go
      deque<to_send_ack_queue_record> speculative_ack_queue;


      int acked_data_in_queue;
      int data_dequeued_but_need_acked;

      int num_initiator_retransmitting;

      bool host_congestion_enabled;
      bool max_ratio_valid;
      int max_occupancy_src;
      int periodic_total_occupancy;
      int total_packet_occupy;
      int next_fairness_request_time;
      int next_fairness_reset_time;

      bool suppress_request; // Suppressing duplicate ack
      unsigned int suppress_target;
      // Duplicate Ack supression active
      unsigned int suppress_active;

      // this is for reservation for space right after NACK, should be multiples of MTU
      int reserved_space;


      // host bandwidth is in flit/ cycle, can be less than one
      double current_host_bandwith;
      //At this cycle,change host bandwidth
      int next_change_host_bandwdith_time;

      std::lognormal_distribution<double> * interarrival_logn;
      int next_change_bandwidth_time;
      bool host_bandwidth_is_slow;
      default_random_engine generator;

      // -----------------------------------------------------------------------
      //  Initiator states
      // -----------------------------------------------------------------------
      //
      //

      int ecn_next_check_period;

      Stats * latency;
  };

  enum policy_type {HC_NO_POLICY = 0, HC_MY_POLICY, HC_TCP_LIKE_POLICY, HC_ECN_POLICY, HC_HOMA_POLICY};
  struct mypolicy_host_control_constant_record {

    // -----------------------------------------------------------------------
    //  Initiator
    // -----------------------------------------------------------------------
    int policy;

    int max_packet_send_per_ack;
    int max_ack_before_send_packet;
    int time_before_halt_state_timeout;
    int delayed_ack_threshold;

    int fairness_reset_period;
    int fairness_sampling_time;
    // Only if the buffer occupancy
    // is greater than this do we care
    int fairness_diff_threshold;

    int NACK_reservation_size;
    // This is the number of incremented acks, that we're going to skip all
    // duplicate acks in between. just a misnomer
    int suppress_duplicate_ack_max;

    // Amount of time we wait until we assume the
    // window reduced signal will not come
    int ecn_period;
    double ecn_param_g;
    double ecn_threshold_percent;
    int ecn_threshold;

    // Storing for TCP-like, not making a new struct for now
    unsigned int tcplikepolicy_MSS;

    // Preemptive insertion: Allow ack to be put in front of another stream for
    // load balance purposes
    bool ack_queue_preemptive_insertion_enabled;
    bool load_balance_queue_enabled;
    bool speculative_ack_enabled;

    unsigned int speculative_ack_queue_size;

    // -----------------------------------------------------------------------
    //  Target
    // -----------------------------------------------------------------------

    int coalescing_timeout;
    int fairness_packet_count_threshold;
    int time_before_shared_ack_timeout;

    double host_bandwidth_high;
    double host_bandwidth_low;
  };

  struct put_buffer_metadata {
    // Wait queue simulation

    //constant
    unsigned int queue_size;
    unsigned int load_balance_queue_size;
    int header_latency, header_flit_num;

    int normal_length, slow_length;

    // queue metadata, state
    deque<put_wait_queue_record> queue;

    // load_balancing_queue
    // Packets admitted to this queue can still be dropped for loadbalancing
    deque<load_balance_queue_record> load_balance_queue;

    bool slow;
    int remaining;
    int load_balance_queue_remaining;
    int latency_length_remaining;

    // Packet processing does not directly match with cycle time
    // So conservatively processing a round-up version of the cycle
    // and so have left over savings for next cycle
    double left_over_cycle_saving_from_previous_packet;
    int next_bursty_time;

    //stats
    int packet_dropped;
    int packet_dropped_full;
  };

  map<unsigned int, mypolicy_host_control_connection_record> _mypolicy_connections;
  mypolicy_host_control_constant_record _mypolicy_constant;
  mypolicy_host_control_endpoint_record _mypolicy_endpoint;
  put_buffer_metadata _put_buffer_meta;

  // May not even matter at all
  int _estimate_round_trip_cycle = 4000; // TODO make it into a configuration

  // ************* Tracking of received packets and ACK responses *************
  // Tracking for packets being received.  Used to check that all flits of a
  // packet are accounted for.
  int _incoming_packet_src;
  int _incoming_packet_pid;
  int _incoming_packet_seq;
  int _incoming_packet_flit_countdown;
  int _incoming_packet_flit_total;

  // Data structure for tracking packets received and ACKs to return, vectored
  // by initiator of the packet (ie. the target of the ack).

  // Keep track of acks we need to return to initiators.  Note that NONE was
  // removed.  If we have no new ACKs to send, we just repeat the last one.
  //   0 : ack to send
  //   1 : nack to send
  enum ack_resp_state {ACK, NACK, SACK};
  struct ack_response_record {
    // Keep track of the sequence number of the last packet successfully received.
    int last_valid_seq_num_recvd;
    // Keep track of the sequence number of the last packet that is ok to be acked
    // This is used for delayed acknowledgement. does not necessarily mean an ack
    // has been sent for this sequence number
    int last_valid_seq_num_recvd_and_ackd;
    int last_valid_seq_num_recvd_and_ready_to_ack;

    // We prefer to piggyback acks, but if too much time passes after a packet was
    // received, and we still haven't sent a piggyacked ack, then send a
    // standalone ack.
    int time_last_valid_unacked_packet_recvd;
    int packets_recvd_since_last_ack;


    // An ACK, NACK, or NONE that is waiting to be sent to the initiator.
    ack_resp_state outstanding_ack_type_to_return;
    // If a packet was received out of sequence, we only send one NACK.  If
    // subsequent packets are also out of sequence, we do nothing.
    bool already_nacked_bad_seq_num;

    int time_last_valid_packet_recvd;
    int time_last_ack_sent;

    // SACK
    unsigned long sack_vec;
  };
  // Vectored by the message initiator
  map<unsigned int, ack_response_record> _ack_response_state;


  // Stats
  unsigned int _cycles_generation_not_attempted;
  unsigned int _cycles_gen_attempted_but_blocked;
  unsigned int _cycles_new_flit_not_injected;
  unsigned int _cycles_new_flit_not_injected_due_to_packet_processing_penalty;
  unsigned int _cycles_new_flit_not_injected_due_to_staging_buffer_full;
  unsigned int _cycles_inj_present_but_blocked;
  unsigned int _cycles_link_avail_no_new_flits;
  unsigned int _cycles_retransmitting;
  unsigned int _packets_retransmitted;
  unsigned int _packets_retransmitted_full_sim;
  unsigned int _flits_retransmitted_full_sim;
  unsigned int _max_packet_retries_full_sim;
  unsigned int _cycles_inj_all_blocked_on_timeout;
  unsigned int _max_outstanding_xactions_per_dest_stat;
  unsigned int _max_outstanding_xactions_all_dests_stat;
  unsigned int _max_total_outstanding_data_per_dest_stat;
  unsigned int _max_total_outstanding_outbound_data_all_dests_stat;
  unsigned int _nacks_sent;
  unsigned int _nacks_received;
  unsigned int _sacks_sent;
  unsigned int _sacks_received;
  unsigned int _puts_converted_to_rgets;

protected:
  // Timed module methods - for now these are empty since everything is driven
  // from trafficmanager.  That could be changed in the future.
  void ReadInputs() {};
  void Evaluate() {};
  void WriteOutputs() {};

public:
  EndPoint(Configuration const & config, TrafficManager * parent,
           const string & name, int nodeid);

  ~EndPoint();

  // TrafficManager calls the major endpoint functions in this order every cycle
  // (from its _Step function):
  //   _ReceiveFlit
  //   _ReceiveCredit
  //   _EvaluateNewPacketInjection
  //   _Step
  //   _ProcessReceivedFlits

  inline void _UpdateTime(int cur_time) { _cur_time = cur_time; }

  // Store received flits
  void _ReceiveFlit(int subnet, Flit * f);

  // Receive credits back from the network
  void _ReceiveCredit(int subnet, Credit * cred);

  inline int occupied_size() {
      return _put_buffer_meta.queue_size - _put_buffer_meta.remaining;
  }

  inline int lbq_occupied_size() {
      return _put_buffer_meta.load_balance_queue_size - _put_buffer_meta.load_balance_queue_remaining;
  }

  inline bool queue_depth_over_threshold(){
    return occupied_size() + _mypolicy_endpoint.reserved_space >
              _mypolicy_constant.delayed_ack_threshold;

  }

  inline double occupancy_ratio_period(int initiator){
    if (_mypolicy_endpoint.periodic_total_occupancy == 0)
      return 0;
    return _mypolicy_connections[initiator].periodic_buffer_occupancy / (double) _mypolicy_endpoint.periodic_total_occupancy;
  }

  // Packet generation and injection
  void _EvaluateNewPacketInjection();
    int  DecideWhetherToGeneratePacket( int cl, bool force_gen = false );
    void GeneratePacket( int & stype, int cl, int64_t time );
      void GeneratePacketFlits(int dest, Flit::FlitType packet_type, int size, int time,
                               bool record, int cl, int req_seq_num, int requested_data_transfer_size,
                               WorkloadMessagePtr data,
                               list< Flit * > * flit_list, bool watch_rsp);

  // Main _Step
  Flit * _Step(int subnet);
    Flit * find_flit_to_retransmit(BufferState * const dest_buf);
        bool packet_qualifies_for_retransmission(Flit::FlitType type, int dest, int size, int data_transfer_size);
      bool retransmission_blocked(Flit::FlitType type, int dest);
      int sack_vec_next_retrans(unsigned int sack_vec);
      int find_opb_idx_of_seq_num_head(int target, int seq_num);
      Flit * find_timed_out_flit_to_retransmit(BufferState * const dest_buf);
    Flit * find_new_flit_to_inject(BufferState * const dest_buf, int last_class, int class_limit, int subnet);
      Flit * find_new_flit_to_inject_from_multi_queue(tx_queue_type_e queue_type, int traffic_class,
                                                      BufferState * const dest_buf, int subnet);
        Flit * check_single_list_for_available_flit(list<Flit *> & flit_list, BufferState * const dest_buf, int subnet);
          Flit * head_packet_avail_and_qualified(list<Flit *> & packet_buf, int subnet);
            void convert_put_to_rget(list<Flit *> & flit_list);
              bool decide_on_put_to_rget_conversion(int dest);
            bool new_packet_qualifies_for_arb(Flit::FlitType type, int dest, int size, int data_transfer_size);
              void update_stat(unsigned int & stat, int change_amount);
              bool check_for_opb_insertion_conflict(int target);
        void pop_and_lock(list<Flit *> & flit_list, tx_queue_type_e queue_type,
                          unsigned int * rr_idx, unsigned int rr_max);
      void inject_blocked_by_tokens(BufferState * const dest_buf, int subnet);
      void increment_weighted_scheduler_tokens();
      void insert_flit_into_opb(Flit * flit);
    void insert_piggybacked_acks(Flit * flit);
        void update_flit_ack_if_needed_for_ecn(Flit * flit);
    Flit * manufacture_standalone_ack(int subnet, BufferState * dest_buf);
    bool has_priority_standalone_ack();
    Flit * inject_flit(BufferState * const dest_buf);
    void process_received_ack_queue();
      void process_received_acks(recvd_ack_record record);
      bool to_be_acked_packets_contain_put(int target, int seq_num_acked, bool nack_initiated = false);
      int calculate_to_be_acked_packet_size(int target, int seq_num_acked, bool nack_initiated = false, bool nack_non_zero = false);
      int calculate_to_be_acked_packet_size_by_index(int target, unsigned int opb_idx, unsigned int seq_num_acked, int & accum_packet_size);
      void target_reserve_put_space_for_initiator_if_needed(int initiator);
        void mypolicy_update_initiator_state_with_ack(recvd_ack_record record, int size);
        void mypolicy_update_periodic_occupancy(put_wait_queue_record * entrance, put_wait_queue_record * exit);
        void mypolicy_update_periodic_ack_occupancy(int source, int size);
        void mypolicy_update_target(put_wait_queue_record * entrance, put_wait_queue_record * exit);
        void reset_buffer_occupancy();
        void reset_ack_occupancy();
        void clear_opb_of_acked_packets(int target, int seq_num_acked, bool nack_initiated = false);
          unsigned int check_and_clear_opb_of_packet_by_index(int target, unsigned int opb_idx, unsigned int seq_num_acked);
            void clear_opb_of_packet_by_index(const int target, const unsigned int opb_idx);
              void clear_opb_of_flit_by_index(const int target, const unsigned int opb_idx);
          bool all_packets_in_opb_are_acked(const int target);
        void clear_opb_of_single_packet(int target, int seq_num_acked);
    void process_pending_outbound_response_queue();
    void process_pending_inbound_response_queue();
      void mark_response_received_in_opb(int target, int response_to_seq_num);
    void update_dequeued_state(put_wait_queue_record r);
    void update_host_bandwidth();
    void sender_process_ecn();

    void process_put_queue();
    void process_delayed_ack_if_needed();
    void _ClearStats();

    /*
     *   Step
     *       process_put_queue
     *          dequeue_put_queue
     *      process_delayed_ack_if_needed
     *
     *   _ProcessReceivedFlits
     *       UpdateAckAndReadResponseState
     */

    // Called from: find_flit_to_retransmit, check_single_list_for_available_flit, manufacture_standalone_ack
    void find_available_output_vc_for_packet(Flit * cf);

    // Called from: check_for_opb_insertion_conflict, insert_flit_into_opb, clear_opb_of_packet_by_index
    unsigned int get_opb_hash(int target, int seq_num);


  // Process received flits
  Credit * _ProcessReceivedFlits(int subnet, Flit * & received_flit_ptr);
    void insert_packet_into_load_balance_queue_or_put_queue(Flit * flit, int packet_size);
    void shift_load_balance_queue_to_data_queue_if_needed();
    void UpdateAckAndReadResponseState(Flit * flit, int packet_size);
      void setupNackState(int source, int seq_num, Flit * flit, int packet_size);
      void QueueResponse(Flit * flit);
    void HomaEnqueue(Flit * flit, int packet_size);
    void HomaAckQueueRecord(put_wait_queue_record r);



  // Called by TrafficManager for quiesce/end-of-run
  bool _InjectionQueueDrained(int c);
  bool _EndPointProcessingFinished();
  bool _OPBDrained();
  bool _AcksToReturn();
  bool _EndSimulation();
  unsigned int _GetFlitsDroppedForRgetConversion();

  bool InjectionBuffersEmpty(int c);
  bool InjectionBuffersNotEmptyButAllBlockedOnTimeout(int c);
  bool PendingRepliesDrained();
  bool PendingRgetGetRequestQueuesDrained();


  void DumpAckQueue();
  void DumpOPB();
  void DumpOPB(int dest);
  void DumpInjectionQueues();
  void DumpReplyQueues();
  void DumpRgetGetRequestQueues();
  void DumpPacketSequenceTracker();
  void DumpWeightedSchedulerState();
};

#endif
