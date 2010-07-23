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

  // Channel length listing file
  AddStrField( "channel_file", "" ) ;

  // Use read/write request reply scheme
  
  _int_map["use_read_write"] = 0;

  _int_map["read_request_begin_vc"] = 0;
  _int_map["read_request_end_vc"] = 5;

  _int_map["write_request_begin_vc"] = 2;
  _int_map["write_request_end_vc"] = 7;

  _int_map["read_reply_begin_vc"] = 8;
  _int_map["read_reply_end_vc"] = 13;

  _int_map["write_reply_begin_vc"] = 10;
  _int_map["write_reply_end_vc"] = 15;

  // Physical sub-networks
  _int_map["physical_subnetworks"] = 1;

  // Control Injection of Packets into Replicated Networks
  _int_map["read_request_subnet"] = 0;

  _int_map["read_reply_subnet"] = 0;

  _int_map["write_request_subnet"] = 0;

  _int_map["write_reply_subnet"] = 0;

  // TCC Simulation Traffic Trace
  AddStrField( "trace_file", "trace-file.txt" ) ;

  //==== Topology options =======================
  //important
  AddStrField( "topology", "torus" );
  _int_map["k"] = 8; //network radix
  _int_map["n"] = 2; //network dimension
  _int_map["c"] = 1; //concentration
  AddStrField( "routing_function", "none" );
  _int_map["use_noc_latency"] = 1;

  //not critical
  _int_map["x"] = 8; //number of routers in X
  _int_map["y"] = 8; //number of routers in Y
  _int_map["xr"] = 1; //number of nodes per router in X only if c>1
  _int_map["yr"] = 1; //number of nodes per router in Y only if c>1
  _int_map["limit"] = 0; //how many of the nodes are actually used


  _int_map["link_failures"] = 0; //legacy
  _int_map["fail_seed"]     = 0; //legacy

  //==== Cmesh topology options =======================
  _int_map["express_channels"] = 0; //for Cmesh only, 0=no express channels
  //==== Single-node options ===============================

  _int_map["in_ports"]  = 5;
  _int_map["out_ports"] = 5;
  _int_map["voq"] = 0; //output queuing

  //========================================================
  // Router options
  //========================================================

  //==== General options ===================================

  AddStrField( "router", "iq" ); 

  _int_map["output_delay"] = 0;
  _int_map["credit_delay"] = 0;
  _float_map["internal_speedup"] = 1.0;

  //==== Input-queued ======================================

  // Control of virtual channel speculation
  _int_map["speculative"] = 0 ;
  
  // what to use to inhibit speculative allocator grants?
  AddStrField("filter_spec_grants", "confl_nonspec_gnts");

  _int_map["num_vcs"]         = 16;  
  _int_map["vc_buf_size"]     = 8;  

  _int_map["wait_for_tail_credit"] = 0; // reallocate a VC before a tail credit?
  _int_map["vc_busy_when_full"] = 0; // mark VCs as in use when they have no credit available
  _int_map["vc_priority_donation"] = 0; // allow high-priority flits to donate their priority to low-priority that they are queued up behind
  _int_map["replies_inherit_priority"] = 0; // whenusing request-reply traffic (use_read_write=1) with age-based priority, make replies inherit their corresponding requests' age

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

  AddStrField( "vc_allocator", "islip" ); 
  AddStrField( "sw_allocator", "islip" ); 
  
  AddStrField( "vc_alloc_arb_type", "round_robin" );
  AddStrField( "sw_alloc_arb_type", "round_robin" );
  
  _int_map["alloc_iters"] = 1;
  
  // dub: allow setting the number of iterations for each allocator separately
  // (a value of 0 indicates it should inherit its value from alloc_iters)
  _int_map["vc_alloc_iters"] = 0;
  _int_map["sw_alloc_iters"] = 0;

  //==== Traffic ========================================

  AddStrField( "traffic", "uniform" );

  _int_map["perm_seed"] = 0;         // seed value for random permuation trafficpattern generator

  _float_map["injection_rate"]       = 0.1; //if 0.0 assumes it is batch mode
  _int_map["injection_rate_uses_flits"] = 0;

  _int_map["const_flits_per_packet"] = 1; //use  read_request_size etc insted

  AddStrField( "injection_process", "bernoulli" );

  _float_map["burst_alpha"] = 0.5; // burst interval
  _float_map["burst_beta"]  = 0.5; // burst length

  AddStrField( "priority", "none" );  // message priorities

  _int_map["batch_size"] = 1000;
  _int_map["batch_count"] = 1;
  _int_map["max_outstanding_requests"] = 4;

  _int_map["read_request_size"]  = 1; //flit per packet
  _int_map["write_request_size"] = 1; //flit per packet
  _int_map["read_reply_size"]    = 1; //flit per packet
  _int_map["write_reply_size"]   = 1; //flit per packet

  //==== Simulation parameters ==========================

  // types:
  //   latency    - average + latency distribution for a particular injection rate
  //   throughput - sustained throughput for a particular injection rate

  AddStrField( "sim_type", "latency" );

  _int_map["warmup_periods"] = 3; // number of samples periods to "warm-up" the simulation

  _int_map["sample_period"] = 1000; // how long between measurements
  _int_map["max_samples"]   = 10;   // maximum number of sample periods in a simulation

  _float_map["latency_thres"] = 500.0; // if avg. latency exceeds the threshold, assume unstable
  _float_map["warmup_thres"] = 0.05; // consider warmed up once relative change in latency and throughput between successive iterations is smaller than this

  // consider converged once relative change in latency / throughput between successive iterations is smaller than this
  _float_map["stopping_thres"] = 0.05;
  _float_map["acc_stopping_thres"] = 0.05;

  _int_map["sim_count"]     = 1;   // number of simulations to perform


  _int_map["include_queuing"] =1; // non-zero includes source queuing latency

  //  _int_map["reorder"]         = 0;  // know what you're doing

  //_int_map["flit_timing"]     = 0;  // know what you're doing
  //_int_map["split_packets"]   = 0;  // know what you're doing

  _int_map["seed"]            = 0; //random seed for simulation, e.g. traffic 

  _int_map["print_activity"] = 0;

  _int_map["print_csv_results"] = 0;
  _int_map["print_vc_stats"] = 0;

  _int_map["drain_measured_only"] = 0;

  _int_map["viewer_trace"] = 0;

  AddStrField("watch_file", "");
  AddStrField("watch_out", "");

  AddStrField("stats_out", "");
  AddStrField("flow_out", "");
  
  //==================Power model params=====================
  _int_map["sim_power"] = 0;
  AddStrField("power_output_file","pwr_tmp");
  AddStrField("tech_file", "../utils/temp");
  _int_map["channel_width"] = 128;
  _int_map["channel_sweep"] = 0;

  //==================Network file===========================
  AddStrField("network_file","");
}


//A list of important simulator for the booksim gui, anything else not listed here is still included
//but just not very organized
vector< pair<string, vector< string> > > *BookSimConfig::GetImportantMap(){
  //Vector of 5 categories, each category is a vector of potions. Maps don't work because it autosorts
  vector< pair<string, vector< string> > > *important = new  vector< pair<string, vector< string> > >;
  important->push_back( make_pair( "Topology", vector<string>() ));
  (*important)[0].second.push_back("topology");
  (*important)[0].second.push_back("k");
  (*important)[0].second.push_back("n");
  (*important)[0].second.push_back("c");
  (*important)[0].second.push_back( "routing_function");
  (*important)[0].second.push_back("use_noc_latency");

  important->push_back(make_pair("Router", vector<string>()));
  (*important)[1].second.push_back("router");
  (*important)[1].second.push_back("num_vcs");
  (*important)[1].second.push_back("vc_buf_size");
  (*important)[1].second.push_back("routing_delay");
  (*important)[1].second.push_back("vc_alloc_delay");
  (*important)[1].second.push_back("sw_alloc_delay");
  (*important)[1].second.push_back("st_prepare_delay");
  (*important)[1].second.push_back("st_final_delay");

  important->push_back(make_pair("Allocator", vector<string>()));
  (*important)[2].second.push_back("vc_allocator");
  (*important)[2].second.push_back("vc_alloc_arb_type");
  (*important)[2].second.push_back("sw_allocator");
  (*important)[2].second.push_back("sw_alloc_arb_type");
  (*important)[2].second.push_back( "priority");
  (*important)[2].second.push_back("speculative");

  important->push_back(make_pair("Simulation", vector<string>()));
 (*important)[3].second.push_back("traffic");
 (*important)[3].second.push_back("injection_rate");
 (*important)[3].second.push_back("injection_rate_uses_flits");
 (*important)[3].second.push_back("sim_type");
 (*important)[3].second.push_back("latency_thres");
 (*important)[3].second.push_back("const_flits_per_packet");
 (*important)[3].second.push_back("injection_process");
 (*important)[3].second.push_back("sample_period");

  important->push_back(make_pair("Statistics", vector<string>()));
  (*important)[4].second.push_back("print_activity");
  (*important)[4].second.push_back("print_csv_results");
  (*important)[4].second.push_back("print_vc_stats");
  (*important)[4].second.push_back("stats_out");
  (*important)[4].second.push_back("sim_power");
  (*important)[4].second.push_back("power_output_file");


  return important;
}



PowerConfig::PowerConfig( )
{ 

  _int_map["H_INVD2"] = 0;
  _int_map["W_INVD2"] = 0;
  _int_map["H_DFQD1"] = 0;
  _int_map["W_DFQD1"] = 0;
  _int_map["H_ND2D1"] = 0;
  _int_map["W_ND2D1"] = 0;
  _int_map["H_SRAM"] = 0;
  _int_map["W_SRAM"] = 0;
  _float_map["Vdd"] = 0;
  _float_map["R"] = 0;
  _float_map["IoffSRAM"] = 0;
  _float_map["IoffP"] = 0;
  _float_map["IoffN"] = 0;
  _float_map["Cg_pwr"] = 0;
  _float_map["Cd_pwr"] = 0;
  _float_map["Cgdl"] = 0;
  _float_map["Cg"] = 0;
  _float_map["Cd"] = 0;
  _float_map["LAMBDA"] = 0;
  _float_map["MetalPitch"] = 0;
  _float_map["Rw"] = 0;
  _float_map["Cw_gnd"] = 0;
  _float_map["Cw_cpl"] = 0;
  _float_map["wire_length"] = 0;

}
