// $Id$

/*
  Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this list
  of conditions and the following disclaimer.
  Redistributions in binary form must reproduce the above copyright notice, this 
  list of conditions and the following disclaimer in the documentation and/or 
  other materials provided with the distribution.
  Neither the name of the Stanford University nor the names of its contributors 
  may be used to endorse or promote products derived from this software with out 
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


//time benchmarks
#include <sys/time.h>
Stats* retired_s; 
Stats* retired_n;

Stats* gStatResEarly_POS;
Stats* gStatResEarly_NEG;
Stats* gStatReservationMismatch_POS;
Stats* gStatReservationMismatch_NEG;

deque<float>** gStatMonitorTransient;


#define ENABLE_STATS

/*transient shit*/
bool TRANSIENT_BURST = true;
bool TRANSIENT_ENABLE = false;
double*** transient_stat;
double** transient_ecn;
long** transient_ird;

int** transient_count;
int transient_prestart = 50000;
int transient_duration  = 1;
int transient_record_duration  = 1000;
int transient_start  = 100000;
float transient_rate = 1.0;
int transient_class = 1;
int transient_granularity = 1;
string transient_data_file;
bool transient_finalize = false;

//transmit reservation before as soon as flow is at the head of the flow buffer
bool FAST_RESERVATION_TRANSMIT=false;
//multiple flows to the same destination must share the same flow buffer
bool FLOW_DEST_MERGE = true;
//expiration check also occurs during VC allocation
bool VC_ALLOC_DROP = true;
//expiration time only count queuing delays
bool RESERVATION_QUEUING_DROP= false;
//retransmit unacked packets as soon as there is nothing else to send
bool FAST_RETRANSMIT_ENABLE = false;
//account for reservation overhead by increasing the reservation time by this factor
float RESERVATION_OVERHEAD_FACTOR = 1.05;
//account for the reservation/grant rtt
float RESERVATION_RTT = 1.0;
//flows with flits fewer than this is ignored by reservation
int RESERVATION_PACKET_THRESHOLD=64;
//each reservation only account for this many flits
int RESERVATION_CHUNK_LIMIT=256;
//debug, resrvation gtrant time is always zero (always succeed)
bool RESERVATION_ALWAYS_SUCCEED=false;
//debug, no speculation is ever set
bool RESERVATION_SPEC_OFF = false;
//flow buffers dont start the next reseration until this reservation's time window
//has passed
bool RESERVATION_POST_WAIT = false;
//use the next hop buffer occupancy to preemptively drop a speculative packet
bool RESERVATION_BUFFER_SIZE_DROP=false;
//send the reservation to the next segment with the last packet
bool RESERVATION_TAIL_RESERVE=false;



//expiration timer for IRD
int IRD_RESET_TIMER=1000;
//unmarked acks also decreaser IRD
bool ECN_TIMER_ONLY = true;
//buffer threshold to enable congestion
int ECN_BUFFER_THRESHOLD = 64;
//channel congestion threshold to identify root
int ECN_CONGEST_THRESHOLD=32;
//each IRD value delays pakcet injection by this amount
int IRD_SCALING_FACTOR = 1;
//increase function should higher than the decrease function
int ECN_IRD_INCREASE = 1;
//Maximum IRD
int ECN_IRD_LIMIT = 0;
//hysteresis for ECN
int ECN_BUFFER_HYSTERESIS=0;
int ECN_CREDIT_HYSTERESIS=0;
//ECN AIMD
bool ECN_AIMD = false;



int DEFAULT_CHANNEL_LATENCY;

#define WATCH_FLID -1
#define MAX(X,Y) ((X)>(Y)?(X):(Y))
#define MIN(X,Y) ((X)<(Y)?(X):(Y))

int TOTAL_SPEC_BUFFER=0;
Stats* gStatSpecCount;
Stats* gStatDropLateness;
Stats* gStatSurviveTTL;

map<int, vector<int> > gDropStats;
map<int, vector<int> > gChanDropStats;
int gExpirationTime = 0;

vector<int> gStatAckReceived;
vector<int> gStatAckEffective;
vector<int> gStatAckSent;

vector<int> gStatNackReceived;
vector<int> gStatNackEffective;
vector<int> gStatNackSent;

vector<int> gStatGrantReceived;
Stats** gStatGrantTimeNow;
Stats** gStatGrantTimeFuture;

vector<int> gStatReservationReceived;
Stats** gStatReservationTimeNow;
Stats** gStatReservationTimeFuture;

vector<int> gStatSpecSent;
vector<int> gStatSpecReceived;
vector<int> gStatSpecDuplicate;

vector<int> gStatNormSent;
vector<int> gStatNormReceived;
vector<int> gStatNormDuplicate;

vector<int> gStatLostPacket;

vector<vector<int> > gStatInjectVCDist;
vector<int> gStatInjectVCBlock;
vector<int> gStatInjectVCMiss;

vector<int> gStatFlowMerged;

Stats* gStatROBRange;

Stats* gStatFlowSenderLatency;
Stats* gStatFlowLatency;
Stats* gStatActiveFlowBuffers;
Stats* gStatReadyFlowBuffers;
Stats* gStatResponseBuffer;

Stats** gStatSpecNetworkLatency;
Stats** gStatPureNetworkLatency;
Stats* gStatAckLatency;
Stats* gStatNackLatency;
Stats* gStatResLatency;
Stats* gStatGrantLatency; 
Stats* gStatSpecLatency;
Stats* gStatNormLatency;
Stats* gStatSourceTrueLatency;
Stats* gStatSourceLatency;
Stats* gStatNackByPacket;

Stats* gStatFastRetransmit;

vector<long> gStatFlowStats;

Stats* gStatNackArrival;

vector<Stats*> gStatIRD;
vector<Stats*> gStatECN;

vector<int> gStatNodeReady;

map<int, int> gStatFlowSizes;

TrafficManager::TrafficManager( const Configuration &config, const vector<Network *> & net )
  : Module( 0, "traffic_manager" ), _net(net), _empty_network(false), _deadlock_timer(0), _last_id(-1), _last_pid(-1), _timed_mode(false), _warmup_time(-1), _drain_time(-1), _cur_id(0), _cur_pid(0), _cur_tid(0), _time(0),_stat_time(0)
{
  cout<<"size of ";
  cout<<sizeof(Flit)<<endl;

  gStatFlowStats.resize(FLOW_STAT_LIFETIME+1+1,0);

  _nodes = _net[0]->NumNodes( );
  _routers = _net[0]->NumRouters( );
  _num_vcs = config.GetInt("num_vcs");

  _max_flow_buffers = config.GetInt("flow_buffers");
  _max_flow_buffers = (_max_flow_buffers==0)?_nodes:_max_flow_buffers;

  _last_sent_norm_buffer.resize(_nodes,NULL);
  _last_sent_spec_buffer.resize(_nodes,NULL);
  _flow_buffer.resize(_nodes);
  _reservation_set.resize(_nodes);
  _active_set.resize(_nodes);
  _deactive_set.resize(_nodes);
  _sleep_set.resize(_nodes);
  


  _pending_flow.resize(_nodes,NULL);
  _flow_buffer_arb.resize(_nodes);
  _reservation_arb.resize(_nodes);
  for(int i = 0; i<_nodes; i++){
    _flow_buffer[i].resize(_max_flow_buffers,NULL);
    //_flow_buffer_arb[i] = new RoundRobinArbiter(this, "inject_arb",_max_flow_buffers);
    //_reservation_arb[i] = new RoundRobinArbiter(this, "reservation_arb",_max_flow_buffers);
    _flow_buffer_arb[i] = new LargeRoundRobinArbiter("inject_arb",_max_flow_buffers);
    _reservation_arb[i] = new LargeRoundRobinArbiter("reservation_arb",_max_flow_buffers);
    _flow_buffer_arb[i]->Clear();
    _reservation_arb[i]->Clear();
  }
 




  _rob.resize(_nodes);
  _reservation_schedule.resize(_nodes, 0);
  _response_packets.resize(_nodes);

  _cur_flid = 0;
  gExpirationTime =  config.GetInt("expiration_time");
  DEFAULT_CHANNEL_LATENCY = config.GetInt("default_channel_latency");

  TRANSIENT_BURST = (config.GetInt("transient_burst")==1);
  TRANSIENT_ENABLE = (config.GetInt("transient_enable")==1);
  transient_duration  = config.GetInt("transient_duration");
  transient_start  = config.GetInt("transient_start");
  transient_granularity  = config.GetInt("transient_granularity");
  transient_record_duration=config.GetInt("transient_stat_size");
  transient_data_file=config.GetStr("transient_data");
  transient_finalize = (config.GetInt("transient_finalize")==1);
  
  //transient
  transient_ecn = new double*[2];
  transient_ecn[0] = new double[transient_record_duration];
  transient_ecn[1] = new double[transient_record_duration];
  transient_ird = new long*[2];
  transient_ird[0] = new long[transient_record_duration];
  transient_ird[1] = new long[transient_record_duration];
  transient_stat = new double**[2]; //class
  transient_stat[0] = new double*[3]; //net_lat, lat, accepted
  transient_stat[1] = new double*[3]; 
  transient_stat[0][0] = new double[transient_record_duration];
  transient_stat[0][1] = new double[transient_record_duration];
  transient_stat[0][2] = new double[transient_record_duration];
  transient_stat[1][0] = new double[transient_record_duration];
  transient_stat[1][1] = new double[transient_record_duration];
  transient_stat[1][2] = new double[transient_record_duration];
  transient_count= new int*[2];
  transient_count[0] = new int[transient_record_duration];
  transient_count[1] = new int[transient_record_duration];
  
  for(int i = 0; i<transient_record_duration ; i++){
    transient_ecn[0][i] = 0.0;
    transient_ecn[1][i] = 0.0;

    transient_ird[0][i] = 0;
    transient_ird[1][i] = 0;

    transient_stat[0][0][i] = 0.0;
    transient_stat[0][1][i] = 0.0;
    transient_stat[0][2][i] = 0.0;
    transient_stat[1][0][i] = 0.0;
    transient_stat[1][1][i] = 0.0;
    transient_stat[1][2][i] = 0.0;
    transient_count[0][i]= 0;
    transient_count[1][i]= 0;

  }

  _LoadTransient(transient_data_file);

  FAST_RESERVATION_TRANSMIT = (config.GetInt("fast_reservation_transmit"));
  RESERVATION_OVERHEAD_FACTOR = config.GetFloat("reservation_overhead_factor");
  RESERVATION_RTT=config.GetFloat("reservation_rtt");
  RESERVATION_PACKET_THRESHOLD = config.GetInt("reservation_packet_threshold");
  RESERVATION_CHUNK_LIMIT=config.GetInt("reservation_chunk_limit");
  RESERVATION_ALWAYS_SUCCEED=(config.GetInt("reservation_always_succeed")==1);
  RESERVATION_SPEC_OFF = (config.GetInt("reservation_spec_off")==1);
  
  RESERVATION_QUEUING_DROP=(config.GetInt("reservation_queuing_drop")==1);
  RESERVATION_POST_WAIT = (config.GetInt("reservation_post_wait")==1);
  RESERVATION_BUFFER_SIZE_DROP=(config.GetInt("reservation_buffer_size_drop")==1);
  RESERVATION_TAIL_RESERVE=(config.GetInt("reservation_tail_reserve")==1);

  FLOW_DEST_MERGE= (config.GetInt("flow_merge")==1);
  FAST_RETRANSMIT_ENABLE = (config.GetInt("fast_retransmit")==1);
  IRD_RESET_TIMER = (config.GetInt("ird_reset_timer"));
  ECN_TIMER_ONLY = (config.GetInt("ecn_timer_only")==1);
  ECN_BUFFER_THRESHOLD = config.GetInt("ecn_buffer_threshold");
  ECN_CONGEST_THRESHOLD = config.GetInt("ecn_congestion_threshold");
  IRD_SCALING_FACTOR = config.GetInt("ird_scaling_factor");
  ECN_IRD_INCREASE = config.GetInt("ecn_ird_increase");
  ECN_IRD_LIMIT= config.GetInt("ecn_ird_limit");
  ECN_BUFFER_HYSTERESIS=config.GetInt("ecn_buffer_hysteresis");
  ECN_CREDIT_HYSTERESIS=config.GetInt("ecn_credit_hysteresis");
  ECN_AIMD = (config.GetInt("ecn_aimd")==1);
  cout<<"ECN_IRD_LIMIT "<<ECN_IRD_LIMIT<<endl;

#ifdef ENABLE_STATS

  gStatSpecCount= new Stats(this,"spec count",1.0, 1);
  gStatNodeReady.resize(_nodes,0);
  
  gStatAckReceived.resize(_nodes,0);
  gStatAckEffective.resize(_nodes,0);
  gStatAckSent.resize(_nodes,0);

  gStatNackReceived.resize(_nodes,0);
  gStatNackEffective.resize(_nodes,0);

  gStatNackSent.resize(_nodes,0);

  gStatGrantReceived.resize(_nodes,0);

  gStatReservationReceived.resize(_nodes,0);

  gStatSurviveTTL = new  Stats(this, "spec drop too early", 1.0, gExpirationTime);
  gStatDropLateness = new  Stats(this, "spec drop too early", 10.0, 100);
  gStatResEarly_POS = new Stats(this, "reservation too early", 10.0, 100);
  gStatResEarly_NEG = new Stats(this, "reservation too early", 10.0, 100);
  gStatReservationMismatch_POS = new Stats(this, "res mismatch", 10.0, 100);
  gStatReservationMismatch_NEG = new Stats(this, "res mismatch", 10.0, 100);
  
  gStatMonitorTransient=new deque<float>* [6];
  for(int i = 0; i<6; i++){
    gStatMonitorTransient[i]=new deque<float>;
  }

  gStatGrantTimeNow=new Stats*[_nodes];
  gStatGrantTimeFuture=new Stats*[_nodes];
  gStatReservationTimeNow=new Stats*[_nodes];
  gStatReservationTimeFuture=new Stats*[_nodes];

  for(int i = 0; i< _nodes; i++){
    ostringstream tmp_name;
    tmp_name << "grant_time_now_" << i;
    gStatGrantTimeNow[i] =  new Stats( this, tmp_name.str() , 10.0, 100 );
    tmp_name.str("");
    tmp_name << "grant_time_future_" << i;
    gStatGrantTimeFuture[i] =  new Stats( this, tmp_name.str() , 10.0, 100 );
    tmp_name.str("");
    tmp_name << "res_time_now_" << i;
    gStatReservationTimeNow[i] =  new Stats( this, tmp_name.str() , 10.0, 100 );
    tmp_name.str("");
    tmp_name << "res_time_future_" << i;
    gStatReservationTimeFuture[i] =  new Stats( this, tmp_name.str() , 10.0, 100 );
    tmp_name.str("");
  }

  gStatSpecSent.resize(_nodes,0);
  gStatSpecReceived.resize(_nodes,0);
  gStatSpecDuplicate.resize(_nodes,0);

  gStatNormSent.resize(_nodes,0);
  gStatNormReceived.resize(_nodes,0);
  gStatNormDuplicate.resize(_nodes,0);

  gStatLostPacket.resize(_nodes,0);
  gStatInjectVCDist.resize(_nodes);
  for(int i = 0; i<_nodes; i++){
    gStatInjectVCDist[i].resize(_num_vcs,0);
  }
  gStatInjectVCBlock.resize(_nodes,0);
  gStatInjectVCMiss.resize(_nodes,0);
  gStatFlowMerged.resize(_nodes,0);

  gStatROBRange =  new Stats( this, "rob_range" , 1.0, 300 );
  
  gStatFlowSenderLatency =  new Stats( this, "flow_sender_latency" , 5.0, 1000 );
  gStatFlowLatency=  new Stats( this, "flow_latency" , 10.0, 5000 );
  gStatActiveFlowBuffers=  new Stats( this, "sender_active_flows" , 1.0, 10 );
  gStatReadyFlowBuffers=  new Stats( this, "sender_ready_flows" , 1.0, 10 );
  gStatResponseBuffer=  new Stats( this, "response_range" , 1.0, 10 );
  
 
  gStatAckLatency=  new Stats( this, "ack_hist" , 1.0, 1000 );
  gStatNackLatency=  new Stats( this, "nack_hist" , 1.0, 1000 );
  gStatResLatency=  new Stats( this, "res_hist" , 1.0, 1000 );
  gStatGrantLatency=  new Stats( this, "grant_hist" , 1.0, 1000 );
  gStatSpecLatency=  new Stats( this, "spec_hist" , 1.0, 5000 );
  gStatNormLatency=  new Stats( this, "norm_hist" , 1.0, 5000 );

  gStatSourceLatency=  new Stats( this, "source_queue_hist" , 1.0, 5000 );
  gStatSourceTrueLatency=  new Stats( this, "source_truequeue_hist" , 1.0, 5000 );
 
  for(int i = 0; i<_nodes; i++){
    gStatECN.push_back( new Stats(this, "ecn", 1.0, 3));
    gStatIRD.push_back( new Stats(this, "ird", 1.0,ECN_IRD_LIMIT) );
  }
  gStatNackByPacket = new Stats(this, "nack_by_sn", 1.0, 1000);

  gStatFastRetransmit=  new Stats( this, "fast_retransmit" , 1.0,  100);
  gStatNackArrival = new Stats(this, "nack_arrival",1.0, 1000);
#endif

  //nodes higher than limit do not produce or receive packets
  //for default limit = sources

  _limit = config.GetInt( "limit" );
  if(_limit == 0){
    _limit = _nodes;
  }
  assert(_limit<=_nodes);
 
  _subnets = config.GetInt("subnets");
  assert(_subnets == 1);
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
  } else if ( priority == "none" ) {
    _pri_type = none;
  } else if ( priority == "other"){
    _pri_type = other;//custom
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
  gStatPureNetworkLatency = new Stats*[_classes];
  gStatSpecNetworkLatency = new Stats*[_classes];
  for(int c = 0; c < _classes; ++c) {
    gStatSpecNetworkLatency[c] =  new Stats( this, "spec_net_hist" , 1.0, 5000 );
    gStatPureNetworkLatency[c] =  new Stats( this, "net_hist" , 1.0, 5000 );
  }
  retired_s =  new Stats( this, "r_s_hist" , 1.0, 5000 );
  retired_n =  new Stats( this, "r_n_hist" , 1.0, 5000 );

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

  _flow_size = config.GetIntArray( "flow_size" );
  if(_flow_size.empty()) {
    _flow_size.push_back(config.GetInt("flow_size"));
  }
  _flow_size.resize(_classes, _flow_size.back());

  _flow_size_range = config.GetIntArray( "flow_size_range" );
  if(_flow_size_range.empty()) {
    _flow_size_range.push_back(config.GetInt("flow_size_range"));
  }
  _flow_size_range.resize(_classes, _flow_size_range.back());

  _flow_mix_mode=config.GetInt("flow_mix_mode");

  if(config.GetInt("injection_rate_uses_flits")){
    if(_flow_mix_mode == FLOW_MIX_SINGLE){
      for(int c = 0; c < _classes; ++c){
	_load[c] /= (double)_packet_size[c];
	_load[c] /= (double)_flow_size[c];
      }
    } else if(_flow_mix_mode == FLOW_MIX_RANGE){
      for(int c = 0; c < _classes; ++c){
	_load[c] /= (double)_packet_size[c];
	_load[c] /= (((double)_flow_size[c]*2+(double)_flow_size_range[c])/2.0);
	cout<<_load[c]<<endl;
      }

    } else if(_flow_mix_mode == FLOW_MIX_BIMOD){
      for(int c = 0; c < _classes; ++c){
	_load[c] /= (double)_packet_size[c];
	_load[c] /=(((double)_flow_size[c]*2+(double)_flow_size_range[c])/2.0);
      }
    } else 
      assert(false);
  }

  if(TRANSIENT_ENABLE){
    transient_rate = _load[transient_class];
    _load[transient_class] = 0.0;
  }

  _traffic = config.GetStrArray("traffic");
  _traffic.resize(_classes, _traffic.back());

  _traffic_function.clear();
  for(int c = 0; c < _classes; ++c) {
    map<string, tTrafficFunction>::const_iterator iter = gTrafficFunctionMap.find(_traffic[c]);
    if(iter == gTrafficFunctionMap.end()) {
      Error("Invalid traffic function: " + _traffic[c]);
    }
    _traffic_function.push_back(iter->second);
  }

  _class_priority = config.GetIntArray("class_priority"); 
  if(_class_priority.empty()) {
    _class_priority.push_back(config.GetInt("class_priority"));
  }
  _class_priority.resize(_classes, _class_priority.back());

  for(int c = 0; c < _classes; ++c) {
    int const & prio = _class_priority[c];
    if(_class_prio_map.count(prio) > 0) {
      _class_prio_map.find(prio)->second.second.push_back(c);
    } else {
      _class_prio_map.insert(make_pair(prio, make_pair(-1, vector<int>(1, c))));
    }
  }

  vector<string> inject = config.GetStrArray("injection_process");
  inject.resize(_classes, inject.back());

  _injection_process.clear();
  for(int c = 0; c < _classes; ++c) {
    map<string, tInjectionProcess>::iterator iter = gInjectionProcessMap.find(inject[c]);
    if(iter == gInjectionProcessMap.end()) {
      Error("Invalid injection process: " + inject[c]);
    }
    _injection_process.push_back(iter->second);
  }
  if(TRANSIENT_BURST){
    _burst_process = gInjectionProcessMap["burst"];
  }

  // ============ Injection VC states  ============ 
  _cut_through = (config.GetInt("cut_through")==1);
  _buf_states.resize(_nodes);
  for ( int source = 0; source < _nodes; ++source ) {
    _buf_states[source].resize(_subnets);
    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
      ostringstream tmp_name;
      tmp_name << "terminal_buf_state_" << source << "_" << subnet;
      _buf_states[source][subnet] = new BufferState( config, this, tmp_name.str( ) );
    }
  }

  // ============ Injection queues ============ 

  _qtime.resize(_nodes);
  _qdrained.resize(_nodes);
  //_partial_packets.resize(_nodes);

  for ( int s = 0; s < _nodes; ++s ) {
    _qtime[s].resize(_classes);
    _qdrained[s].resize(_classes);
    //_partial_packets[s].resize(_classes);
  }

  _total_in_flight_flits.resize(_classes);
  _measured_in_flight_flits.resize(_classes);
  _retired_packets.resize(_classes);

  _packets_sent.resize(_nodes);
  _batch_size = config.GetInt( "batch_size" );
  _batch_count = config.GetInt( "batch_count" );
  _repliesPending.resize(_nodes);
  _requestsOutstanding.resize(_nodes);
  _maxOutstanding = config.GetInt ("max_outstanding_requests");  

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
  _spec_frag_stats.resize(_classes);
  _overall_min_frag.resize(_classes);
  _overall_avg_frag.resize(_classes);
  _overall_max_frag.resize(_classes);

  //  _pair_plat.resize(_classes);
  //_pair_tlat.resize(_classes);
  
  _hop_stats.resize(_classes);
  
  _sent_data_flits.resize(_classes);
  _accepted_data_flits.resize(_classes);
  _sent_flits.resize(_classes);
  _accepted_flits.resize(_classes);
  
  _overall_accepted.resize(_classes);
  _overall_accepted_min.resize(_classes);

  for ( int c = 0; c < _classes; ++c ) {
    ostringstream tmp_name;
    tmp_name << "plat_stat_" << c;
    _plat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 5000 );
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
    tmp_name << "spec_frag_stat_" << c;
    _spec_frag_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 100 );
    _stats[tmp_name.str()] = _spec_frag_stats[c];
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

    //    _pair_plat[c].resize(_nodes*_nodes);
    //_pair_tlat[c].resize(_nodes*_nodes);

    _sent_data_flits[c].resize(_nodes,0);
    _accepted_data_flits[c].resize(_nodes,0);
    _sent_flits[c].resize(_nodes);
    _accepted_flits[c].resize(_nodes);
    
    for ( int i = 0; i < _nodes; ++i ) {
      tmp_name << "sent_stat_" << c << "_" << i;
      _sent_flits[c][i] = new Stats( this, tmp_name.str( ) );
      _stats[tmp_name.str()] = _sent_flits[c][i];
      tmp_name.str("");    
      
      for ( int j = 0; j < _nodes; ++j ) {
	//	tmp_name << "pair_plat_stat_" << c << "_" << i << "_" << j;
	//_pair_plat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
	//	_stats[tmp_name.str()] = _pair_plat[c][i*_nodes+j];
	tmp_name.str("");
	
	//	tmp_name << "pair_tlat_stat_" << c << "_" << i << "_" << j;
	//_pair_tlat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
	//	_stats[tmp_name.str()] = _pair_tlat[c][i*_nodes+j];
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

  if(config.GetInt("seed")==54321){
    cout<<"You have hit the secret code. Time random seed is on\n";
    int time_seed = int(time(NULL));
    cout<<"Today's seed is "<<time_seed<<endl;
    RandomSeed(time_seed);
  } else {
    RandomSeed(config.GetInt("seed"));
  }


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

  _warmup_cycles = (config.GetInt( "warmup_cycles")==0)?config.GetInt( "sample_period" ):config.GetInt( "warmup_cycles");
  _sample_period = config.GetInt( "sample_period" );
  _max_samples    = config.GetInt( "max_samples" );
  _warmup_periods = config.GetInt( "warmup_periods" );
  _forced_warmup = (config.GetInt("forced_warmup")==1);
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
  _print_vc_stats = config.GetInt( "print_vc_stats" );
  _deadlock_warn_timeout = config.GetInt( "deadlock_warn_timeout" );
  _drain_measured_only = config.GetInt( "drain_measured_only" );
  _no_drain = (config.GetInt("no_drain")==1);
  

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
  if(stats_out_file == "" || stats_out_file == "NULL") {
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

  for(int i = 0; i<_nodes; i++){
    delete _flow_buffer_arb[i];
    delete _reservation_arb[i];
  }
  for ( int subnet = 0; subnet < _subnets; ++subnet ) {
    for ( int source = 0; source < _nodes; ++source ) {
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
    delete _spec_frag_stats[c];
    delete _overall_min_frag[c];
    delete _overall_avg_frag[c];
    delete _overall_max_frag[c];
    
    delete _hop_stats[c];
    delete _overall_accepted[c];
    delete _overall_accepted_min[c];
    
    for ( int source = 0; source < _nodes; ++source ) {
      delete _sent_flits[c][source];
      
      for ( int dest = 0; dest < _nodes; ++dest ) {
	//	delete _pair_plat[c][source*_nodes+dest];
	//delete _pair_tlat[c][source*_nodes+dest];
      }
    }
    
    for ( int dest = 0; dest < _nodes; ++dest ) {
      delete _accepted_flits[c][dest];
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

  for(size_t i = 0; i<_flow_buffer.size();i++){
    for(size_t j = 0; j<_flow_buffer[i].size(); j++){
      if(_flow_buffer[i][j])
	delete _flow_buffer[i][j];
    }
    _flow_buffer[i].clear();
  }
  _flow_buffer.clear();
#ifdef ENABLE_STATS
 for ( int c = 0; c < _classes; ++c ) {
   delete gStatPureNetworkLatency[c];
   delete gStatSpecNetworkLatency[c];
 }
 delete [] gStatPureNetworkLatency;
 delete [] gStatSpecNetworkLatency;
 
 delete gStatSurviveTTL;
 delete gStatDropLateness;
 delete gStatResEarly_POS;
 delete gStatResEarly_NEG;
 delete gStatReservationMismatch_POS;
 delete gStatReservationMismatch_NEG;
 for(int i = 0; i< _nodes; i++){
 delete  gStatGrantTimeNow[i];
 delete  gStatGrantTimeFuture[i];
 delete  gStatReservationTimeNow[i];
 delete  gStatReservationTimeFuture[i];
 }
 delete [] gStatGrantTimeNow;
 delete [] gStatGrantTimeFuture;
 delete [] gStatReservationTimeNow;
 delete [] gStatReservationTimeFuture;

 delete gStatROBRange ;
 
 delete gStatFlowSenderLatency;
 delete gStatFlowLatency;
 delete gStatActiveFlowBuffers;
 delete  gStatReadyFlowBuffers;
 delete  gStatResponseBuffer;
  
 
 delete  gStatAckLatency;
 delete  gStatNackLatency;
 delete  gStatResLatency;
 delete  gStatGrantLatency;
 delete  gStatSpecLatency;
 delete  gStatNormLatency;

 delete  gStatSourceLatency;
 delete  gStatSourceTrueLatency;

  for(int i = 0; i<_nodes; i++){
    delete gStatECN[i];
    delete gStatIRD[i];
  }
  delete  gStatNackByPacket;

  delete gStatFastRetransmit;
  delete gStatNackArrival;

#endif
}

Flit* TrafficManager::IssueSpecial(int src, Flit* ff){
  Flit * f  = Flit::New();
  f->packet_size=1;
  f->flid = ff->flid;
  f->sn = ff->sn;
  f->id = _cur_id++;
  f->subnetwork = 0;
  f->src = src;
  f->dest = ff->src;
  f->time = _time;
  f->cl = ff->cl;
  f->type = Flit::ANY_TYPE;
  f->head = true;
  f->tail = true;
  f->vc = 0;
  f->flbid = ff->flbid;
  return f;
}

Flit* TrafficManager::DropPacket(int src, Flit* f){
  Flit* ff = IssueSpecial(f->src, f);
  ff->res_type = RES_TYPE_NACK;
  ff->pri = FLIT_PRI_NACK;
  ff->vc = RES_RESERVED_VCS-1;
  if(f->watch){
    ff->watch=true;
  }
  return ff;
}



void TrafficManager::_RetireFlit( Flit *f, int dest )
{
  Flit* ff;
  FlowROB* receive_rob = NULL;
  FlowBuffer* receive_flow_buffer  = NULL;

  switch(f->res_type){
  case RES_TYPE_ACK:
    if(gReservation){
#ifdef ENABLE_STATS
      gStatAckLatency->AddSample(_time-f->time);
      gStatAckReceived[dest]++;
#endif
      receive_flow_buffer = _flow_buffer[dest][f->flbid];
      if( receive_flow_buffer!=NULL &&
	  receive_flow_buffer->active() && 
	  receive_flow_buffer->fl->flid == f->flid){
#ifdef ENABLE_STATS
	gStatAckEffective[dest]+= receive_flow_buffer->ack(f->sn)?1:0;
#endif
	int flow_done_status = receive_flow_buffer->done();
	if(flow_done_status!=FLOW_DONE_NOT){
#ifdef ENABLE_STATS
	  if(receive_flow_buffer->fl->cl==0){
	    for(size_t i = 0; i<gStatFlowStats.size()-1; i++){
	      gStatFlowStats[i]+=receive_flow_buffer->_stats[i];
	    }
	    gStatFlowStats[gStatFlowStats.size()-1]++;
	  }
	  gStatFlowSenderLatency->AddSample(_time-receive_flow_buffer->fl->create_time);
	  gStatFastRetransmit->AddSample(receive_flow_buffer->_fast_retransmit);
#endif
	  if(flow_done_status==FLOW_DONE_DONE){
	    //Flow is done
	    _active_set[dest].erase(receive_flow_buffer);
	    _deactive_set[dest].insert(receive_flow_buffer);
	    _flow_buffer[dest][f->flbid]->Deactivate();
	  } else {
	    //Reactivate the flow, assign vc, and insertinto the correct queues
	    receive_flow_buffer->Reset();
	    _FlowVC(receive_flow_buffer);
	    _reservation_set[dest].insert(receive_flow_buffer);
	  }
	}
      }
      f->Free();
    } else if(gECN){
#ifdef ENABLE_STATS
      gStatAckLatency->AddSample(_time-f->time);
      gStatAckReceived[dest]++;
#endif
      receive_flow_buffer = _flow_buffer[dest][f->flbid];
      if( receive_flow_buffer!=NULL &&
	  receive_flow_buffer->_dest == f->src){
#ifdef ENABLE_STATS
	gStatAckEffective[dest]+= (f->becn)?1:0;
#endif
	receive_flow_buffer->ack(f->becn);	
      }
      if(f->becn && TRANSIENT_ENABLE){
	int accepted_index = int(float(_time-transient_start+transient_prestart)/float(transient_granularity));
	if(accepted_index>=0 && accepted_index < transient_record_duration){
	  transient_ecn[1][accepted_index]+= 1.0;
	}
      }
      f->Free();
    }
    return;
    break;
  case RES_TYPE_NACK:
#ifdef ENABLE_STATS
    gStatNackLatency->AddSample(_time-f->time);
    gStatNackByPacket->AddSample(f->sn);
    gStatNackReceived[dest]++;
#endif
    receive_flow_buffer = _flow_buffer[dest][f->flbid];
    if( receive_flow_buffer!=NULL &&
	receive_flow_buffer->active() && 
	receive_flow_buffer->fl->flid == f->flid){
      int temp = receive_flow_buffer->nack(f->sn)?1:0;
#ifdef ENABLE_STATS
      gStatNackArrival->AddSample(_time-receive_flow_buffer->fl->create_time);
      gStatNackEffective[dest]+= temp;
#endif
    }
    f->Free();
    return;
    break;
  case RES_TYPE_GRANT: 
#ifdef ENABLE_STATS
    gStatGrantLatency->AddSample(_time-f->time);
    gStatGrantReceived[dest]++;
#endif
    receive_flow_buffer = _flow_buffer[dest][f->flbid];
    assert(receive_flow_buffer->active());
    if( receive_flow_buffer!=NULL &&
	receive_flow_buffer->fl->flid == f->flid){

#ifdef ENABLE_STATS
      if(f->payload>_time){
	gStatGrantTimeFuture[dest]->AddSample(f->payload-_time);
      } else {
	gStatGrantTimeNow[dest]->AddSample(_time-f->payload);
      }
#endif
      
      receive_flow_buffer->grant(f->payload,
				 int(ceil(RESERVATION_RTT*float(_time-f->ntime))));
    }
    f->Free();
    return;
    break;
  case RES_TYPE_RES:
    assert(f->vc==RES_PACKET_VC);
#ifdef ENABLE_STATS
    gStatResLatency->AddSample(_time-f->time);
    gStatReservationReceived[dest]++;
#endif
    //find or create a reorder buffer
    //this maybe error IF spec shows up before reservaiton and frees the rob
    assert(_rob[dest].count(f->flid)!=0);    
    receive_rob = _rob[dest][f->flid];

    f = receive_rob->insert(f);
    if(f){
      ff = IssueSpecial(dest,f);
      ff->id=999;
      if(f->flid == WATCH_FLID){
	cout<<"Reservation received"<<endl;
	ff->watch = true;
      }
#ifdef ENABLE_STATS
      if(_reservation_schedule[dest]>_time){
	gStatReservationTimeFuture[dest]->AddSample(_reservation_schedule[dest]-_time);
      } else {
	gStatReservationTimeNow[dest]->AddSample(_time-_reservation_schedule[dest]);
      }
#endif
      //the return time is offset by the reservation packet latency 
      //to prevent schedule fragmentation
      _reservation_schedule[dest] = MAX(_time,   _reservation_schedule[dest]);
      ff->payload  =_reservation_schedule[dest];
      //this functionality has been moved tot he source
      //-int(ceil(RESERVATION_RTT*float(_time-f->ntime)));

      _reservation_schedule[dest] += int(ceil(float(f->payload)*RESERVATION_OVERHEAD_FACTOR));

      ff->res_type = RES_TYPE_GRANT;
      ff->pri = FLIT_PRI_GRANT;
      ff->vc = GRANT_PACKET_VC;
      _response_packets[dest].push_back(ff);
    }
    break;

  case RES_TYPE_SPEC:
#ifdef ENABLE_STATS
    gStatSpecReceived[dest]++;
#endif
    //for very large flows sometimes spec packets can jump infront of res packet
    //in these cases robs can mistakenly retire flits that are associated with the
    //next reservation

    if(_rob[dest].count(f->flid)==0 || !_rob[dest][f->flid]->sn_check(f->sn)){
      if(f->flid == WATCH_FLID){
	cout<<"spec destination nack sn "<<f->sn<<"\n";
      }
      //NACK
      if(f->head){
#ifdef ENABLE_STATS
	gStatNackSent[dest]++;
#endif
	ff = IssueSpecial(dest,f);
	ff->res_type = RES_TYPE_NACK;
	ff->pri = FLIT_PRI_NACK;
	ff->vc = RES_RESERVED_VCS-1;
	_response_packets[dest].push_back(ff);
      } 
      f->Free();
      f = NULL;
    } else {
      if(f->flid == WATCH_FLID){
	cout<<"spec insert sn "<<f->sn<<"\n";
      }
      receive_rob = _rob[dest][f->flid];
      f = receive_rob->insert(f);
      if(f==NULL){
#ifdef ENABLE_STATS
	gStatSpecDuplicate[dest]++;
#endif
      } else {
	_sent_data_flits[f->cl][f->src]++;
#ifdef ENABLE_STATS
	if(f->head){
	  gStatSurviveTTL->AddSample(f->exptime);
	}
	if(f->tail){
	  gStatSpecLatency->AddSample(_time-f->time);
	}
#endif
      }
      //ACK
      if(f!=NULL && f->tail){
#ifdef ENABLE_STATS
	gStatAckSent[dest]++;
#endif
	ff = IssueSpecial(dest,f);
	ff->sn = f->head_sn;
	ff->res_type = RES_TYPE_ACK;
	ff->pri  = FLIT_PRI_ACK;
	ff->vc =  RES_RESERVED_VCS-1;
	_response_packets[dest].push_back(ff);
      }
    }
    break;
  case RES_TYPE_NORM:
#ifdef ENABLE_STATS
    gStatNormReceived[dest]++;
#endif
    if(gReservation && !f->walkin){
      //find or create a reorder buffer
      if(_rob[dest].count(f->flid)==0){
#ifdef ENABLE_STATS
	gStatNormDuplicate[dest]++;
#endif
	f->Free();
	return;
      } else {
	receive_rob = _rob[dest][f->flid];
      }
      if(f->flid == WATCH_FLID){
	cout<<"normal insert sn "<<f->sn<<"\n";
      }
      f = receive_rob->insert(f);
      if(f==NULL){
#ifdef ENABLE_STATS
	gStatNormDuplicate[dest]++;
#endif
      } else {
	if(!f->head && f->payload!=-1){
	  //this should almost always be positive, unless the expected_latency
	  //was over estimated
	  if(_time-f->payload>=0){
	    gStatReservationMismatch_POS->AddSample(_time-f->payload);
	  } else {
	    gStatReservationMismatch_NEG->AddSample(-(_time-f->payload));
	  }
	} else 	if(f->head && f->payload!=-1 && RESERVATION_TAIL_RESERVE){
	  ff = IssueSpecial(dest,f);
	  ff->id=666;
	  if(f->flid == WATCH_FLID){
	    cout<<"End rservation Reservation received"<<endl;
	    ff->watch = true;
	  }
#ifdef ENABLE_STATS
	  if(_reservation_schedule[dest]>_time){
	    gStatReservationTimeFuture[dest]->AddSample(_reservation_schedule[dest]-_time);
	  } else {
	    gStatReservationTimeNow[dest]->AddSample(_time-_reservation_schedule[dest]);
	  }
#endif
	  //the return time is offset by the reservation packet latency 
	  //to prevent schedule fragmentation
	  _reservation_schedule[dest] = MAX(_time,   _reservation_schedule[dest]);
	  ff->payload  =_reservation_schedule[dest];
	  //this functionality has been moved tot he source
	  //-int(ceil(RESERVATION_RTT*float(_time-f->ntime)));

	  _reservation_schedule[dest] += int(ceil(float(f->payload)*RESERVATION_OVERHEAD_FACTOR));

	  ff->res_type = RES_TYPE_GRANT;
	  ff->pri = FLIT_PRI_GRANT;
	  ff->vc = GRANT_PACKET_VC;
	  _response_packets[dest].push_back(ff);
	}
	_sent_data_flits[f->cl][f->src]++;
	if(f->tail){
#ifdef ENABLE_STATS
	  gStatNormLatency->AddSample(_time-f->time);
#endif
	}
      }
    } else { //for other modes duplication normal is not possible
      _sent_data_flits[f->cl][f->src]++;
    }  
    if(gECN){
      //only send if fecn is active
#ifdef ENABLE_STATS
      if(f->head)
	gStatECN[f->src]->AddSample(f->fecn);
#endif
      if(f->head &&
	 f->fecn){
	if(TRANSIENT_ENABLE){
	  int accepted_index = int(float(_time-transient_start+transient_prestart)/float(transient_granularity));
	  if(accepted_index>=0 && accepted_index < transient_record_duration){
	    transient_ecn[0][accepted_index]+= 1.0;
	  }
	}
#ifdef ENABLE_STATS
	gStatAckSent[dest]++;
#endif
	ff = IssueSpecial(dest,f);
	ff->sn = f->head_sn;
	ff->res_type = RES_TYPE_ACK;
	ff->pri  = FLIT_PRI_ACK;
	ff->vc =  ECN_RESERVED_VCS-1;
	ff->becn = f->fecn;
	_response_packets[dest].push_back(ff);
      }
    }
    break;
  default:
    assert(false);
  }
  
  //duplicate packet would not pass this
  if(gReservation){
    if(f){
      //walk in flits passes last if but fails here
      if(receive_rob!=NULL){
#ifdef ENABLE_STATS
	//gStatROBRange->AddSample(receive_rob->range());
#endif
	if(receive_rob->done()){ //entire reservation chunk(s) has been received
#ifdef ENABLE_STATS
	  gStatFlowLatency->AddSample(GetSimTime()-receive_rob-> _flow_creation_time);
#endif
	  delete _rob[dest][f->flid];
	  _rob[dest].erase(f->flid); 
	}
	//rest of the code can't process this
	if(f->res_type == RES_TYPE_RES){
	  f->Free();
	  return;
	}
      }
    } else {
      return;
    }
  }

 
    
  if(f->watch){
    *gWatchOut << GetSimTime() << " | "
	       << "node" << dest << " | "
	       << "retire flit " << f->id
	       << "res type  "<<f->res_type
	       << "." << endl;
  }
  

  //this occurs, when the normal flit retires before the speculative
  if(_total_in_flight_flits[f->cl].count(f->id) == 0){
    //    gStatLostPacket[dest]++;
    return;
  }

 
  if(TRANSIENT_ENABLE && f){
    int accepted_index = int(float(_time-transient_start+transient_prestart)/float(transient_granularity));
    if(accepted_index>=0 && accepted_index < transient_record_duration){
      transient_stat[f->cl][2][accepted_index]+= 1.0;
    }
  }
  

  //Regular retire flit
  if(f->res_type==RES_TYPE_SPEC){
    retired_s->AddSample(f->atime - f->ntime);
  } else {
    retired_n->AddSample(f->atime - f->ntime);
  }
  _accepted_data_flits[f->cl][dest]++;
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
      //TODO single packets fuck up if a second packet shows up really really late
      //then it will try to be retired again??
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
      //      rinfo->ttime = f->ttime;
      rinfo->record = f->record;
      rinfo->type = f->type;
      _repliesDetails[f->id] = rinfo;
      _repliesPending[dest].push_back(f->id);
    } else {
      if ( f->watch ) { 
	*gWatchOut << GetSimTime() << " | "
		   << "node" << dest << " | "
	  //		   << "Completing transation " << f->tid
	  //		   << " (lat = " << f->atime - head->ttime
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
    if ( ( _sim_state == warming_up ) || (f->record ||_no_drain) ) {
      
      _hop_stats[f->cl]->AddSample( f->hops );

      if((_slowest_flit[f->cl] < 0) ||
	 (_plat_stats[f->cl]->Max() < (f->atime - f->time)))
	_slowest_flit[f->cl] = f->id;


      if(TRANSIENT_ENABLE && f){
	int transient_index = int(float(head->ntime-transient_start+transient_prestart)/float(transient_granularity));
	//maybe shoudl use flit creation time
	if(transient_index>=0 && transient_index < transient_record_duration){
	  transient_stat[f->cl][0][transient_index]+= (f->atime - head->ntime);
	  transient_stat[f->cl][1][transient_index]+= (f->atime - f->time);
	  transient_count[f->cl][transient_index]++;
	}

      }
  


      _plat_stats[f->cl]->AddSample( f->atime - f->time);
#ifdef ENABLE_STATS
      if(head->res_type==RES_TYPE_SPEC)
	gStatSpecNetworkLatency[f->cl]->AddSample( f->atime - head->ntime);
      
      gStatPureNetworkLatency[f->cl]->AddSample( f->atime - head->ntime);
#endif
      if(f->res_type==RES_TYPE_SPEC){
	_spec_frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );
      } else {
	_frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );
      }
      //      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY || f->type == Flit::ANY_TYPE)
	//	_tlat_stats[f->cl]->AddSample( f->atime - f->ttime );
   
      //      _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->time );
      if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY){
	//	_pair_tlat[f->cl][dest*_nodes+f->src]->AddSample( f->atime - f->ttime );
      }else if(f->type == Flit::ANY_TYPE){
	//	_pair_tlat[f->cl][f->src*_nodes+dest]->AddSample( f->atime - f->ttime );
      }
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
    if(_use_read_write[cl]){ //read write packets
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
	result = _packet_size[cl];
	_packets_sent[source]++;
	//here is means, how many flits can be waiting in the queue
	_requestsOutstanding[source]++;
      } 
    } 
  } else { //injection rate mode
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
      } else {

	//produce a packet
	if(_injection_process[cl]( source, _load[cl] )){
	
	  //coin toss to determine request type.
	  result = (RandomFloat() < 0.5) ? -2 : -1;

	} else {
	  result = 0;
	}
      } 
    } else { //normal mode
      bool transient_started = false;
      int source_time = _include_queuing==1 ? 
	_qtime[source][cl] : _time;
      if(TRANSIENT_ENABLE){
	if(source_time>=transient_start && source_time<transient_start+transient_duration){
	  transient_started =true;
	  _load[transient_class] = transient_rate;
	} else {
	  transient_started =false;
	  _load[transient_class] = 0.0;
	}
      }
      if(TRANSIENT_ENABLE && TRANSIENT_BURST && transient_started && cl == transient_class){
	cout<<"burst"<<endl;
	return _burst_process( source, _load[cl] ) ? 1 : 0;
      } else {
	return _injection_process[cl]( source, _load[cl] ) ? 1 : 0;
      }
    } 
  }
  return result;
}

#define NMAX 4
int TrafficManager::_GeneratePacket( flow* fl, int n)
{
  int generate  = MIN(n,NMAX);
  for(int nn = 0; nn<generate;nn++){  
    Flit::FlitType packet_type = Flit::ANY_TYPE;
    int size = _packet_size[fl->cl]; //input size 
    int ttime = fl->create_time;
    int pid = _cur_pid++;
    assert(_cur_pid);
    int tid = _cur_tid;
    bool record = false;
    bool watch = gWatchOut && ((_packets_to_watch.count(pid) > 0) ||
			       (_transactions_to_watch.count(tid) > 0));
  
    //    ++_cur_tid;
    //    assert(_cur_tid);
  
  

    if ( ( _sim_state == running ) ||
	 ( ( _sim_state == draining ) && ( fl->create_time < _drain_time ) ) ) {
      record = _measure_stats[fl->cl];
    }

    int subnetwork = ((packet_type == Flit::ANY_TYPE) ? 
		      RandomInt(_subnets-1) :
		      _subnet[packet_type]);
  
    if ( watch ) { 
      *gWatchOut << GetSimTime() << " | "
		 << "node" << fl->src << " | "
		 << "Enqueuing packet " << pid
		 << " at time " << fl->create_time
		 << "." << endl;
    }
  

    for ( int i = 0; i < size; ++i ) {
      Flit * f  = Flit::New();
      f ->flid = fl->flid;
      f->id     = _cur_id++;
      assert(_cur_id);
      f->pid    = pid;
      //      f->tid    = tid;
      f->watch  = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
      f->subnetwork = subnetwork;
      f->src    = fl->src;
      f->time   = fl->create_time;
      //      f->ttime  = ttime;
      f->record = record;
      f->cl     = fl->cl;
      f->sn = fl->sn++;
      //watchwatch
      if(f->id == -1){
	f->watch=true;;
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
	f->dest = fl->dest;
	f->packet_size=size;
      } else {
	f->head = false;
	f->dest = -1;
      }
      switch( _pri_type ) {
      case class_based:
	f->pri = _class_priority[fl->cl];
	assert(f->pri >= 0);
	break;
      case age_based:
	f->pri = numeric_limits<int>::max() - (_replies_inherit_priority ? ttime : fl->create_time);
	assert(f->pri >= 0);
	break;
      case sequence_based:
	f->pri = numeric_limits<int>::max() - _packets_sent[fl->src];
	assert(f->pri >= 0);
	break;
      default:
	f->pri = 0;
      }
      if ( i == ( size - 1 ) ) { // Tail flit
	f->head_sn = f->sn-size+1;
	f->tail = true;
	f->dest =  fl->dest;
      } else {
	f->tail = false;
      }
    
      f->vc  = -1;

      if ( f->watch ) { 
	*gWatchOut << GetSimTime() << " | "
		   << "node" << fl->src << " | "
		   << "Enqueuing flit " << f->id
		   << " flow "<<f->flid
		   << " (packet " << f->pid
		   << ") at time " << fl->create_time
		   << "." << endl;
      }

      fl->buffer->push( f );
    }
  }
  return generate;
}

void TrafficManager::_GenerateFlow( int source, int stype, int cl, int time ){
  assert(stype!=0);

  //refusing to generate packets for nodes greater than limit
  if(source >=_limit){
    return ;
  }
  
  int packet_destination; 

  packet_destination = _traffic_function[cl](source, _limit);

  if ((packet_destination <0) || (packet_destination >= _nodes)) {
    ostringstream err;
    err << "Incorrect packet destination " << packet_destination;
    Error( err.str( ) );
  }

  flow* fl = new flow;
  fl->flid = _cur_flid++;
  fl->vc = -1;
  int flow_packet_size = 0;
  if(_flow_mix_mode==FLOW_MIX_SINGLE){
    flow_packet_size  = _flow_size[cl];
  } else if (_flow_mix_mode == FLOW_MIX_RANGE){
    flow_packet_size  =  (RandomInt(_flow_size_range[cl])+_flow_size[cl]);
  } else if (_flow_mix_mode == FLOW_MIX_BIMOD){
    flow_packet_size  = (RandomInt(1)*_flow_size_range[cl]+_flow_size[cl]);
  }
  fl->flow_size = flow_packet_size*_packet_size[cl];
  gStatFlowSizes[flow_packet_size]++;
  fl->create_time = time;
  fl->data_to_generate = flow_packet_size;
  fl->src = source;
  fl->dest = packet_destination;
  fl->cl = cl;
  fl->sn = 0;

  assert(_pending_flow[source]==NULL);
  _pending_flow[source] = fl;
}

void TrafficManager::_FlowVC(FlowBuffer* flb){

	 
  Flit* f = flb->front();
  assert(f);
  assert(f->head);
  OutputSet route_set;
  _rf(NULL, f, 0, &route_set, true);
  set<OutputSet::sSetElement> const & os = route_set.GetSet();
  assert(os.size() == 1);
  OutputSet::sSetElement const & se = *os.begin();
  assert(se.output_port == 0);
  int const & vc_start = se.vc_start;
  int const & vc_end = se.vc_end;
  int const vc_count = vc_end - vc_start + 1;
  //VC assignment is randomized instead of round robined!!!
  int rvc = RandomInt(vc_count-1);
  int const vc = vc_start + rvc;
#ifdef ENABLE_STATS
  gStatInjectVCDist[f->src][vc]++;
#endif
  flb->_vc = vc; 

}

void TrafficManager::_Inject(){

  for ( int input = 0; input < _nodes; ++input ) {
    for ( int c = 0; c < _classes; ++c ) {      
      if (_pending_flow[input]==NULL){
	bool generated = false;
	if ( !_empty_network ) {
	  while( !generated && ( _qtime[input][c] <= _time ) ) {
	    int stype = _IssuePacket( input, c );
	    if ( stype != 0 ) { //generate a packet
	      _GenerateFlow( input, stype, c, 
			     _include_queuing==1 ? 
			     _qtime[input][c] : _time );	   
	      generated = true;
	    }
	    //this is not a request packet
	    //don't advance time
	    //shoudl always trigger
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
      
      //otherwise this next part breaks
      assert(  _max_flow_buffers == _nodes && FLOW_DEST_MERGE);

      if(_pending_flow[input]!=NULL){
	if(_flow_buffer[input][_pending_flow[input]->dest] != NULL && 
	   _flow_buffer[input][_pending_flow[input]->dest]->active()){
	  assert(_flow_buffer[input][_pending_flow[input]->dest]->_dest == _pending_flow[input]->dest);
	  //insert the flow into a flowbuffer with the same destination
	  if(FLOW_DEST_MERGE){
#ifdef ENABLE_STATS
	    gStatFlowMerged[input]++;
#endif
	    _flow_buffer[input][_pending_flow[input]->dest]->_flow_queue.push(_pending_flow[input]);
	    _pending_flow[input]=NULL;
	  }
	}
       
	//flow could have been taken care of above
	//otherwise find an opportutnity to insert
	if( _pending_flow[input]!=NULL) {	  
	  int empty_flow = _pending_flow[input]->dest;
	  assert(_flow_buffer[input][empty_flow] == NULL || 
		 !_flow_buffer[input][empty_flow]->active());

	  //create or active the flow	  
	  int mode = NORMAL_MODE; 
	  if(gReservation)
	    mode = RES_MODE;
	  else if(gECN)
	    mode = ECN_MODE;
	  if(_flow_buffer[input][empty_flow]!=NULL){
	    _flow_buffer[input][empty_flow]->Activate(input, empty_flow, mode, _pending_flow[input]);
	  } else {
	    _flow_buffer[input][empty_flow] = 
	      new FlowBuffer(this, input, empty_flow, mode, _pending_flow[input]);
	  }
	  
	  //assign VC from the start
	  _FlowVC(_flow_buffer[input][empty_flow]);
	  

	  //insert into the correct queue
	  _active_set[input].insert(_flow_buffer[input][empty_flow]);
	  _deactive_set[input].erase(_flow_buffer[input][empty_flow]);
	  if(gReservation){
	    _reservation_set[input].insert(_flow_buffer[input][empty_flow]);
	  }
	  if(_pending_flow[input]->flid == WATCH_FLID){
	    _flow_buffer[input][empty_flow]->_watch = true;
	  }
	  _pending_flow[input]=NULL;
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
  //process credit
  for ( int source = 0; source < _nodes; ++source ) {
    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
      Credit * const c = _net[subnet]->ReadCredit( source );
      if ( c ) {
	_buf_states[source][subnet]->ProcessCredit(c);
	c->Free();
      }
    }
  }
  //Eject
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

  //update 
  for ( int source = 0; source < _nodes; ++source ) {
    BufferState * const dest_buf = _buf_states[source][0];
    FlowBuffer* ready_flow_buffer = NULL;
    //each cycle a special packet and a data packet can be insertedf
    if(gReservation || gECN){
#ifdef ENABLE_STATS
      gStatResponseBuffer->AddSample((int)_response_packets[source].size());
#endif
      if( !_response_packets[source].empty() ){
	Flit* f = _response_packets[source].front();
	assert(f);
	assert(f->head);//only heads
	if(((!_cut_through && dest_buf->IsAvailableFor(f->vc)) || 
	    ( _cut_through && dest_buf->IsAvailableFor(f->vc,f->packet_size))) &&
	   dest_buf->HasCreditFor(f->vc)){
	  dest_buf->TakeBuffer(f->vc); 
	  dest_buf->SendingFlit(f);
	  f->ntime = _time;
	  _sent_flits[0][source]->AddSample(1);
	  _net[0]->WriteSpecialFlit(f, source);
	  _response_packets[source].pop_front();
	}
      }
    }
   
    //first check dangling  flowbuffer
    //this check lways occurs until a tail flit is sent, this prevents
    //starvation
    if(_last_sent_norm_buffer[source]!=NULL){
      FlowBuffer* flb = _last_sent_norm_buffer[source];
      if((dest_buf->HasCreditFor(flb->_vc))){
	assert(flb->eligible());
	//removed a credit check
	ready_flow_buffer =  flb;
      }
    }
    if(_last_sent_spec_buffer[source]!=NULL){
      if(_last_sent_spec_buffer[source]->send_spec_ready()){
      
	FlowBuffer* flb = _last_sent_spec_buffer[source];
	if((dest_buf->HasCreditFor(flb->_vc))){
	  assert(flb->eligible());
	  //removed a credit check
	  ready_flow_buffer =  flb;
	}
      } else {
	assert(false);
      }
    }

    //scan through all active buffers
    int flow_bids = 0;
    if(ready_flow_buffer==NULL){
      _flow_buffer_arb[source]->Clear();  
    }
    bool node_ready = false;
    for(set<FlowBuffer*>::iterator i = _active_set[source].begin();
	i!=_active_set[source].end();
	i++){
      FlowBuffer* flb = *i;
      flb->active_update();


      if(flb->_vc == -1 &&
	 (flb->send_norm_ready())){
	_FlowVC( flb);
      }
      if(flb->_vc == -1 &&
	 flb->send_spec_ready()){
	_FlowVC( flb);
	if(!RESERVATION_TAIL_RESERVE){
	  _reservation_set[source].insert(flb);
	}
      }
      if(gECN){
	flb->ecn_update();
      }
      node_ready = 	node_ready || flb->eligible();
      if(ready_flow_buffer==NULL){
	if(flb->eligible() &&
	   (((!_cut_through && dest_buf->IsAvailableFor(flb->_vc))||
	     ( _cut_through && dest_buf->IsAvailableFor(flb->_vc, _packet_size[flb->fl->cl])))|| !flb->_tail_sent)&&
	   dest_buf->HasCreditFor(flb->_vc)){
	  _flow_buffer_arb[source]->AddRequest(flb->_dest, flb->_dest, flb->priority());
	  flow_bids++;
	}
      }
    }
    
    if(flow_bids!=0){
      int id = _flow_buffer_arb[source]->Arbitrate();
      _flow_buffer_arb[source]->UpdateState();
      ready_flow_buffer = _flow_buffer[source][id];
    }

#ifdef ENABLE_STATS
    gStatNodeReady[source]+=int(node_ready);
    //this stat may not be accurate, flows can be in both queues
    gStatActiveFlowBuffers->AddSample(int(_active_set[source].size()));  
#endif

    
    if(gECN){
      for(set<FlowBuffer*>::iterator i = _deactive_set[source].begin();
	  i!=_deactive_set[source].end();
	  i++){
	FlowBuffer* flb = *i;
	flb->ecn_update();
      }
      for(multimap<int, FlowBuffer*>::iterator i = _sleep_set[source].begin();
	  i!=_sleep_set[source].end();
	  i++){
	FlowBuffer* flb = i->second;
	flb->ecn_update();
      }
    }  
    
    if(ready_flow_buffer){
      int vc = ready_flow_buffer->_vc;
      Flit* f = ready_flow_buffer->front();
      assert(f);
      //vc bookkeeping
      if(f->res_type== RES_TYPE_RES){
	if(dest_buf->IsAvailableFor(0) &&
	   dest_buf->HasCreditFor(0)){
	  dest_buf->TakeBuffer(0); 
	  dest_buf->SendingFlit(f);
	  assert(_reservation_set[source].erase(ready_flow_buffer));
	  //create rob at the destiantion
	  if(f->sn==0){
	    _rob[f->dest].insert(pair<int, FlowROB*>(f->flid, new FlowROB(ready_flow_buffer->fl->flow_size)));
	  }
	} else {
	  f = NULL;
	}
      } else {
	//Fast reservation flit transmit
	if(gReservation && 
	   FAST_RESERVATION_TRANSMIT &&
	   !_reservation_set[source].empty()){
	  if(dest_buf->IsAvailableFor(0) &&
	     dest_buf->HasCreditFor(0)){
	    dest_buf->TakeBuffer(0); 

	    _reservation_arb[source]->Clear();
	    for(set<FlowBuffer*>::iterator i=_reservation_set[source].begin();
		i!=_reservation_set[source].end();
		i++){
	      FlowBuffer* flb = *i;
	      assert(flb && flb->active());
	      //this can be false if post_wait is on
	      if(flb->send_spec_ready() ) {
		_reservation_arb[source]->AddRequest(flb->_dest, flb->_dest, 1);
	      }
	    }
	    if(_reservation_arb[source]->NumReqs()>0){
	      int id = _reservation_arb[source]->Arbitrate();
	      
	      _reservation_arb[source]->UpdateState() ;
	      
	      FlowBuffer* flb = _flow_buffer[source][id];
	      Flit* rf =  flb->send();
	      assert(rf->res_type==RES_TYPE_RES);
	      if(rf->sn==0){
		_rob[rf->dest].insert(pair<int, FlowROB*>(rf->flid, new FlowROB(flb->fl->flow_size)));
	      }
	      dest_buf->SendingFlit(rf);
	      rf->ntime = _time;
	      _net[0]->WriteSpecialFlit(rf, source);
	      _sent_flits[0][source]->AddSample(1);
	      assert(_reservation_set[source].erase(flb));
	    }
	  }   
	}
	if(f->head){
	  dest_buf->TakeBuffer(vc); 
	}
	f->vc = vc;
	dest_buf->SendingFlit(f);
      }    
      //VC bookkeeping completed success
      if(f){
	//actual the actual packet to send
	f =  ready_flow_buffer->send();
	if(RESERVATION_QUEUING_DROP ){
	  f->exptime = gExpirationTime;
	} else {
	  f->exptime = _time+gExpirationTime;
	}
	//flit network traffic time
	f->ntime = _time;
	_net[0]->WriteSpecialFlit(f, source);
	_sent_flits[0][source]->AddSample(1);

	//flow continuation book keeping
	if(f->tail){
	  if(f->res_type==RES_TYPE_SPEC){
#ifdef ENABLE_STATS
	    gStatSpecSent[source]++;
#endif
	    _last_sent_spec_buffer[source] =NULL;
	  } else if(f->res_type==RES_TYPE_NORM) {

#ifdef ENABLE_STATS
	    gStatNormSent[source]++;
#endif
	    _last_sent_norm_buffer[source] =NULL;
	  } else {
	    assert(f->res_type==RES_TYPE_RES);
	    _last_sent_spec_buffer[source] =NULL;
	  }
	} else {
	  if(f->res_type==RES_TYPE_SPEC){
#ifdef ENABLE_STATS
	    gStatSpecSent[source]++;
#endif
	    _last_sent_spec_buffer[source] =ready_flow_buffer;
	  } else if(f->res_type==RES_TYPE_NORM) {
	    _last_sent_norm_buffer[source] =ready_flow_buffer;
#ifdef ENABLE_STATS
	    gStatNormSent[source]++;
#endif
	  }
	}

	//check flow done
	int flow_done_status = ready_flow_buffer->done();
	if(flow_done_status !=FLOW_DONE_NOT){
#ifdef ENABLE_STATS
	  if(ready_flow_buffer->fl->cl==0){
	    for(size_t i = 0; i<gStatFlowStats.size()-1; i++){
	      gStatFlowStats[i]+=ready_flow_buffer->_stats[i];
	    }
	    gStatFlowStats[gStatFlowStats.size()-1]++;
	  }
	  gStatFlowSenderLatency->AddSample(_time-ready_flow_buffer->fl->create_time);
	  gStatFastRetransmit->AddSample(ready_flow_buffer->_fast_retransmit);
	  if(!gReservation){
	    gStatFlowLatency->AddSample(GetSimTime()-_flow_buffer[source][f->flbid]->fl->create_time);
	  }
#endif
	  if(flow_done_status==FLOW_DONE_DONE){
	    _flow_buffer[source][f->flbid]->Deactivate();
	    _active_set[source].erase(ready_flow_buffer);
	    _deactive_set[source].insert(ready_flow_buffer);
	  } else {
	    ready_flow_buffer->Reset();
	    _FlowVC(ready_flow_buffer);
	    if(gReservation){
	      _reservation_set[source].insert(ready_flow_buffer);
	    }	  
	  }
	}
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
	c->vc.push_back(f->vc);
	_net[subnet]->WriteCredit(c, dest);
	_RetireFlit(f, dest);
      }
    }
    flits[subnet].clear();
    _net[subnet]->Evaluate( );
    _net[subnet]->WriteOutputs( );
  }

  ++_stat_time;
  ++_time;
    gStatSpecCount->AddSample(TOTAL_SPEC_BUFFER);
  if(_time%10000==0){
    
    gStatMonitorTransient[0]->push_back(retired_s->NumSamples()+retired_n->NumSamples());
    gStatMonitorTransient[1]->push_back(retired_s->Average());
    gStatMonitorTransient[2]->push_back(retired_n->Average());
    gStatMonitorTransient[3]->push_back(((gStatReservationMismatch_POS->Average()*gStatReservationMismatch_POS->NumSamples())-(gStatReservationMismatch_NEG->Average()*gStatReservationMismatch_NEG->NumSamples()))/(gStatReservationMismatch_POS->NumSamples()+gStatReservationMismatch_NEG->NumSamples()));
    gStatMonitorTransient[4]->push_back(((gStatResEarly_POS->Average()*gStatResEarly_POS->NumSamples())-(gStatResEarly_NEG->Average()*gStatResEarly_NEG->NumSamples()))/(gStatResEarly_POS->NumSamples()+gStatResEarly_NEG->NumSamples()));
    gStatMonitorTransient[5]->push_back(float(gStatSpecLatency->NumSamples())/_plat_stats[0]->NumSamples()*100);


    cout<<" Retired "<<retired_s->NumSamples()+retired_n->NumSamples()
	<<" Spec Out "<<TOTAL_SPEC_BUFFER
	<<" spec_lat "<<retired_s->Average()
	<<" norm_lat "<<retired_n->Average()
	<<" ("<<float(retired_s->NumSamples())/retired_n->NumSamples()<<")"<<endl
	<<" mismatch "<<((gStatReservationMismatch_POS->Average()*gStatReservationMismatch_POS->NumSamples())-(gStatReservationMismatch_NEG->Average()*gStatReservationMismatch_NEG->NumSamples()))/(gStatReservationMismatch_POS->NumSamples()+gStatReservationMismatch_NEG->NumSamples())<<"("<<gStatReservationMismatch_POS->NumSamples()+gStatReservationMismatch_NEG->NumSamples()<<")"
	<<" early "<<((gStatResEarly_POS->Average()*gStatResEarly_POS->NumSamples())-(gStatResEarly_NEG->Average()*gStatResEarly_NEG->NumSamples()))/(gStatResEarly_POS->NumSamples()+gStatResEarly_NEG->NumSamples())<<"("<<gStatResEarly_POS->NumSamples()+gStatResEarly_NEG->NumSamples()<<")"<<endl;
    cout<<"\t%spec "<<float(gStatSpecLatency->NumSamples())/_plat_stats[0]->NumSamples()*100<<endl; 
    //gStatResEarly_POS->Clear();
    //gStatResEarly_NEG->Clear();
    //gStatReservationMismatch_POS->Clear();
    //gStatReservationMismatch_NEG->Clear();
    retired_s->Clear();
    retired_n->Clear();
    //gStatSpecLatency->Clear();
    //_plat_stats[0]->Clear();
  }

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
  _stat_time=0;
  _slowest_flit.assign(_classes, -1);

  gStatFlowSizes.clear();

  for ( int c = 0; c < _classes; ++c ) {

    _plat_stats[c]->Clear( );
    _tlat_stats[c]->Clear( );
    _frag_stats[c]->Clear( );
    _spec_frag_stats[c]->Clear( );
#ifdef ENABLE_STATS
    gStatPureNetworkLatency[c]->Clear();
    gStatSpecNetworkLatency[c]->Clear();
#endif
    for ( int i = 0; i < _nodes; ++i ) {
      _sent_flits[c][i]->Clear( );
      
      for ( int j = 0; j < _nodes; ++j ) {
	//	_pair_plat[c][i*_nodes+j]->Clear( );
	//_pair_tlat[c][i*_nodes+j]->Clear( );
      }
    }

    for ( int i = 0; i < _nodes; ++i ) {
      _sent_data_flits[c][i]=0;
      _accepted_data_flits[c][i]=0;
      _accepted_flits[c][i]->Clear( );
    }
  
    _hop_stats[c]->Clear();

  }
  
  for(int i = 0; i<_routers; i++){
    for(unsigned int j = 0; j<    gDropStats[i].size(); j++){
      gDropStats[i][j] = 0;
    }
  }
  for(int i = 0; i<_routers; i++){
    for(unsigned int j = 0; j<    gChanDropStats[i].size(); j++){
      gChanDropStats[i][j] = 0;
    }
  }
#ifdef ENABLE_STATS
  gStatSpecCount->Clear();
  gStatDropLateness->Clear();
  gStatSurviveTTL->Clear();
  gStatNodeReady.clear();
  gStatNodeReady.resize(_nodes,0);

  gStatAckReceived.clear();
  gStatAckReceived.resize(_nodes,0);
  gStatAckEffective.clear();
  gStatAckEffective.resize(_nodes,0);
  gStatAckSent.clear();
  gStatAckSent.resize(_nodes,0);

  gStatNackReceived.clear();
  gStatNackReceived.resize(_nodes,0);
  gStatNackEffective.clear();
  gStatNackEffective.resize(_nodes,0);
  gStatNackSent.clear();
  gStatNackSent.resize(_nodes,0);

  gStatGrantReceived.clear();
  gStatGrantReceived.resize(_nodes,0);

  gStatReservationReceived.clear();
  gStatReservationReceived.resize(_nodes,0);

  for(int i = 0; i< _nodes; i++){
    gStatGrantTimeNow[i]->Clear();
    gStatGrantTimeFuture[i]->Clear();
    gStatReservationTimeNow[i]->Clear();
    gStatReservationTimeFuture[i]->Clear();
  }

  gStatSpecSent.clear();
  gStatSpecSent.resize(_nodes,0);
  gStatSpecReceived.clear();
  gStatSpecReceived.resize(_nodes,0);
  gStatSpecDuplicate.clear();
  gStatSpecDuplicate.resize(_nodes,0);

  gStatNormSent.clear();
  gStatNormSent.resize(_nodes,0);
  gStatNormReceived.clear();
  gStatNormReceived.resize(_nodes,0);
  gStatNormDuplicate.clear();
  gStatNormDuplicate.resize(_nodes,0);

  gStatLostPacket.clear();
  gStatLostPacket.resize(_nodes,0);

  gStatInjectVCDist.clear();
  gStatInjectVCDist.resize(_nodes);
  for(int i= 0; i<_nodes; i++){
    gStatInjectVCDist[i].resize(_num_vcs,0);
  }
  gStatInjectVCBlock.clear();
  gStatInjectVCBlock.resize(_nodes,0);

  gStatFlowMerged.clear();
  gStatFlowMerged.resize(_nodes,0);

  gStatROBRange->Clear();
  
  gStatFlowSenderLatency->Clear();
  gStatFlowLatency->Clear();
  gStatActiveFlowBuffers->Clear();
  gStatReadyFlowBuffers->Clear();
  gStatResponseBuffer->Clear();

  gStatAckLatency->Clear();
  gStatNackLatency->Clear();
  gStatResLatency->Clear();
  gStatGrantLatency->Clear();
  gStatSpecLatency->Clear();
  gStatNormLatency->Clear();

  gStatSourceTrueLatency->Clear();
  gStatSourceLatency->Clear();

  gStatNackByPacket->Clear();
  
  gStatFastRetransmit->Clear();
  gStatNackArrival->Clear();
  gStatFlowStats.clear();
  gStatFlowStats.resize(FLOW_STAT_LIFETIME+1+1,0);
#endif
}

int TrafficManager::_ComputeStats( const vector<Stats *> & stats, double *avg, double *min , double *max) const 
{
  int dmin = -1;
  if(min)
    *min = numeric_limits<double>::max();
  if(max)
    *max = -numeric_limits<double>::max();
  if(avg)
    *avg = 0.0;
  
  for ( int d = 0; d < _nodes; ++d ) {
    double curr = stats[d]->Average( );
    if (min !=NULL  &&  curr < *min ) {
      *min = curr;
      dmin = d;
    }
    if (max!=NULL &&  curr > *max ) {
      *max = curr;
    } 
    if(avg)
      *avg += curr;
  }
  if(avg)
    *avg /= (double)_nodes;

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

	  if(_measure_stats[c] == 0) {
	    continue;
	  }

	  double cur_latency = _plat_stats[c]->Average( );
	  double min, avg;
	  int dmin = _ComputeStats( _accepted_flits[c], &avg, &min );
	  
	  cout << "Class " << c << ":" << endl;
	  cout << "Minimum latency = " << _plat_stats[c]->Min( ) << endl;
	  cout << "Average latency = " << cur_latency << endl;
	  cout << "Maximum latency = " << _plat_stats[c]->Max( ) << endl;
	  cout << "Average fragmentation = " << _frag_stats[c]->Average( ) << endl;
	  cout << "\tAverage spec fragmentation = " << _spec_frag_stats[c]->Average( ) << endl;
	  
	  cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;

	  cout << "Total in-flight flits = " << _total_in_flight_flits[c].size() << " (" << _measured_in_flight_flits[c].size() << " measured)" << endl;

	  //c+1 because of matlab arrays starts at 1
	  if(_stats_out)
	    *_stats_out << "lat(" << c+1 << ") = " << cur_latency << ";" << endl
			<< "lat_hist(" << c+1 << ",:) = " << *_plat_stats[c] << ";" << endl
			<< "frag_hist(" << c+1 << ",:) = " << *_frag_stats[c] << ";" 			<< "spec_frag_hist(" << c+1 << ",:) = " << *_spec_frag_stats[c] << ";" << endl;
	} 
      }
    }
    converged = 1;

  } else if(_sim_mode == batch && !_timed_mode){//batch mode   
    while(total_phases < _batch_count) {
      _packets_sent.assign(_nodes, 0);
      _last_id = -1;
      _last_pid = -1;
      _sim_state = running;
      int start_time = _time;
      int min_packets_sent = 0;
      while(min_packets_sent < _batch_size){
	_Step();
	min_packets_sent = _packets_sent[0];
	for(int i = 1; i < _nodes; ++i) {
	  if(_packets_sent[i] < min_packets_sent)
	    min_packets_sent = _packets_sent[i];
	}
	if(_flow_out) {
	  *_flow_out << "packets_sent(" << _time << ",:) = " << _packets_sent << ";" << endl;
	}
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
      packets_left = packets_left && !_no_drain;

      while( packets_left ) { 
	_Step( ); 

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
      double cur_latency = _plat_stats[0]->Average( );
      double min, avg;
      int dmin = _ComputeStats( _accepted_flits[0], &avg, &min );
      
      cout << "Batch duration = " << _time - start_time << endl;
      cout << "Minimum latency = " << _plat_stats[0]->Min( ) << endl;
      cout << "Average latency = " << cur_latency << endl;
      cout << "Maximum latency = " << _plat_stats[0]->Max( ) << endl;
      cout << "Average fragmentation = " << _frag_stats[0]->Average( ) << endl;
      cout << "\tAverage spec fragmentation = " << _spec_frag_stats[0]->Average( ) << endl;
      cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
      if(_stats_out) {
	*_stats_out << "batch_time(" << total_phases + 1 << ") = " << _time << ";" << endl
		    << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl
		    << "lat_hist(" << total_phases + 1 << ",:) = "
		    << *_plat_stats[0] << ";" << endl
		    << "frag_hist(" << total_phases + 1 << ",:) = "
		    << *_frag_stats[0] << ";" 
		    << "spec_frag_hist(" << total_phases + 1 << ",:) = "
		    << *_spec_frag_stats[0] << ";" << endl;

	//		    << "pair_sent(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _nodes; ++i) {
	  for(int j = 0; j < _nodes; ++j) {
	    //	    *_stats_out << _pair_plat[0][i*_nodes+j]->NumSamples( ) << " ";
	  }
	}
	//	*_stats_out << "];" << endl
	//		    << "pair_lat(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _nodes; ++i) {
	  for(int j = 0; j < _nodes; ++j) {
	    //	    *_stats_out << _pair_plat[0][i*_nodes+j]->Average( ) << " ";
	  }
	}
	//*_stats_out << "];" << endl
	//		    << "pair_tlat(" << total_phases + 1 << ",:) = [ ";
	for(int i = 0; i < _nodes; ++i) {
	  for(int j = 0; j < _nodes; ++j) {
	    //	    *_stats_out << _pair_tlat[0][i*_nodes+j]->Average( ) << " ";
	  }
	}
	//*_stats_out << "];" << endl
	*_stats_out << "sent(" << total_phases + 1 << ",:) = [ ";
	for ( int d = 0; d < _nodes; ++d ) {
	  *_stats_out << _sent_flits[0][d]->NumSamples() << " ";
	}
	*_stats_out << "];" << endl
		    << "accepted(" << total_phases + 1 << ",:) = [ ";
	for ( int d = 0; d < _nodes; ++d ) {
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
      
     
      for ( int iter = 0; iter < ((_sim_state==warming_up)?_warmup_cycles:_sample_period); ++iter )
	_Step( );
      
      if(_stats_out)
	*_stats_out << "%=================================" << endl;

      int lat_exc_class = -1;
      double lat_exc_value = 0.0;
      int lat_chg_exc_class = -1;
      int acc_chg_exc_class = -1;

      for(int c = 0; c < _classes; ++c) {

	if(_measure_stats[c] == 0) {
	  continue;
	}

	double cur_latency = _plat_stats[c]->Average( );
	int dmin;
	double min, avg;
	dmin = _ComputeStats( _accepted_flits[c], &avg, &min );
	double cur_accepted = avg;

	double latency_change = fabs((cur_latency - prev_latency[c]) / cur_latency);
	prev_latency[c] = cur_latency;
	double accepted_change = fabs((cur_accepted - prev_accepted[c]) / cur_accepted);
	prev_accepted[c] = cur_accepted;

	cout << "Class " << c << ":" << endl;
	cout<<"Flits allocated "<<Flit::Allocated()<<endl;
	cout << "Minimum latency = " << _plat_stats[c]->Min( ) << endl;
	cout << "Average latency = " << cur_latency << endl;
	cout << "Maximum latency = " << _plat_stats[c]->Max( ) << endl;
	cout << "Average fragmentation = " << _frag_stats[c]->Average( ) << endl;
	cout << "\tAverage spec fragmentation = " << _spec_frag_stats[c]->Average( ) << endl;
	cout << "Accepted packets = " << min << " at node " << dmin << " (avg = " << avg << ")" << endl;
	cout << "Total in-flight flits = " << _total_in_flight_flits[c].size() << " (" << _measured_in_flight_flits[c].size() << " measured)" << endl;
	//c+1 due to matlab array starting at 1
	if(_stats_out) {
	  *_stats_out << "lat(" << c+1 << ") = " << cur_latency << ";" << endl
		      << "lat_hist(" << c+1 << ",:) = " << *_plat_stats[c] << ";" << endl
		      << "frag_hist(" << c+1 << ",:) = " << *_frag_stats[c] << ";"
		      << "spec_frag_hist(" << c+1 << ",:) = " << *_spec_frag_stats[c] << ";" << endl;
	    //		      << "pair_sent(" << c+1 << ",:) = [ ";
	  for(int i = 0; i < _nodes; ++i) {
	    for(int j = 0; j < _nodes; ++j) {
	      //*_stats_out << _pair_plat[c][i*_nodes+j]->NumSamples( ) << " ";
	    }
	  }
	  //*_stats_out << "];" << endl;
	  //		      << "pair_lat(" << c+1 << ",:) = [ ";
	  for(int i = 0; i < _nodes; ++i) {
	    for(int j = 0; j < _nodes; ++j) {
	      //	      *_stats_out << _pair_plat[c][i*_nodes+j]->Average( ) << " ";
	    }
	  }
	  //	  *_stats_out << "];" << endl
	  //	      << "pair_lat(" << c+1 << ",:) = [ ";
	  for(int i = 0; i < _nodes; ++i) {
	    for(int j = 0; j < _nodes; ++j) {
	      //	      *_stats_out << _pair_tlat[c][i*_nodes+j]->Average( ) << " ";
	    }
	  }
	  //	  *_stats_out << "];" << endl;
		     *_stats_out  << "sent(" << c+1 << ",:) = [ ";
	  for ( int d = 0; d < _nodes; ++d ) {
	    *_stats_out << _sent_flits[c][d]->NumSamples( ) << " ";
	  }
	  *_stats_out << "];" << endl
		      << "accepted(" << c+1 << ",:) = [ ";
	  for ( int d = 0; d < _nodes; ++d ) {
	    *_stats_out << _accepted_flits[c][d]->Average( ) << " ";
	  }
	  *_stats_out << "];" << endl;
	  *_stats_out << "inflight(" << c+1 << ") = " << _total_in_flight_flits[c].size() << ";" << endl;
	  
	  *_stats_out<< "sent_data(" << c+1 << ",:) = [ ";
	  for ( int d = 0; d < _nodes; ++d ) {
	    *_stats_out << _sent_data_flits[c][d] << " ";
	  }
	  *_stats_out << "];" << endl
		      << "accepted_data(" << c+1 << ",:) = [ ";
	  for ( int d = 0; d < _nodes; ++d ) {
	    *_stats_out << _accepted_data_flits[c][d] << " ";
	  }
	  *_stats_out << "];" << endl;
	 
	  vector<FlitChannel *>  temp = _net[0]->GetChannels();
	  for(unsigned int i = 0; i<temp.size(); i++){
	    *_stats_out <<"chan_idle("<<i+1<<",:)=["
			<<temp[i]->GetActivity()
			<<"];"<<endl;
	  }
	  *_stats_out <<"run_time = "<<_stat_time<<";"<<endl;
	}
	
	double latency = (double)_plat_stats[c]->Sum();
	double count = (double)_plat_stats[c]->NumSamples();
	  
	map<int, Flit *>::const_iterator iter;
	for(iter = _total_in_flight_flits[c].begin(); 
	    iter != _total_in_flight_flits[c].end(); 
	    iter++) {
	  latency += (double)(_time - iter->second->time);
	  count++;
	}
	
	if((_sim_state != warming_up || !_forced_warmup) &&
	   (lat_exc_class < 0) &&
	   (_latency_thres[c] >= 0.0) &&
	   ((latency / count) > _latency_thres[c])) {
	  lat_exc_class = c;
	  lat_exc_value = (latency / count);
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

	cout << "Average latency for class " << lat_exc_class <<" is "<<lat_exc_value<< " exceeded " << _latency_thres[lat_exc_class] << " cycles. Aborting simulation." << endl;
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
    packets_left = packets_left && !_no_drain;

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
    if(_drain_measured_only!=1 && !_no_drain){
      //wait until all the credits are drained as well
      while(Credit::OutStanding()!=0){
	++empty_steps;
	_Step();
      }
    }
    if(TRANSIENT_ENABLE){
      _rob.clear();
      _rob.resize(_nodes);
      _response_packets.clear();
      _response_packets.resize(_nodes);
      _reservation_schedule.clear();
      _reservation_schedule.resize(_nodes, 0);
      _pending_flow.clear();
      _pending_flow.resize(_nodes,NULL);
      _flow_buffer.clear();
      _flow_buffer.resize(_nodes);
      for(int i = 0; i<_nodes; i++){
	_flow_buffer[i].resize(_max_flow_buffers,NULL);
      
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
      //      return false;
    }
    //for the love of god don't ever say "Time taken" anywhere else
    //the power script depend on it
    cout << "Time taken is " << _time << " cycles" <<endl; 
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
      
      if(_sim_mode == batch)
	_overall_batch_time->AddSample(_batch_time->Sum( ));
    }
  }
  
  DisplayStats();
  if(_print_vc_stats) {
    if(_print_csv_results) {
      cout << "vc_stats:";
    }
    VC::DisplayStats(_print_csv_results);
  }
  return true;
}

void TrafficManager::DisplayStats( ostream & os ) {
  for ( int c = 0; c < _classes; ++c ) {

    if(_measure_stats[c] == 0) {
      continue;
    }

    if(_print_csv_results) {
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

    os << "====== Traffic class " << c << " ======" << endl;
    
    os << "Overall minimum latency = " << _overall_min_plat[c]->Average( )
       << " (" << _overall_min_plat[c]->NumSamples( ) << " samples)" << endl;
    os << "Overall average latency = " << _overall_avg_plat[c]->Average( )
       << " (" << _overall_avg_plat[c]->NumSamples( ) << " samples)" << endl;
#ifdef ENABLE_STATS
    os << "Overall average network latency = " <<gStatPureNetworkLatency[c]->Average( )
       << " (" << _overall_avg_plat[c]->NumSamples( ) << " samples)" << endl;
#endif

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
    double max;
    _ComputeStats( _accepted_flits[c], NULL, NULL, &max );
    os << "Overall max accepted rate = " <<max << endl;
    
    float mean=0.0;
    float nim =numeric_limits<float>::max();
    float xam =-numeric_limits<float>::max();
    int zeros=0;
    for ( int d = 0; d < _nodes; ++d ) {
      bool zero = false;
      zero= (_accepted_data_flits[c][d]==0);
      mean+=float(_accepted_data_flits[c][d])/float(_stat_time);
      if(zero)
	zeros++;
      if(!zero && float(_accepted_data_flits[c][d])/float(_stat_time)<nim)
	nim =float(_accepted_data_flits[c][d])/float(_stat_time);
      if(float(_accepted_data_flits[c][d])/float(_stat_time)>xam)
	xam =float(_accepted_data_flits[c][d])/float(_stat_time);
    }
    cout<<"Overall average data accepted rate = "<<mean/_nodes<<endl;
    cout<<"\tOverall min data accepted rate = "<<nim<<" ("<<zeros<<" zeros)"<<endl;
    cout<<"\tOverall max data accepted rate = "<<xam<<endl;

    os << "Average hops = " << _hop_stats[c]->Average( )
       << " (" << _hop_stats[c]->NumSamples( ) << " samples)" << endl;

    os << "Slowest flit = " << _slowest_flit[c] << endl;
    
#ifdef ENABLE_STATS
    double ecn_sum = 0.0;
    long ecn_num = 0;
    for(int i = 0; i<_nodes; i++){
      if( gStatECN[i]->NumSamples()>0){
	ecn_sum += gStatECN[i]->Sum();
	ecn_num +=gStatECN[i]->NumSamples();
      }
    }
    os<<"ECN ratio "<<ecn_sum/ecn_num<<endl;
    os<<"Flows created "<<_cur_flid<<endl;
    _DisplayTedsShit();    
#endif
  }
  
  if(_sim_mode == batch)
    os << "Overall batch duration = " << _overall_batch_time->Average( )
       << " (" << _overall_batch_time->NumSamples( ) << " samples)" << endl;
  
}

void TrafficManager::_DisplayTedsShit(){
  if(_stats_out){
    for(int c = 0; c < _classes; ++c) {
      *_stats_out<< "sent_data(" << c+1 << ",:) = [ ";
      for ( int d = 0; d < _nodes; ++d ) {
	*_stats_out << _sent_data_flits[c][d] << " ";
      }
      *_stats_out << "];" << endl
		  << "accepted_data(" << c+1 << ",:) = [ ";
      for ( int d = 0; d < _nodes; ++d ) {
	*_stats_out << _accepted_data_flits[c][d] << " ";
      }
      *_stats_out << "];" << endl;
      
      vector<FlitChannel *>  temp = _net[0]->GetChannels();
      for(unsigned int i = 0; i<temp.size(); i++){
	*_stats_out <<"chan_idle("<<i+1<<",:)=["
		    <<temp[i]->GetActivity()
		    <<"];"<<endl;
      }
    }
    
    *_stats_out <<"run_time = "<<_stat_time<<";"<<endl;
    
    *_stats_out<< "late_drop =["
	       << *gStatDropLateness<<"];\n";
    *_stats_out<< "ttl_remain =["
	       << *gStatSurviveTTL<<"];\n";
    *_stats_out<< "ack_received = ["
	       <<gStatAckReceived<<"];\n";
    *_stats_out<< "ack_eff = ["
	       <<gStatAckEffective<<"];\n";
    *_stats_out<< "ack_sent = ["
	       <<gStatAckSent<<"];\n";

    *_stats_out<< "nack_received =[ "
	       <<gStatNackReceived<<"];\n";
    *_stats_out<< "nack_eff = ["
	       <<gStatNackEffective<<"];\n";
    *_stats_out<< "nack_sent = ["
	       <<gStatNackSent<<"];\n";

    *_stats_out<< "grant_received = ["
	       <<gStatGrantReceived<<"];\n";

    *_stats_out<< "res_received = ["
	       <<gStatReservationReceived<<"];\n";
    *_stats_out<< "spec_sent = ["
	       <<gStatSpecSent<<"];\n";
    *_stats_out<< "spec_received = ["
	       <<gStatSpecReceived<<"];\n";
    *_stats_out<< "spec_dup = ["
	       <<gStatSpecDuplicate<<"];\n";

    *_stats_out<< "norm_sent = ["
	       <<gStatNormSent<<"];\n";
    *_stats_out<< "norm_received = ["
	       <<gStatNormReceived<<"];\n";
    *_stats_out<< "norm_dup = ["
	       <<gStatNormDuplicate<<"];\n";

    *_stats_out<< "lost_flits = ["
	       <<gStatLostPacket<<"];\n";
    *_stats_out<<"reservation_early_neg =["
	       <<*gStatResEarly_NEG <<"];\n";
    *_stats_out<<"reservation_early_pos =["
	       <<*gStatResEarly_POS <<"];\n";
    *_stats_out<<"reservation_mismatch_neg =["
	       <<*gStatReservationMismatch_NEG <<"];\n";
    *_stats_out<<"reservation_mismatch_pos =["
	       <<*gStatReservationMismatch_POS <<"];\n";

    for(int i = 0; i<6; i++){
      *_stats_out<<"monitor_transient("<<i+1<<",:)=[";
	for(size_t j = 0; j<gStatMonitorTransient[i]->size(); j++){
	  *_stats_out<<(*gStatMonitorTransient[i])[j]<<"\t";
	}
      *_stats_out<<"];\n";
    }

    for(int i = 0; i<_nodes; i++){
      *_stats_out<<"inject_vc_dist("<<i+1<<",:)=["
		 <<gStatInjectVCDist[i] <<"];\n";
      *_stats_out<<"grant_now ("<<i+1<<",:)=["
		 <<*gStatGrantTimeNow[i] <<"];\n";
      *_stats_out<<"grant_future ("<<i+1<<",:)=["
		 <<*gStatGrantTimeFuture[i] <<"];\n";
      *_stats_out<<"res_now ("<<i+1<<",:)=["
		 <<*gStatReservationTimeNow[i] <<"];\n";
      *_stats_out<<"res_future ("<<i+1<<",:)=["
		 <<*gStatReservationTimeFuture[i] <<"];\n";
    }
    *_stats_out<<"inject_vc_block = ["
	       <<gStatInjectVCBlock<<"];\n";
    
    *_stats_out<<"flowbuffer_merge = ["
	       <<gStatFlowMerged<<"];\n";

    *_stats_out<< "rob_range = ["
	       <<*gStatROBRange<<"];\n";

    *_stats_out<< "flow_sender_hist = ["
	       <<*gStatFlowSenderLatency <<"];\n";
    *_stats_out<< "flow_hist = ["
	       <<*gStatFlowLatency <<"];\n";
    *_stats_out<< "active_flows = ["
	       <<*gStatActiveFlowBuffers <<"];\n";
    *_stats_out<< "ready_flows = ["
	       <<*gStatReadyFlowBuffers <<"];\n";
    *_stats_out<< "response_range = ["
	       <<*gStatResponseBuffer <<"];\n";
    
    for(int c = 0; c < _classes; ++c) {
      *_stats_out<< "net_hist(" << c+1 << ",:) = "
		 <<*gStatPureNetworkLatency[c] <<";\n";
      *_stats_out<< "spec_net_hist(" << c+1 << ",:) = "
		 <<*gStatSpecNetworkLatency[c] <<";\n";
    }
    *_stats_out<< "ack_hist = "
	       <<*gStatAckLatency <<";\n";
    *_stats_out<< "nack_hist = "
	       <<*gStatNackLatency <<";\n";
    *_stats_out<< "res_hist = "
	       <<*gStatResLatency <<";\n";
    *_stats_out<< "grant_hist = "
	       <<*gStatGrantLatency <<";\n";
    *_stats_out<< "spec_hist = "
	       <<*gStatSpecLatency <<";\n";
    *_stats_out<< "norm_hist = "
	       <<*gStatNormLatency <<";\n";

    *_stats_out<< "source_queue_hist = "
	       <<*gStatSourceLatency <<";\n";
    *_stats_out<< "source_truequeue_hist = "
	       <<*gStatSourceTrueLatency <<";\n";   

    *_stats_out<< "nack_by_sn = "
	       <<*gStatNackByPacket<<";\n";

    *_stats_out<< "fast_retransmit = "
	       <<*gStatFastRetransmit<<";\n";
    *_stats_out<< "nack_arrival = "
	       <<*gStatNackArrival<<";\n";

    *_stats_out<< "flow_in_spec =" 
	       <<gStatFlowStats[FLOW_STAT_SPEC]<<";\n";
    *_stats_out<< "flow_in_nack =" 
	       <<gStatFlowStats[FLOW_STAT_NACK]<<";\n";
    *_stats_out<< "flow_in_wait =" 
	       <<gStatFlowStats[FLOW_STAT_WAIT]<<";\n";
    *_stats_out<< "flow_in_norm =" 
	       <<gStatFlowStats[FLOW_STAT_NORM ]<<";\n";
    *_stats_out<< "flow_in_norm_ready =" 
	       <<gStatFlowStats[FLOW_STAT_NORM_READY ]<<";\n";
    *_stats_out<< "flow_in_spec_ready =" 
	       <<gStatFlowStats[FLOW_STAT_SPEC_READY]<<";\n";
    *_stats_out<< "flow_in_not_ready =" 
	       <<gStatFlowStats[FLOW_STAT_NOT_READY]<<";\n";
    *_stats_out<< "flow_in_final_not_ready =" 
	       <<gStatFlowStats[FLOW_STAT_FINAL_NOT_READY]<<";\n";
    *_stats_out<< "flow_in_lifetime =" 
	       <<gStatFlowStats[FLOW_STAT_LIFETIME]<<";\n";
    *_stats_out<< "flow_count =" 
	       <<gStatFlowStats[FLOW_STAT_LIFETIME+1]<<";\n";
    *_stats_out<<"node_ready=["
	       <<gStatNodeReady<<"];\n"<<endl;
    *_stats_out<<"spec_count="<<gStatSpecCount->Average()<<";\n";
    for(int i = 0; i<_nodes; i++){
      *_stats_out<< "ird_stat(:,"<<i+1<<")=" 
		 <<*gStatIRD[i]<<";\n";
      *_stats_out<< "ecn_stat (:,"<<i+1<<")=" 
		 <<*gStatECN[i]<<";\n";
    }
    if(TRANSIENT_ENABLE && (transient_data_file==""||transient_finalize)){
      for(int i = 0; i<transient_record_duration; i++){
	*_stats_out<<"transient_net_stat("<<i+1<<",:)= [";
	*_stats_out<<transient_stat[0][0][i]<<" "<<transient_count[0][i]<<" "<<float(transient_stat[0][0][i])/transient_count[0][i]<<" ";
	*_stats_out<<transient_stat[1][0][i]<<" "<<transient_count[1][i]<<" "<<float(transient_stat[1][0][i])/transient_count[1][i]<<" ";
	*_stats_out<<" ];"<<endl;
      }
      for(int i = 0; i<transient_record_duration; i++){
	*_stats_out<<"transient_stat("<<i+1<<",:)= [";
	*_stats_out<<transient_stat[0][1][i]<<" "<<transient_count[0][i]<<" "<<float(transient_stat[0][1][i])/transient_count[0][i]<<" ";
	*_stats_out<<transient_stat[0][2][i]<<" ";
	*_stats_out<<transient_stat[1][1][i]<<" "<<transient_count[1][i]<<" "<<float(transient_stat[1][1][i])/transient_count[1][i]<<" ";
	*_stats_out<<transient_stat[1][2][i]<<" ";
	*_stats_out<<" ];"<<endl;
      }
      if(gECN){
	for(int i = 0; i<transient_record_duration; i++){
	  *_stats_out<<"transient_ecn("<<i+1<<",:)= [";
	  *_stats_out<<transient_ecn[0][i]<<" ";
	  *_stats_out<<transient_ecn[1][i]<<" ";
	  *_stats_out<<" ];"<<endl;
	}
	for(int i = 0; i<transient_record_duration; i++){
	  *_stats_out<<"transient_ird("<<i+1<<",:)= [";
	  *_stats_out<<transient_ird[0][i]<<" ";
	  *_stats_out<<transient_ird[1][i]<<" ";
	  *_stats_out<<" ];"<<endl;
	}
      }
    } else if(TRANSIENT_ENABLE && transient_data_file!=""){
      ofstream tfile;
      tfile.open(transient_data_file.c_str());
      
      for(int i = 0; i<transient_record_duration; i++){
	tfile<<transient_stat[0][0][i]<<" "<<transient_count[0][i]<<" ";
	tfile<<transient_stat[1][0][i]<<" "<<transient_count[1][i]<<" ";
      }
      for(int i = 0; i<transient_record_duration; i++){
	tfile<<transient_stat[0][1][i]<<" "<<transient_count[0][i]<<" ";
	tfile<<transient_stat[0][2][i]<<" ";
	tfile<<transient_stat[1][1][i]<<" "<<transient_count[1][i]<<" ";
	tfile<<transient_stat[1][2][i]<<" ";
      }
      if(gECN){
	for(int i = 0; i<transient_record_duration; i++){
	  tfile<<transient_ecn[0][i]<<" ";
	  tfile<<transient_ecn[1][i]<<" ";
	}
	for(int i = 0; i<transient_record_duration; i++){
	  tfile<<transient_ird[0][i]<<" ";
	  tfile<<transient_ird[1][i]<<" ";
	}
      }
      cout<<"test ";
      cout<<transient_stat[0][0][0]<<" "<<transient_count[0][0]<<" ";
      cout<<transient_stat[1][0][0]<<" "<<transient_count[1][0]<<" ";
      cout<<transient_stat[0][1][0]<<" "<<transient_count[0][0]<<" ";
      cout<<transient_stat[0][2][0]<<" ";
      cout<<transient_stat[1][1][0]<<" "<<transient_count[1][0]<<" ";
      cout<<transient_stat[1][2][0]<<"\n";
      tfile.close();
    }
    vector<FlitChannel *> ccc = _net[0]->GetChannels();
    *_stats_out<<"channel_util=[";
    for(size_t i = 0; i<ccc.size(); i++){
      *_stats_out<<ccc[i]->GetUtilization()<<" ";
    }
    *_stats_out<<"];\n";

    vector<Router *> rrr = _net[0]->GetRouters();
    int max_outputs = 0;
    for(size_t i = 0; i<rrr.size(); i++){
      if(rrr[i]->NumOutputs()>max_outputs){
	max_outputs=rrr[i]->NumOutputs();
      }
    }
    *_stats_out <<"router_hold=[";
    for(int i = 0; i<_routers; i++){
      *_stats_out <<rrr[i]->_holds<<" ";
    }
    *_stats_out <<"];"<<endl;
    *_stats_out <<"router_hold_cancel=[";
    for(int i = 0; i<_routers; i++){
      *_stats_out <<rrr[i]->_hold_cancels<<" ";
    }
    *_stats_out <<"];"<<endl;

    for(int i = 0; i<_routers; i++){
      *_stats_out <<"router_"<<i<<"_activity=["
		  <<rrr[i]->_vc_activity;
      for(int ii=0; ii<max_outputs-rrr[i]->NumOutputs(); ii++)	 *_stats_out<<"0 ";
      *_stats_out <<"];"<<endl;
    }
    for(int i = 0; i<_routers; i++){
      *_stats_out <<"drop_router("<<i+1<<",:)=["
		  <<gDropStats[i] ;
      for(int ii=0; ii<max_outputs-rrr[i]->NumOutputs(); ii++)	 *_stats_out<<"0 ";
      *_stats_out <<"];"<<endl;
    }
    for(int i = 0; i<_routers; i++){
      *_stats_out <<"drop_channel("<<i+1<<",:)=["
		  <<gChanDropStats[i] ;
      for(int ii=0; ii<max_outputs-rrr[i]->NumOutputs(); ii++)	 *_stats_out<<"0 ";
      *_stats_out <<"];"<<endl;
    }
    /*
    for(size_t i = 0; i<rrr.size(); i++){
      *_stats_out<<"vc_congestion(:,"<<i+1<<")=[";
      *_stats_out<<rrr[i]->_vc_congested;
      for(int ii=0; ii<(max_outputs-rrr[i]->NumOutputs())*_num_vcs; ii++)	 *_stats_out<<"0 ";
      *_stats_out<<"];\n";
    }
    for(size_t i = 0; i<rrr.size(); i++){
      *_stats_out<<"vc_congestion_level(:,"<<i+1<<")=[";
      for(size_t j = 0; j<rrr[i]->_vc_congested.size(); j++){
	if(rrr[i]->_vc_congested[j]!=0)
	  *_stats_out<<double(rrr[i]->_vc_congested_sum[j])/rrr[i]->_vc_congested[j]<<" "; 
	else 
	  *_stats_out<<"0 ";
	for(int ii=0; ii<(max_outputs-rrr[i]->NumOutputs())*_num_vcs; ii++)	 *_stats_out<<"0 ";
      }
      *_stats_out<<"];\n";
    }
    */
    for(size_t i = 0; i<rrr.size(); i++){
      *_stats_out<<"ecn_on(:,"<<i+1<<")=[";
      *_stats_out<<rrr[i]->_ECN_activated;
      for(int ii=0; ii<(max_outputs-rrr[i]->NumOutputs())*_num_vcs; ii++)	 *_stats_out<<"0 ";
      *_stats_out<<"];\n";
    }
    /*    for(size_t i = 0; i<rrr.size(); i++){
      *_stats_out<<"congestness(:,"<<i+1<<")=[";
      *_stats_out<<rrr[i]->_port_congestness;
      for(int ii=0; ii<(max_outputs-rrr[i]->NumOutputs())*_num_vcs; ii++)	 *_stats_out<<"0 ";
      *_stats_out<<"];\n";
      }*/
    for(size_t i = 0; i<rrr.size(); i++){
      *_stats_out<<"input_request(:,"<<i+1<<")=[";
      *_stats_out<<rrr[i]->_input_request;
      for(int ii=0; ii<max_outputs-rrr[i]->NumOutputs(); ii++)	 *_stats_out<<"0 ";
      *_stats_out<<"];\n";
    }
    for(size_t i = 0; i<rrr.size(); i++){
      *_stats_out<<"input_grant(:,"<<i+1<<")=[";
      *_stats_out<<rrr[i]->_input_grant;
      for(int ii=0; ii<max_outputs-rrr[i]->NumOutputs(); ii++)	 *_stats_out<<"0 ";
      *_stats_out<<"];\n";
    }
    *_stats_out<<"wait_time =[";
    for(int i = 0; i<_nodes; i++){
      int sum = 0; 
      for(int j = 0; j<_max_flow_buffers; j++){
	if(_flow_buffer[i][j] != NULL){
	  sum += _flow_buffer[i][j]->_total_wait;
	}
      }
      *_stats_out<<" "<<sum;
    }
    *_stats_out<<"];\n";
    int j=0;
    for(map<int, int>::iterator i = gStatFlowSizes.begin();
	i!=gStatFlowSizes.end(); 
	i++,j++){
      *_stats_out<<"flow_size("<<j+1<<",:)=["<<i->first<<" "<<i->second<<"];\n";
    }
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

//read the watchlist
void TrafficManager::_LoadTransient(const string & filename){
  ifstream tfile;
  tfile.open(filename.c_str());
  
  string line;
  if(tfile.is_open()) {
    while(!tfile.eof()) {
      for(int i = 0; i<transient_record_duration; i++){
	tfile>>transient_stat[0][0][i];
	tfile>>transient_count[0][i];
	tfile>>transient_stat[1][0][i];
	tfile>>transient_count[1][i];
      }
      for(int i = 0; i<transient_record_duration; i++){
	tfile>>transient_stat[0][1][i];
	tfile>>transient_count[0][i];
	tfile>>transient_stat[0][2][i];
	tfile>>transient_stat[1][1][i];
	tfile>>transient_count[1][i];
	tfile>>transient_stat[1][2][i];
      }
      if(gECN){
	for(int i = 0; i<transient_record_duration; i++){
	  tfile>>transient_ecn[0][i];
	  tfile>>transient_ecn[1][i];
	}
	for(int i = 0; i<transient_record_duration; i++){
	  tfile>>transient_ird[0][i];
	  tfile>>transient_ird[1][i];
	}
      }
    }
    cout<<"test ";
    cout<<transient_stat[0][0][0]<<" "<<transient_count[0][0]<<" ";
    cout<<transient_stat[1][0][0]<<" "<<transient_count[1][0]<<" ";
    cout<<transient_stat[0][1][0]<<" "<<transient_count[0][0]<<" ";
    cout<<transient_stat[0][2][0]<<" ";
    cout<<transient_stat[1][1][0]<<" "<<transient_count[1][0]<<" ";
    cout<<transient_stat[1][2][0]<<"\n";
    tfile.close();
  } else {
    cout<<"Unable to open transient data file, continuing with simulation\n";
  }
}
