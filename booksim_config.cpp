/*booksim_config.cpp
 *
 *Contains all the configurable parameters in a network
 *
 */


#include "booksim.hpp"
#include "booksim_config.hpp"

BookSimConfig::BookSimConfig( )
{ 
  //========================================================
  // Network options
  //========================================================

  // Control of virtual channel speculation
  AddStrField( "speculative", "1" ) ;
  _int_map["speculative"] = 1 ;

  // Channel length listing file
  AddStrField( "channel_file", "" ) ;

  // Control alloction of VC to packet types
  AddStrField( "partition_vcs", "1" ) ;
  _int_map["partition_vcs"] = 1 ;

  AddStrField( "read_request_begin_vc", "0");
  _int_map["read_request_begin_vc"] = 0;
  AddStrField( "read_request_end_vc", "3");
  _int_map["read_request_end_vc"] = 3;

  AddStrField( "write_request_begin_vc", "4");
  _int_map["write_request_begin_vc"] = 4;
  AddStrField( "write_request_end_vc", "5");
  _int_map["write_request_end_vc"] = 5;

  AddStrField( "read_reply_begin_vc", "6");
  _int_map["read_reply_begin_vc"] = 6;
  AddStrField( "read_reply_end_vc", "7");
  _int_map["read_reply_end_vc"] = 7;


  AddStrField( "write_reply_begin_vc", "8");
  _int_map["write_reply_begin_vc"] = 8;
  AddStrField( "write_reply_end_vc", "11");
  _int_map["write_reply_end_vc"] = 11;

  // Control Injection of Packets into Replicated Networks
  AddStrField( "read_request_subnet", "0" );
  _int_map["read_request_subnet"] = 0;

  AddStrField( "read_reply_subnet", "1" );
  _int_map["read_reply_subnet"] = 1;

  AddStrField( "write_request_subnet", "1" );
  _int_map["write_request_subnet"] = 1;

  AddStrField( "write_reply_subnet", "0" );
  _int_map["write_reply_subnet"] = 0;

  // TCC Simulation Traffic Trace
  AddStrField( "trace_file", "trace-file.txt" ) ;

  //==== Multi-node topology options =======================

  AddStrField( "topology", "torus" );

  _int_map["k"] = 8; //network radix
  _int_map["n"] = 2; //network dimension
  _int_map["c"] = 1; //concentration
  _int_map["x"] = 1; //number of routers in X
  _int_map["y"] = 1; //number of routers in Y
  _int_map["xr"] = 1; //number of nodes per router in X
  _int_map["yr"] = 1; //number of nodes per router in Y
  _int_map["limit"] = 0; //how many of the nodes are actually used


  AddStrField( "routing_function", "none" );
  AddStrField( "selection_function", "random" );

  _int_map["link_failures"] = 0; //legacy
  _int_map["fail_seed"]     = 0; //legacy
  _int_map["wire_delay"] = 0; //legacy

  //==== Single-node options ===============================

  _int_map["in_ports"]  = 5;
  _int_map["out_ports"] = 5;

  _int_map["voq"] = 0;

  //========================================================
  // Router options
  //========================================================

  //==== General options ===================================

  AddStrField( "router", "iq" ); 

  _int_map["output_delay"] = 0;
  _int_map["credit_delay"] = 0;
  _float_map["internal_speedup"] = 1.0;

  //==== Input-queued ======================================

  _int_map["num_vcs"]         = 1;  
  _int_map["vc_buf_size"]     = 4;  
  _int_map["vc_buffer_pool"]  = 0;

  _int_map["wait_for_tail_credit"] = 1; // reallocate a VC before a tail credit?

  _int_map["hold_switch_for_packet"] = 0; // hold a switch config for the entire packet

  _int_map["input_speedup"]     = 1;  // expansion of input ports into crossbar
  _int_map["output_speedup"]    = 1;  // expansion of output ports into crossbar

  _int_map["routing_delay"]    = 0;  
  _int_map["vc_alloc_delay"]   = 0;  
  _int_map["sw_alloc_delay"]   = 0;  
  _int_map["st_prepare_delay"] = 0;
  _int_map["st_final_delay"]   = 0;

  //==== Event-driven =====================================

  _int_map["vct"] = 0; 

  //==== Allocators ========================================

  AddStrField( "vc_allocator", "max_size" ); 
  AddStrField( "sw_allocator", "max_size" ); 

  _int_map["alloc_iters"] = 1;

  //==== Traffic ========================================

  AddStrField( "traffic", "uniform" );

  _int_map["perm_seed"] = 0;         // seed value for random perms traffic

  _float_map["injection_rate"]       = 0.0; //if 0.0 assumes it is batch mode
  _int_map["const_flits_per_packet"] = 1; //flits per packet

  AddStrField( "injection_process", "bernoulli" );

  _float_map["burst_alpha"] = 0.5; // burst interval
  _float_map["burst_beta"]  = 0.5; // burst length

  AddStrField( "priority", "age" );  // message priorities

  _int_map["batch_size"] = 100;
  _int_map["max_outstanding_requests"] = 4;

  _int_map["read_request_size"]  = 1;
  _int_map["write_request_size"] = 1;
  _int_map["read_reply_size"]    = 1;
  _int_map["write_reply_size"]   = 1;

  //==== Simulation parameters ==========================

  // types:
  //   latency    - average + latency distribution for a particular injection rate
  //   throughput - sustained throughput for a particular injection rate

  AddStrField( "sim_type", "latency" );

  _int_map["warmup_periods"] = 0; // number of samples periods to "warm-up" the simulation

  _int_map["sample_period"] = 1000; // how long between measurements
  _int_map["max_samples"]   = 20;   // maximum number of sample periods in a simulation

  _float_map["latency_thres"] = 1000.0; // if avg. latency exceeds the threshold, assume unstable

  _int_map["sim_count"]     = 1;   // number of simulations to perform

  _int_map["auto_periods"]  = 1;   // non-zero for the simulator to automatically
                                   //   control the length of warm-up and the
                                   //   total length of the simulation

  _int_map["include_queuing"] = 1; // non-zero includes source queuing latency

  _int_map["reorder"]         = 0;  // know what you're doing

  _int_map["flit_timing"]     = 0;  // know what you're doing
  _int_map["split_packets"]   = 0;  // know what you're doing

  _int_map["seed"]            = 0;
}
