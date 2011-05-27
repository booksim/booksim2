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
#include <fstream>
#include <limits>

#include "booksim.hpp"
#include "booksim_config.hpp"
#include "trafficmanager.hpp"
#include "steadystatetrafficmanager.hpp"
#include "batchtrafficmanager.hpp"
#include "workloadtrafficmanager.hpp"
#include "random_utils.hpp" 
#include "vc.hpp"

TrafficManager * TrafficManager::NewTrafficManager(Configuration const & config,
						   vector<Network *> const & net)
{
  TrafficManager * result = NULL;
  string sim_type = config.GetStr("sim_type");
  if((sim_type == "latency") || (sim_type == "throughput")) {
    result = new SteadyStateTrafficManager(config, net);
  } else if(sim_type == "batch") {
    result = new BatchTrafficManager(config, net);
  } else if(sim_type == "workload") {
    result = new WorkloadTrafficManager(config, net);
  } else {
    cerr << "Unknown simulation type: " << sim_type << endl;
  } 
  return result;
}

TrafficManager::TrafficManager( const Configuration &config, const vector<Network *> & net )
  : Module( 0, "traffic_manager" ), _net(net), _empty_network(false), _deadlock_timer(0), _reset_time(0), _drain_time(-1), _cur_id(0), _cur_pid(0), _cur_tid(0), _time(0)
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

  _subnet = config.GetIntArray("subnet"); 
  if(_subnet.empty()) {
    _subnet.push_back(config.GetInt("subnet"));
  }
  _subnet.resize(_classes, _subnet.back());

  _class_priority = config.GetIntArray("class_priority"); 
  if(_class_priority.empty()) {
    _class_priority.push_back(config.GetInt("class_priority"));
  }
  _class_priority.resize(_classes, _class_priority.back());

  _last_class.resize(_nodes, -1);

  // ============ Injection VC states  ============ 

  _buf_states.resize(_nodes);
  _last_vc.resize(_nodes);

  for ( int source = 0; source < _nodes; ++source ) {
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

  _partial_packets.resize(_classes);
  _sent_packets.resize(_classes);
  _requests_outstanding.resize(_classes);

  for ( int c = 0; c < _classes; ++c ) {
    _partial_packets[c].resize(_nodes);
    _sent_packets[c].resize(_nodes);
    _requests_outstanding[c].resize(_nodes);
  }

  _total_in_flight_flits.resize(_classes);
  _measured_in_flight_flits.resize(_classes);
  _retired_packets.resize(_classes);

  // ============ Statistics ============ 

  _plat_stats.resize(_classes);
  _overall_min_plat.resize(_classes, 0.0);
  _overall_avg_plat.resize(_classes, 0.0);
  _overall_max_plat.resize(_classes, 0.0);

  _frag_stats.resize(_classes);
  _overall_min_frag.resize(_classes, 0.0);
  _overall_avg_frag.resize(_classes, 0.0);
  _overall_max_frag.resize(_classes, 0.0);

  _pair_plat.resize(_classes);
  
  _hop_stats.resize(_classes);
  _overall_hop_stats.resize(_classes, 0.0);
  
  _offered_flits.resize(_classes);
  _overall_min_offered.resize(_classes, 0.0);
  _overall_avg_offered.resize(_classes, 0.0);
  _overall_max_offered.resize(_classes, 0.0);

  _sent_flits.resize(_classes);
  _overall_min_sent.resize(_classes, 0.0);
  _overall_avg_sent.resize(_classes, 0.0);
  _overall_max_sent.resize(_classes, 0.0);

  _accepted_flits.resize(_classes);
  _overall_min_accepted.resize(_classes, 0.0);
  _overall_avg_accepted.resize(_classes, 0.0);
  _overall_max_accepted.resize(_classes, 0.0);

#ifdef TRACK_STALLS
  _overall_buffer_busy_stalls.resize(_classes, 0);
  _overall_buffer_conflict_stalls.resize(_classes, 0);
  _overall_buffer_full_stalls.resize(_classes, 0);
  _overall_crossbar_conflict_stalls.resize(_classes, 0);
#endif

  for ( int c = 0; c < _classes; ++c ) {
    ostringstream tmp_name;

    tmp_name << "plat_stat_" << c;
    _plat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _plat_stats[c];
    tmp_name.str("");

    tmp_name << "frag_stat_" << c;
    _frag_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 100 );
    _stats[tmp_name.str()] = _frag_stats[c];
    tmp_name.str("");

    tmp_name << "hop_stat_" << c;
    _hop_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 20 );
    _stats[tmp_name.str()] = _hop_stats[c];
    tmp_name.str("");

    _pair_plat[c].resize(_nodes*_nodes);

    _offered_flits[c].resize(_nodes, 0);
    _sent_flits[c].resize(_nodes, 0);
    _accepted_flits[c].resize(_nodes, 0);
    
    for ( int i = 0; i < _nodes; ++i ) {
      for ( int j = 0; j < _nodes; ++j ) {
	tmp_name << "pair_plat_stat_" << c << "_" << i << "_" << j;
	_pair_plat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
	_stats[tmp_name.str()] = _pair_plat[c][i*_nodes+j];
	tmp_name.str("");
      }
    }
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

  _measure_stats = config.GetIntArray( "measure_stats" );
  if(_measure_stats.empty()) {
    _measure_stats.push_back(config.GetInt("measure_stats"));
  }
  _measure_stats.resize(_classes, _measure_stats.back());

  _include_queuing = config.GetInt( "include_queuing" );

  _print_csv_results = config.GetInt( "print_csv_results" );
  _deadlock_warn_timeout = config.GetInt( "deadlock_warn_timeout" );
  _drain_measured_only = config.GetInt( "drain_measured_only" );

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
  
#ifdef TRACK_FLOWS
  string active_packets_out_file = config.GetStr( "active_packets_out" );
  if(active_packets_out_file == "") {
    _active_packets_out = NULL;
  } else {
    _active_packets_out = new ofstream(active_packets_out_file.c_str());
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
#endif

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
    delete _frag_stats[c];
    delete _hop_stats[c];

    for ( int i = 0; i < _nodes; ++i ) {
      for ( int j = 0; j < _nodes; ++j ) {
	delete _pair_plat[c][i*_nodes+j];
      }
    }
  }
  
  if(gWatchOut && (gWatchOut != &cout)) delete gWatchOut;
  if(_stats_out && (_stats_out != &cout)) delete _stats_out;

#ifdef TRACK_FLOWS
  if(_active_packets_out) delete _active_packets_out;
  if(_received_flits_out) delete _received_flits_out;
  if(_sent_flits_out) delete _sent_flits_out;
  if(_stored_flits_out) delete _stored_flits_out;
#endif

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
		 << "Retiring packet " << head->pid 
		 << " (lat = " << f->atime - head->time
		 << ", frag = " << (f->atime - head->atime) - (f->id - head->id)
		 << ", src = " << head->src 
		 << ", dest = " << head->dest
		 << ")." << endl;
    }

    _RetirePacket(head, f, dest);
    
    // Only record statistics once per packet (at tail)
    // and based on the simulation state
    if ( ( _sim_state == warming_up ) || f->record ) {
      
      _hop_stats[f->cl]->AddSample( f->hops );
      
      if((_slowest_flit[f->cl] < 0) ||
	 (_plat_stats[f->cl]->Max() < (f->atime - f->time))) {
	_slowest_flit[f->cl] = f->id;
      }
      _plat_stats[f->cl]->AddSample( f->atime - f->time);
      _frag_stats[f->cl]->AddSample( (f->atime - head->atime) - (f->id - head->id) );
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

void TrafficManager::_RetirePacket(Flit * head, Flit * tail, int dest)
{
  if ( tail->watch ) { 
    *gWatchOut << GetSimTime() << " | "
	       << "node" << dest << " | "
	       << "Completing transation " << tail->tid
	       << " (lat = " << tail->atime - head->ttime
	       << ", src = " << head->src 
	       << ", dest = " << head->dest
	       << ")." << endl;
  }
  _requests_outstanding[tail->cl][tail->src]--;
}

void TrafficManager::_GeneratePacket( int source, int dest, int size, 
				      int cl, int time, int tid, int ttime )
{
  assert(size > 0);
  assert((source >= 0) && (source < _nodes));
  assert((dest >= 0) && (dest < _nodes));

  if((_sim_state == warming_up) || (_sim_state == running)) {
    _offered_flits[cl][source] += size;
  }

  bool begin_trans = false;

  if(tid < 0) {
    tid = _cur_tid++;
    assert(_cur_tid);
    begin_trans = true;
  }
  if(ttime < 0) {
    ttime = time;
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
      f->pri = numeric_limits<int>::max() - _sent_packets[cl][source];
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

    _partial_packets[cl][source].push_back(f);
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

  for ( int subnet = 0; subnet < _subnets; ++subnet ) {
    for ( int source = 0; source < _nodes; ++source ) {
      Credit * const c = _net[subnet]->ReadCredit( source );
      if ( c ) {
	_buf_states[source][subnet]->ProcessCredit(c);
	c->Free();
      }
    }
    _net[subnet]->ReadInputs( );
  }

  vector<map<int, Flit *> > flits(_subnets);

  for ( int dest = 0; dest < _nodes; ++dest ) {
    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
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
	if((_sim_state == warming_up) || (_sim_state == running)) {
	  ++_accepted_flits[f->cl][dest];
	}
      }
    }
  }
  
  if ( !_empty_network ) {
    _Inject();
  }

  for(int source = 0; source < _nodes; ++source) {
    
    vector<Flit *> flits_sent_by_subnet(_subnets);
    
    int const last_class = _last_class[source];

    for(int i = 1; i <= _classes; ++i) {

      int const c = (last_class + i) % _classes;

      if(!_partial_packets[c][source].empty()) {

	Flit * cf = _partial_packets[c][source].front();
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
	
	_partial_packets[c][source].pop_front();
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
	if(!_partial_packets[c][source].empty() && !f->tail) {
	  Flit * nf = _partial_packets[c][source].front();
	  nf->vc = f->vc;
	}
	
	if((_sim_state == warming_up) || (_sim_state == running)) {
	  ++_sent_flits[c][source];
	}

	_net[f->subnetwork]->WriteFlit(f, source);

      }	
    }
  }

  for(int subnet = 0; subnet < _subnets; ++subnet) {
    for(int dest = 0; dest < _nodes; ++dest) {
      map<int, Flit *>::const_iterator iter = flits[subnet].find(dest);
      if(iter != flits[subnet].end()) {
	Flit * const & f = iter->second;
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
  
#ifdef TRACK_FLOWS
  vector<vector<int> > received_flits(_classes*_subnets*_routers);
  vector<vector<int> > sent_flits(_classes*_subnets*_routers);
  vector<vector<int> > stored_flits(_classes*_subnets*_routers);
  vector<vector<int> > active_packets(_classes*_subnets*_routers);
  
  for (int subnet = 0; subnet < _subnets; ++subnet) {
    for(int router = 0; router < _routers; ++router) {
      Router * const r = _router[subnet][router];
      for(int c = 0; c < _classes; ++c) {
	received_flits[(c*_subnets+subnet)*_routers+router] = r->GetReceivedFlits(c);
	sent_flits[(c*_subnets+subnet)*_routers+router] = r->GetSentFlits(c);
	stored_flits[(c*_subnets+subnet)*_routers+router] = r->GetStoredFlits(c);
	active_packets[(c*_subnets+subnet)*_routers+router] = r->GetActivePackets(c);
	r->ResetFlowStats(c);
      }
    }
  }
  if(_received_flits_out) *_received_flits_out << received_flits << endl;
  if(_stored_flits_out) *_stored_flits_out << stored_flits << endl;
  if(_sent_flits_out) *_sent_flits_out << sent_flits << endl;
  if(_active_packets_out) *_active_packets_out << active_packets << endl;
#endif

  ++_time;
  assert(_time);
  if(gTrace){
    cout<<"TIME "<<_time<<endl;
  }

}
  
bool TrafficManager::_PacketsOutstanding( ) const
{
  for ( int c = 0; c < _classes; ++c ) {
    if ( !_measured_in_flight_flits[c].empty() ) {
      return true;
    }
  }
  return false;
}

void TrafficManager::_ResetSim( )
{
  _time = 0;
  
  //remove any pending request from the previous simulations
  for ( int c = 0; c < _classes; ++c ) {
    _requests_outstanding[c].assign(_nodes, 0);
  }
}

void TrafficManager::_ClearStats( )
{
  _slowest_flit.assign(_classes, -1);

  for ( int c = 0; c < _classes; ++c ) {

    _plat_stats[c]->Clear( );
    _frag_stats[c]->Clear( );
  
    _offered_flits[c].assign(_nodes, 0);
    _sent_flits[c].assign(_nodes, 0);
    _accepted_flits[c].assign(_nodes, 0);
    
    for ( int i = 0; i < _nodes; ++i ) {
      for ( int j = 0; j < _nodes; ++j ) {
	_pair_plat[c][i*_nodes+j]->Clear( );
      }
    }

    _hop_stats[c]->Clear();

  }

#ifdef TRACK_STALLS
  for(int s = 0; s < _subnets; ++s) {
    for(int r = 0; r < _routers; ++r) {
      for(int c = 0; c < _classes; ++c) {
	_router[s][r]->ResetStallStats(c);
      }
    }
  }
#endif

  _reset_time = _time;
}

void TrafficManager::_ComputeStats( const vector<int> & stats, int *sum, int *min, int *max, int *min_pos, int *max_pos ) const 
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

bool TrafficManager::Run( )
{
  for ( int sim = 0; sim < _total_sims; ++sim ) {

    _ClearStats( );

    _ResetSim( );

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

    if(_stats_out) {
      WriteStats(*_stats_out);
    }
    _UpdateOverallStats();
  }
  
  DisplayOverallStats();
  if(_print_csv_results) {
    DisplayOverallStatsCSV();
  }
  
  return true;
}

void TrafficManager::DisplayOverallStats(ostream & os) const
{
  for ( int c = 0; c < _classes; ++c ) {
    if(_measure_stats[c]) {
      cout << "====== Traffic class " << c << " ======" << endl;
      _DisplayOverallClassStats(c, os);
    }
  }
}

void TrafficManager::DisplayOverallStatsCSV(ostream & os) const
{
  os << "header:class," << _OverallStatsHeaderCSV() << endl;
  for(int c = 0; c < _classes; ++c) {
    if(_measure_stats[c]) {
      os << "results:" << c << ',' << _OverallClassStatsCSV(c) << endl;
    }
  }
}

void TrafficManager::_UpdateOverallStats() {

  for ( int c = 0; c < _classes; ++c ) {
    
    if(_measure_stats[c] == 0) {
      continue;
    }
    
    assert(_plat_stats[c]->NumSamples() > 0);
    _overall_min_plat[c] += _plat_stats[c]->Min();
    _overall_avg_plat[c] += _plat_stats[c]->Average();
    _overall_max_plat[c] += _plat_stats[c]->Max();
    assert(_frag_stats[c]->NumSamples() > 0);
    _overall_min_frag[c] += _frag_stats[c]->Min();
    _overall_avg_frag[c] += _frag_stats[c]->Average();
    _overall_max_frag[c] += _frag_stats[c]->Max();
    
    assert(_hop_stats[c]->NumSamples() > 0);
    _overall_hop_stats[c] += _hop_stats[c]->Average();

    int count_min, count_sum, count_max;
    double rate_min, rate_sum, rate_max;
    double rate_avg;
    double time_delta = (double)(_drain_time - _reset_time);
    _ComputeStats( _offered_flits[c], &count_sum, &count_min, &count_max );
    rate_min = (double)count_min / time_delta;
    rate_sum = (double)count_sum / time_delta;
    rate_max = (double)count_max / time_delta;
    rate_avg = rate_sum / (double)_nodes;
    _overall_min_offered[c] += rate_min;
    _overall_avg_offered[c] += rate_avg;
    _overall_max_offered[c] += rate_max;
    _ComputeStats( _sent_flits[c], &count_sum, &count_min, &count_max );
    rate_min = (double)count_min / time_delta;
    rate_sum = (double)count_sum / time_delta;
    rate_max = (double)count_max / time_delta;
    rate_avg = rate_sum / (double)_nodes;
    _overall_min_sent[c] += rate_min;
    _overall_avg_sent[c] += rate_avg;
    _overall_max_sent[c] += rate_max;
    _ComputeStats( _accepted_flits[c], &count_sum, &count_min, &count_max );
    rate_min = (double)count_min / time_delta;
    rate_sum = (double)count_sum / time_delta;
    rate_max = (double)count_max / time_delta;
    rate_avg = rate_sum / (double)_nodes;
    _overall_min_accepted[c] += rate_min;
    _overall_avg_accepted[c] += rate_avg;
    _overall_max_accepted[c] += rate_max;

#ifdef TRACK_STALLS
    for(int subnet = 0; subnet < _subnets; ++subnet) {
      for(int router = 0; router < _routers; ++router) {
	Router const * const r = _router[subnet][router];
	for(int c = 0; c < _classes; ++c) {
	  _overall_buffer_busy_stalls[c] += r->GetBufferBusyStalls(c);
	  _overall_buffer_conflict_stalls[c] += r->GetBufferConflictStalls(c);
	  _overall_buffer_full_stalls[c] += r->GetBufferFullStalls(c);
	  _overall_crossbar_conflict_stalls[c] += r->GetCrossbarConflictStalls(c);
	}
      }
    }
#endif

  }
}

void TrafficManager::DisplayStats(ostream & os) const {
  os << "===== Time: " << _time << " =====" << endl;

  for(int c = 0; c < _classes; ++c) {
    if(_measure_stats[c]) {
      os << "Class " << c << ":" << endl;
      _DisplayClassStats(c, os);
    }
  }
}

void TrafficManager::_DisplayClassStats(int c, ostream & os) const {
  
  os << "Minimum latency = " << _plat_stats[c]->Min() << endl;
  os << "Average latency = " << _plat_stats[c]->Average() << endl;
  os << "Maximum latency = " << _plat_stats[c]->Max() << endl;
  os << "Minimum fragmentation = " << _frag_stats[c]->Min() << endl;
  os << "Average fragmentation = " << _frag_stats[c]->Average() << endl;
  os << "Maximum fragmentation = " << _frag_stats[c]->Max() << endl;
  
  int count_sum, count_min, count_max;
  double rate_sum, rate_min, rate_max;
  double rate_avg;
  int min_pos, max_pos;
  double time_delta = (double)(_time - _reset_time);
  _ComputeStats(_offered_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
  rate_sum = (double)count_sum / time_delta;
  rate_min = (double)count_min / time_delta;
  rate_max = (double)count_max / time_delta;
  rate_avg = rate_sum / (double)_nodes;
  cout << "Minimum offered flit rate = " << rate_min 
       << " (at node " << min_pos << ")" << endl
       << "Average offered flit rate = " << rate_avg << endl
       << "Maximum offered flit rate = " << rate_max
       << " (at node " << max_pos << ")" << endl;
  _ComputeStats(_sent_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
  rate_sum = (double)count_sum / time_delta;
  rate_min = (double)count_min / time_delta;
  rate_max = (double)count_max / time_delta;
  rate_avg = rate_sum / (double)_nodes;
  cout << "Minimum injected flit rate = " << rate_min 
       << " (at node " << min_pos << ")" << endl
       << "Average injected flit rate = " << rate_avg << endl
       << "Maximum injected flit rate = " << rate_max
       << " (at node " << max_pos << ")" << endl;
  _ComputeStats(_accepted_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
  rate_sum = (double)count_sum / time_delta;
  rate_min = (double)count_min / time_delta;
  rate_max = (double)count_max / time_delta;
  rate_avg = rate_sum / (double)_nodes;
  cout << "Minimum accepted flit rate = " << rate_min 
       << " (at node " << min_pos << ")" << endl
       << "Average accepted flit rate = " << rate_avg << endl
       << "Maximum accepted flit rate = " << rate_max
       << " (at node " << max_pos << ")" << endl;
  
  os << "Total in-flight flits = " << _total_in_flight_flits[c].size()
       << " (" << _measured_in_flight_flits[c].size() << " measured)"
       << endl;
}

void TrafficManager::WriteStats(ostream & os) const {
  os << "%=================================" << endl;
  for(int c = 0; c < _classes; ++c) {
    if(_measure_stats[c]) {
      _WriteClassStats(c, os);
    }
  }
}

void TrafficManager::_WriteClassStats(int c, ostream & os) const {
  
  os << "lat(" << c+1 << ") = " << _plat_stats[c]->Average() << ";" << endl
     << "lat_hist(" << c+1 << ",:) = " << *_plat_stats[c] << ";" << endl
     << "frag_hist(" << c+1 << ",:) = " << *_frag_stats[c] << ";" << endl
     << "pair_sent(" << c+1 << ",:) = [ ";
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

  double time_delta = (double)(_time - _reset_time);

  os << "];" << endl
     << "offered(" << c+1 << ",:) = [ ";
  for ( int d = 0; d < _nodes; ++d ) {
    os << _offered_flits[c][d] / time_delta << " ";
  }
  os << "];" << endl
     << "sent(" << c+1 << ",:) = [ ";
  for ( int d = 0; d < _nodes; ++d ) {
    os << _sent_flits[c][d] / time_delta << " ";
  }
  os << "];" << endl
     << "accepted(" << c+1 << ",:) = [ ";
  for ( int d = 0; d < _nodes; ++d ) {
    os << _accepted_flits[c][d] / time_delta << " ";
  }
  os << "];" << endl;
}

void TrafficManager::_DisplayOverallClassStats( int c, ostream & os ) const {
  
  os << "Overall minimum latency = " << _overall_min_plat[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall average latency = " << _overall_avg_plat[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall maximum latency = " << _overall_max_plat[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl;
    
  os << "Overall minimum fragmentation = " << _overall_min_frag[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall average fragmentation = " << _overall_avg_frag[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall maximum fragmentation = " << _overall_max_frag[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl;
  os << "Overall minimum offered rate = " << _overall_min_offered[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall average offered rate = " << _overall_avg_offered[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall maximum offered rate = " << _overall_max_offered[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl;
  os << "Overall minimum sent rate = " << _overall_min_sent[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall average sent rate = " << _overall_avg_sent[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall maximum sent rate = " << _overall_max_sent[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl;
  os << "Overall minimum accepted rate = " << _overall_min_accepted[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall average accepted rate = " << _overall_avg_accepted[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall maximum accepted rate = " << _overall_max_accepted[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl;
  os << "Overall average hops = " << _overall_hop_stats[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl;

#ifdef TRACK_STALLS
  os << "Overall buffer busy stalls = " << (double)_overall_buffer_busy_stalls[c] / (double)_total_sims << endl
     << "Overall buffer conflict stalls = " << (double)_overall_buffer_conflict_stalls[c] / (double)_total_sims << endl
     << "Overall buffer full stalls = " << (double)_overall_buffer_full_stalls[c] / (double)_total_sims << endl
     << "Overall crossbar conflict stalls = " << (double)_overall_crossbar_conflict_stalls[c] / (double)_total_sims << endl;
#endif

}

string TrafficManager::_OverallStatsHeaderCSV() const
{
  ostringstream os;
  os << "min_plat"
     << ',' << "avg_plat"
     << ',' << "max_plat"
     << ',' << "min_frag"
     << ',' << "avg_frag"
     << ',' << "max_frag"
     << ',' << "min_offered"
     << ',' << "avg_offered"
     << ',' << "max_offered"
     << ',' << "min_sent"
     << ',' << "avg_sent"
     << ',' << "max_sent"
     << ',' << "min_accepted"
     << ',' << "avg_accepted"
     << ',' << "max_accepted"
     << ',' << "hops";
  return os.str();
}

string TrafficManager::_OverallClassStatsCSV(int c) const
{
  ostringstream os;
  os << _overall_min_plat[c] / (double)_total_sims
     << ',' << _overall_avg_plat[c] / (double)_total_sims
     << ',' << _overall_max_plat[c] / (double)_total_sims
     << ',' << _overall_min_frag[c] / (double)_total_sims
     << ',' << _overall_avg_frag[c] / (double)_total_sims
     << ',' << _overall_max_frag[c] / (double)_total_sims
     << ',' << _overall_min_offered[c] / (double)_total_sims
     << ',' << _overall_avg_offered[c] / (double)_total_sims
     << ',' << _overall_max_offered[c] / (double)_total_sims
     << ',' << _overall_min_sent[c] / (double)_total_sims
     << ',' << _overall_avg_sent[c] / (double)_total_sims
     << ',' << _overall_max_sent[c] / (double)_total_sims
     << ',' << _overall_min_accepted[c] / (double)_total_sims
     << ',' << _overall_avg_accepted[c] / (double)_total_sims
     << ',' << _overall_max_accepted[c] / (double)_total_sims
     << ',' << _overall_hop_stats[c] / (double)_total_sims;

#ifdef TRACK_STALLS
  os << ',' << (double)_overall_buffer_busy_stalls[c] / (double)_total_sims
     << ',' << (double)_overall_buffer_conflict_stalls[c] / (double)_total_sims
     << ',' << (double)_overall_buffer_full_stalls[c] / (double)_total_sims
     << ',' << (double)_overall_crossbar_conflict_stalls[c] / (double)_total_sims;
#endif

  return os.str();
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
    Error("Unable to open flit watch file: " + filename);
  }
}
