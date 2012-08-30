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

#include <iostream>
#include <cstdlib>

#include "workload.hpp"
#include "random_utils.hpp"

//#define DEBUG_NETRACE

Workload::Workload(int nodes) : _nodes(nodes)
{
  
}

Workload::~Workload()
{
  
}

Workload * Workload::New(string const & workload, int nodes,
			 Configuration const * const config)
{
  string workload_name;
  string param_str;
  size_t left = workload.find_first_of('(');
  if(left == string::npos) {
    cout << "Error: Missing parameter in workload specification: " << workload
	 << endl;
    exit(-1);
  }
  workload_name = workload.substr(0, left);
  size_t right = workload.find_last_of(')');
  if(right == string::npos) {
    param_str = workload.substr(left+1);
  } else {
    param_str = workload.substr(left+1, right-left-1);
  }
  vector<string> params = tokenize_str(param_str);
  
  Workload * result = NULL;
  if(workload_name == "null") {
    result = new NullWorkload(nodes);
  } else if(workload_name == "synthetic") {
    if(params.size() < 2) {
      cout << "Error: Missing parameter in synthetic workload definition: "
	   << workload << endl;
      exit(-1);
    }
    double load = atof(params[0].c_str());
    string traffic = params[1];
    string injection = (params.size() > 2) ? params[2] : (config ? config->GetStr("injection_process") : "bernoulli");
    vector<int> sizes = (params.size() > 3) ? tokenize_int(params[3]) : vector<int>(1, config ? config->GetInt("packet_size") : 1);
    vector<int> rates = (params.size() > 4) ? tokenize_int(params[4]) : vector<int>(1, 1);
    rates.resize(sizes.size(), rates.back());
    result = new SyntheticWorkload(nodes, load, traffic, injection, sizes, 
				   rates, config);
  } else if(workload_name == "trace") {
    if(params.size() < 2) {
      cout << "Error: Missing parameter in trace workload definition: "
	   << workload << endl;
      exit(-1);
    }
    string const & filename = params[0];
    vector<int> packet_sizes = tokenize_int(params[1]);
    int limit = -1;
    int skip = 0;
    int scale = 1;
    if(params.size() > 2) {
      limit = atoi(params[2].c_str());
      if(params.size() > 3) {
	skip = atoi(params[3].c_str());
	if(params.size() > 4) {
	  scale = atoi(params[4].c_str());
	}
      }
    }
    result = new TraceWorkload(nodes, filename, packet_sizes, limit, skip, scale);
  } else if(workload_name == "netrace") {
    if(params.size() < 1) {
      cout << "Error: Missing parameter in trace workload definition: "
	   << workload << endl;
      exit(-1);
    }
    string const & filename = params[0];
    int channel_width = config ? (config->GetInt("channel_width")) : 128;
    int size_offset = 0;
    int region = -1;
    long long int limit = -1;
    int scale = 1;
    bool enforce_deps = true;
    bool enforce_lats = false;
    if(params.size() > 1) {
      limit = atoll(params[1].c_str());
      if(params.size() > 2) {
	scale = atoi(params[2].c_str());
	if(params.size() > 3) {
	  region = atoi(params[3].c_str());
	  if(params.size() > 4) {
	    enforce_deps = atoi(params[4].c_str());
	    if(params.size() > 5) {
	      enforce_lats = atoi(params[5].c_str());
	      if(params.size() > 6) {
		size_offset = atoi(params[6].c_str());
	      }
	    }
	  }
	}
      }
    }
    result = new NetraceWorkload(nodes, filename, channel_width, limit, scale, region, enforce_deps, enforce_lats, size_offset);
  }
  return result;
}

void Workload::reset()
{
  while(!_pending_nodes.empty()) {
    _pending_nodes.pop();
  }
  while(!_deferred_nodes.empty()) {
    _deferred_nodes.pop();
  }
}

void Workload::advanceTime()
{
  while(!_deferred_nodes.empty()) {
    int const source = _deferred_nodes.front();
    _deferred_nodes.pop();
    _pending_nodes.push(source);
    assert(_pending_nodes.size() <= (size_t)_nodes);
  }
}

bool Workload::empty() const
{
  return _pending_nodes.empty();
}

int Workload::source() const
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  return source;
}

void Workload::defer()
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  _deferred_nodes.push(source);
  assert(_deferred_nodes.size() <= (size_t)_nodes);
  _pending_nodes.pop();
}

void Workload::printStats(ostream & os) const
{
  
}

SyntheticWorkload::SyntheticWorkload(int nodes, double load, 
				     string const & traffic, 
				     string const & injection, 
				     vector<int> const & sizes, 
				     vector<int> const & rates, 
				     Configuration const * const config)
  : Workload(nodes), _sizes(sizes), _rates(rates), _max_val(-1)
{
  _injection = InjectionProcess::New(injection, nodes, load, config);
  _traffic = TrafficPattern::New(traffic, nodes, config);
  _qtime.resize(nodes);
  int size = rates.size();
  for(int i = 0; i < size; ++i) {
    int rate = rates[i];
    assert(rate >= 0);
    _max_val += rate;
  }
}

SyntheticWorkload::~SyntheticWorkload()
{
  delete _injection;
  delete _traffic;
}

void SyntheticWorkload::reset()
{
  Workload::reset();
  _time = 0;
  while(!_sleeping_nodes.empty()) {
    _sleeping_nodes.pop();
  }
  _qtime.assign(_nodes, 0);
  _injection->reset();
  _traffic->reset();
  for(int source = 0; source < _nodes; ++source) {
    if(_injection->test(source)) {
      _pending_nodes.push(source);
      assert(_pending_nodes.size() <= (size_t)_nodes);
    } else {
      _sleeping_nodes.push(source);
      assert(_sleeping_nodes.size() <= (size_t)_nodes);
    }
  }
}

void SyntheticWorkload::advanceTime()
{
  Workload::advanceTime();
  ++_time;
  for(size_t i = 0; i < _sleeping_nodes.size(); ++i) {
    int const source = _sleeping_nodes.front();
    _sleeping_nodes.pop();
    bool generated = false;
    while(_qtime[source] < _time) {
      ++_qtime[source];
      if(_injection->test(source)) {
	generated = true;
	break;
      }
    }
    if(generated) {
      _pending_nodes.push(source);
      assert(_pending_nodes.size() <= (size_t)_nodes);
    } else {
      _sleeping_nodes.push(source);
      assert(_sleeping_nodes.size() <= (size_t)_nodes);
    }
  }
}

bool SyntheticWorkload::completed() const
{
  return false;
}

int SyntheticWorkload::dest() const
{
  assert(!_pending_nodes.empty());
  int const dest = _traffic->dest(source());
  assert((dest >= 0) && (dest < _nodes));
  return dest;
}

int SyntheticWorkload::size() const
{
  assert(!_pending_nodes.empty());
  int num_sizes = _sizes.size();
  if(num_sizes == 1) {
    return _sizes[0];
  }
  int pct = RandomInt(_max_val);
  for(int i = 0; i < (num_sizes - 1); ++i) {
    int const limit = _rates[i];
    if(limit > pct) {
      return _sizes[i];
    } else {
      pct -= limit;
    }
  }
  assert(_rates.back() > pct);
  return _sizes.back();
}

int SyntheticWorkload::time() const
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  return _qtime[source];
}

void SyntheticWorkload::inject(int pid)
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  _pending_nodes.pop();
  _sleeping_nodes.push(source);
  assert(_sleeping_nodes.size() <= (size_t)_nodes);
}

TraceWorkload::TraceWorkload(int nodes, string const & filename, 
			     vector<int> const & packet_sizes, 
			     int limit, unsigned int skip, unsigned int scale)
  : Workload(nodes), 
    _packet_sizes(packet_sizes), _limit(limit), _scale(scale), _skip(skip)
{
  _ready_packets.resize(nodes);
  _trace = new ifstream(filename.c_str());
  if(!_trace->is_open()) {
    cerr << "Unable to open trace file: " << filename << endl;
    exit(-1);
  }
}

TraceWorkload::~TraceWorkload()
{
  if(_trace) {
    if(_trace->is_open()) {
      _trace->close();
    }
    delete _trace;
  }
}

void TraceWorkload::_refill()
{
  unsigned int time = _time;
  while(((_limit < 0) || (_count < (unsigned int)_limit)) && !_trace->eof()) {
    ++_count;
    int delay, source, dest, type;
    *_trace >> delay >> source >> dest >> type;
    assert(delay >= 0);
    assert((source >= 0) && (source < _nodes));
    assert((dest >= 0) && (dest < _nodes));
    time += (unsigned int)delay;
    if(type >= 0) {
      _next_source = source;
      _next_packet.time = time;
      _next_packet.dest = dest;
      _next_packet.type = type;
      assert(time >= _time);
      if(time == _time) {
	if(_ready_packets[source].empty()) {
	  _pending_nodes.push(source);
	  assert(_pending_nodes.size() <= (size_t)_nodes);
	}
	_ready_packets[source].push(_next_packet);
	_next_source = -1;
      } else {
	break;
      }
    }
  }
}

void TraceWorkload::reset()
{
  Workload::reset();
  _time = 0;
  _trace->seekg(0);
  unsigned int count = 0;
  while((count < _skip) && !_trace->eof()) {
    ++count;
    int delay, source, dest, type;
    *_trace >> delay >> source >> dest >> type;
    assert(delay >= 0);
    assert((source >= 0) && (source < _nodes));
    assert((dest >= 0) && (dest < _nodes));
  }
  _count = 0;
  _next_source = -1;
  _refill();
}

void TraceWorkload::advanceTime()
{
  Workload::advanceTime();
  for(unsigned int s = 0; s < _scale; ++s) {
    ++_time;
    if(_next_source >= 0) {
      unsigned int const time = _next_packet.time;
      assert(time >= _time);
      if(time == _time) {
	if(_ready_packets[_next_source].empty()) {
	  _pending_nodes.push(_next_source);
	  assert(_pending_nodes.size() <= (size_t)_nodes);
	}
	_ready_packets[_next_source].push(_next_packet);
	_next_source = -1;
	_refill();
      }
    }
  }
}

bool TraceWorkload::completed() const
{
  return (_pending_nodes.empty() && _deferred_nodes.empty() && 
	  (_next_source < 0));
}

int TraceWorkload::dest() const
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  assert(!_ready_packets[source].empty());
  int const dest = _ready_packets[source].front().dest;
  assert((dest >= 0) && (dest < _nodes));
  return dest;
}

int TraceWorkload::size() const
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  assert(!_ready_packets[source].empty());
  int const size = _packet_sizes[_ready_packets[source].front().type];
  assert(size > 0);
  return size;
}

int TraceWorkload::time() const
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  assert(!_ready_packets[source].empty());
  int const time = (int)(_ready_packets[source].front().time / _scale);
  assert(time >= 0);
  return time;
}

void TraceWorkload::inject(int pid)
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  _pending_nodes.pop();
  assert(!_ready_packets[source].empty());
  _ready_packets[source].pop();
  if(!_ready_packets[source].empty()) {
    _deferred_nodes.push(source);
    assert(_deferred_nodes.size() <= (size_t)_nodes);
  }
}

void TraceWorkload::retire(int pid)
{

}

void TraceWorkload::printStats(ostream & os) const
{
  os << "Packets read from trace = " << _count << endl;
  os << "Future packets = " << ((_next_source < 0) ? 0 : 1) << endl;
  int pend_count = 0;
  for(int n = 0; n < _nodes; ++n) {
    pend_count += _ready_packets[n].size();
  }
  os << "Packets pending injection = " << pend_count << endl;
}

NetraceWorkload::NetraceWorkload(int nodes, string const & filename, 
				 unsigned int channel_width, 
				 long long int limit, unsigned int scale, 
				 int region, bool enforce_deps, 
				 bool enforce_lats, 
				 unsigned int size_offset)
  : Workload(nodes), _channel_width(channel_width), _size_offset(size_offset), 
    _scale(scale), _enforce_deps(enforce_deps), _enforce_lats(enforce_lats)
{
  _l2_tag_latency = 2;
  _l2_data_latency = 8;
  _mem_latency = 150;
  _trace_net_delay = 8;
  _window_size = enforce_lats ? max(max(_l2_tag_latency, _l2_data_latency), _mem_latency) : 1;
  _ready_packets.resize(nodes);
  _ctx = (nt_context_t*)calloc(1, sizeof(nt_context_t));
  _next_packet = NULL;
  _last_response_eject_time.resize(_nodes);
  nt_open_trfile(_ctx, filename.c_str());
  if(!enforce_deps) {
    nt_disable_dependencies(_ctx);
  }
  nt_header_t* header = nt_get_trheader(_ctx);
  _skip = 0ll;
  assert(nodes == header->num_nodes);
  unsigned int last_region;
  if(region < 0) {
    _region = 0;
    last_region = header->num_regions - 1;
  } else {
    assert((unsigned int)region < header->num_regions);
    _region = (unsigned int)region;
    last_region = (unsigned int)region;
  }
  for(unsigned int r = 0; r < _region; ++r) {
    _skip += header->regions[r].num_cycles;
  }
#ifdef DEBUG_NETRACE
  if(_skip) {
    cout << "CONSTR: Skipping " << region << " regions (" << _skip << " cycles)." << endl;
  }
#endif
  _limit = 0ll;
  for(unsigned int r = _region; r <= last_region; ++r) {
    _limit += header->regions[r].num_packets;
  }
  if(limit >= 0) {
    _limit = min(_limit, (unsigned long long int)limit);
  }
#ifdef DEBUG_NETRACE
  cout << "CONSTR: Playing back " << _limit << " packets." << endl;
#endif 
}

NetraceWorkload::~NetraceWorkload()
{
  assert(_pending_nodes.empty());
  assert(_deferred_nodes.empty());
  assert(_in_flight_packets.empty());
  nt_close_trfile(_ctx);
  free(_ctx);
}

void NetraceWorkload::_refill()
{
  while(_count < _limit) {
    _next_packet = nt_read_packet(_ctx);
    if(!_next_packet) {
#ifdef DEBUG_NETRACE
      cout << "REFILL: Reached end of trace." << endl;
#endif
      break;
    }
    ++_count;
#ifdef DEBUG_NETRACE
    cout << "REFILL: Read next packet (" << _next_packet->id << ")" << endl;
    cout << "REFILL: ";
    nt_print_packet(_next_packet);
#endif
    _next_packet->cycle -= _skip;
    if(_enforce_deps && _enforce_lats &&
       (nt_get_dst_type(_next_packet) <= 1) &&
       ((_next_packet->type == 2) ||
	(_next_packet->type == 3) ||
	(_next_packet->type == 5) ||
	(_next_packet->type == 14) ||
	(_next_packet->type == 16) ||
	(_next_packet->type == 28) ||
	(_next_packet->type == 30))) {
      assert(_response_eject_time.count(_next_packet->id) == 0);
      unsigned long long int const eject_time = _next_packet->cycle + _trace_net_delay;
      _response_eject_time.insert(make_pair(_next_packet->id, eject_time));
    }
    assert(_next_packet->cycle >= _time);
    if(_next_packet->cycle == _time) {
#ifdef DEBUG_NETRACE
      cout << "REFILL: Injection time has elapsed." << endl;
#endif
      int const source = _next_packet->src;
      assert((source >= 0) && (source < _nodes));
      if(!_enforce_deps || nt_dependencies_cleared(_ctx, _next_packet)) {
#ifdef DEBUG_NETRACE
	cout << "REFILL: No dependencies." << endl;
#endif
	if(_ready_packets[source].empty()) {
#ifdef DEBUG_NETRACE
	  cout << "REFILL: Waking up node " << source << "." << endl;
#endif
	  _pending_nodes.push(source);
	  assert(_pending_nodes.size() <= (size_t)_nodes);
	}
	_ready_packets[source].push(_next_packet);
      } else {
#ifdef DEBUG_NETRACE
	cout << "REFILL: Unmet dependencies." << endl;
#endif
	assert(_stalled_packets.count(_next_packet->id) == 0);
	_stalled_packets.insert(make_pair(_next_packet->id, _next_packet));
      }
      _next_packet = NULL;
    } else if(_enforce_lats && (_next_packet->cycle < _time + _window_size)) {
#ifdef DEBUG_NETRACE
      cout << "REFILL: Injection time is within window; queuing packet." << endl;
#endif
      if(!_enforce_deps || nt_dependencies_cleared(_ctx, _next_packet)) {
#ifdef DEBUG_NETRACE
	cout << "REFILL: No dependencies." << endl;
#endif
	if(_future_packets.empty() || 
	   (_future_packets.back()->cycle <= _next_packet->cycle)) {
	  _future_packets.push_back(_next_packet);
	} else {
	  list<nt_packet_t *>::iterator iter = _future_packets.begin();
	  while((*iter)->cycle < _next_packet->cycle) {
	    ++iter;
	  }
	  _future_packets.insert(iter, _next_packet);
	}
      } else {
#ifdef DEBUG_NETRACE
	cout << "REFILL: Unmet dependencies." << endl;
#endif
	assert(_stalled_packets.count(_next_packet->id) == 0);
	_stalled_packets.insert(make_pair(_next_packet->id, _next_packet));
      }
      _next_packet = NULL;
    } else {
#ifdef DEBUG_NETRACE
      cout << "REFILL: Injection time is in the future; sleeping." << endl;
#endif
      break;
    }
  }
}

void NetraceWorkload::reset()
{
#ifdef DEBUG_NETRACE
  cout << "RESET : Restarting trace." << endl;
#endif
  Workload::reset();
  _time = 0ll;
  _last_response_eject_time.assign(_nodes, 0ll);
  for(int i = 0; i < _nodes; ++i) {
    assert(_ready_packets[i].empty());
  }
  assert(_future_packets.empty());
  assert(_stalled_packets.empty());
  assert(_in_flight_packets.empty());
  assert(!_next_packet);
  nt_header_t* header = nt_get_trheader(_ctx);
  nt_seek_region(_ctx, &header->regions[_region]);
  _count = 0ll;
  _refill();
}

void NetraceWorkload::advanceTime()
{
  for(set<unsigned int>::iterator iter = _check_packets.begin();
      iter != _check_packets.end(); ++iter) {
    unsigned int id = *iter;
#ifdef DEBUG_NETRACE
    cout << "ADVANC: Checking if packet " << id << " is cleared." << endl;
#endif
    map<unsigned int, nt_packet_t *>::iterator piter = _stalled_packets.find(id);
    if(piter == _stalled_packets.end()) {
#ifdef DEBUG_NETRACE
      cout << "ADVANC: Dependent packet " << id << " has not yet been read from trace." << endl;
#endif
      continue;
    }
    nt_packet_t * packet = piter->second;
    assert(packet);
    assert(packet->id == id);
    assert(_enforce_deps);
    if(nt_dependencies_cleared(_ctx, packet)) {
#ifdef DEBUG_NETRACE
      cout << "ADVANC: Dependencies cleared for packet " << id << "." << endl;
#endif
      if(_enforce_lats) {
	unsigned int latency = 0;
	if((nt_get_src_type(packet) == 1) &&
	   ((packet->type == 1) ||
	    (packet->type == 4) ||
	    (packet->type == 13) ||
	    (packet->type == 16) ||
	    (packet->type == 27) ||
	    (packet->type == 29))) {
	  int const source = packet->src;
	  assert((source >= 0) && (source < _nodes));
	  unsigned long long int const last_response_eject_time = _last_response_eject_time[source];
	  assert(packet->cycle >= last_response_eject_time - 1);
	  if(packet->cycle >= last_response_eject_time) {
	    latency = (unsigned int)(packet->cycle - last_response_eject_time);
	  }
	} else {
	  if(nt_get_src_type(packet) == 2) {
	    if(nt_get_dst_type(packet) == 3) {
	      latency = _l2_tag_latency;
	    } else if(nt_get_dst_type(packet) <= 1) {
	      latency = _l2_data_latency;
	    }
	  } else if(nt_get_src_type(packet) == 3) {
	    latency = _mem_latency;
	  }
	}
	packet->cycle = max(packet->cycle, _time + latency);
	assert(packet->cycle >= _time);
	if(packet->cycle > _time) {
#ifdef DEBUG_NETRACE
	  cout << "ADVANC: New injection time is in the future; queuing packet." << endl;
#endif
	  if(_future_packets.empty() || 
	     (_future_packets.back()->cycle <= packet->cycle)) {
	    _future_packets.push_back(packet);
	  } else {
	    list<nt_packet_t *>::iterator iter = _future_packets.begin();
	    while((*iter)->cycle < packet->cycle) {
	      ++iter;
	    }
	    _future_packets.insert(iter, packet);
	  }
	}
      } else {
	assert(packet->cycle <= _time);
	packet->cycle = _time;
      }
      assert(packet->cycle >= _time);
      if(!_enforce_lats || (packet->cycle == _time)) {
	int const source = packet->src;
	assert((source >= 0) && (source < _nodes));
	if(_ready_packets[source].empty()) {
#ifdef DEBUG_NETRACE
	  cout << "ADVANC: Waking up node " << source << "." << endl;
#endif
	  _pending_nodes.push(source);
	  assert(_pending_nodes.size() <= (size_t)_nodes);
	}
	_ready_packets[source].push(packet);
      }
      _stalled_packets.erase(piter);
    }
  }
  _check_packets.clear();
  Workload::advanceTime();
  for(unsigned int s = 0; s < _scale; ++s) {
    ++_time;
    while(!_future_packets.empty()) {
      nt_packet_t * packet = _future_packets.front();
      assert(packet->cycle >= _time);
      if(packet->cycle == _time) {
	_future_packets.pop_front();
#ifdef DEBUG_NETRACE
	cout << "ADVANC: Injection time has elapsed for queued packet " << packet->id << "." << endl;
	cout << "ADVANC: ";
	nt_print_packet(packet);
#endif
	int const source = packet->src;
	assert((source >= 0) && (source < _nodes));
	assert(!_enforce_deps || nt_dependencies_cleared(_ctx, packet));
	if(_ready_packets[source].empty()) {
#ifdef DEBUG_NETRACE
	  cout << "ADVANC: Waking up node " << source << "." << endl;
#endif
	  _pending_nodes.push(source);
	  assert(_pending_nodes.size() <= (size_t)_nodes);
	}
	_ready_packets[source].push(packet);
      } else {
	break;
      }
    }
    if(_next_packet) {
      assert(_next_packet->cycle >= _time);
      if(_next_packet->cycle == _time) {
#ifdef DEBUG_NETRACE
	cout << "ADVANC: Injection time has elapsed for waiting packet " << _next_packet->id << "." << endl;
	cout << "ADVANC: ";
	nt_print_packet(_next_packet);
#endif
	int const source = _next_packet->src;
	assert((source >= 0) && (source < _nodes));
	if(!_enforce_deps || nt_dependencies_cleared(_ctx, _next_packet)) {
#ifdef DEBUG_NETRACE
	  cout << "ADVANC: No dependencies." << endl;
#endif
	  if(_ready_packets[source].empty()) {
#ifdef DEBUG_NETRACE
	    cout << "ADVANC: Waking up node " << source << "." << endl;
#endif
	    _pending_nodes.push(source);
	    assert(_pending_nodes.size() <= (size_t)_nodes);
	  }
	  _ready_packets[source].push(_next_packet);
	} else {
#ifdef DEBUG_NETRACE
	  cout << "ADVANC: Unmet dependencies." << endl;
#endif
	  assert(_stalled_packets.count(_next_packet->id) == 0);
	  _stalled_packets.insert(make_pair(_next_packet->id, _next_packet));
	}
	_next_packet = NULL;
	_refill();
      } else if(_enforce_lats && 
		(_next_packet->cycle < _time + _window_size)) {
#ifdef DEBUG_NETRACE
	cout << "ADVANC: Injection time is within window for waiting packet " << _next_packet->id << "; queuing packet." << endl;
	cout << "ADVANC: ";
	nt_print_packet(_next_packet);
#endif
	if(!_enforce_deps || nt_dependencies_cleared(_ctx, _next_packet)) {
#ifdef DEBUG_NETRACE
	  cout << "ADVANC: No dependencies." << endl;
#endif
	  if(_future_packets.empty() || 
	     (_future_packets.back()->cycle <= _next_packet->cycle)) {
	    _future_packets.push_back(_next_packet);
	  } else {
	    list<nt_packet_t *>::iterator iter = _future_packets.begin();
	    while((*iter)->cycle < _next_packet->cycle) {
	      ++iter;
	    }
	    _future_packets.insert(iter, _next_packet);
	  }
	} else {
#ifdef DEBUG_NETRACE
	  cout << "ADVANC: Unmet dependencies." << endl;
#endif
	  assert(_stalled_packets.count(_next_packet->id) == 0);
	  _stalled_packets.insert(make_pair(_next_packet->id, _next_packet));
	}
	_next_packet = NULL;
	_refill();
      } else {
#ifdef DEBUG_NETRACE
	cout << "ADVANC: Injection time is in the future for waiting packet " << _next_packet->id << "; sleeping." << endl;
	cout << "ADVANC: ";
	nt_print_packet(_next_packet);
#endif
      }
    }
  }
}

bool NetraceWorkload::completed() const
{
  return (_pending_nodes.empty() && _deferred_nodes.empty() && 
	  _stalled_packets.empty() && _future_packets.empty() && 
	  !_next_packet);
}

int NetraceWorkload::dest() const
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  assert(!_ready_packets[source].empty());
  int const dest = _ready_packets[source].front()->dst;
  assert((dest >= 0) && (dest < _nodes));
  return dest;
}

int NetraceWorkload::size() const
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  assert(!_ready_packets[source].empty());
  int const size = nt_get_packet_size(_ready_packets[source].front());
  assert(size > 0);
  int const scaled_size = (8 * size + _size_offset + _channel_width - 1) / _channel_width;
  return scaled_size;
}

int NetraceWorkload::time() const
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  assert(!_ready_packets[source].empty());
  int const time = (int)(_ready_packets[source].front()->cycle / _scale);
  assert(time >= 0);
  return time;
}

void NetraceWorkload::inject(int pid)
{
  assert(!_pending_nodes.empty());
  int const source = _pending_nodes.front();
  assert((source >= 0) && (source < _nodes));
  _pending_nodes.pop();
  assert(!_ready_packets[source].empty());
  nt_packet_t * packet = _ready_packets[source].front();
#ifdef DEBUG_NETRACE
  cout << "INJECT: Injecting packet " << packet->id << " at node " << source << "." << endl;
#endif
  _ready_packets[source].pop();
  _in_flight_packets.insert(make_pair(pid, packet));
  if(!_ready_packets[source].empty()) {
#ifdef DEBUG_NETRACE
    cout << "INJECT: " << _ready_packets[source].size() << " ready packets remaining." << endl;
#endif
    _deferred_nodes.push(source);
    assert(_deferred_nodes.size() <= (size_t)_nodes);
  }
}

void NetraceWorkload::retire(int pid)
{
  map<int, nt_packet_t *>::iterator iter = _in_flight_packets.find(pid);
  assert(iter != _in_flight_packets.end());
  nt_packet_t * packet = iter->second;
#ifdef DEBUG_NETRACE
  cout << "RETIRE: Ejecting packet " << packet->id << "." << endl;
#endif
  if(_enforce_deps && _enforce_lats &&
     (nt_get_dst_type(packet) <= 1) &&
     ((packet->type == 2) ||
      (packet->type == 3) ||
      (packet->type == 5) ||
      (packet->type == 14) ||
      (packet->type == 16) ||
      (packet->type == 28) ||
      (packet->type == 30))) {
    map<unsigned int, unsigned long long int>::iterator iter = _response_eject_time.find(packet->id);
    assert(iter != _response_eject_time.end());
    unsigned long long int const eject_time = iter->second;
    assert(_time >= eject_time - 1);
    int const dest = packet->dst;
    assert((dest >= 0) && (dest < _nodes));
#ifdef DEBUG_NETRACE
    if(_last_response_eject_time[dest] != eject_time) {
      cout << "RETIRE: Updating last response eject time for node " << dest << " to " << eject_time << " cycles." << endl;
    }
#endif
    _last_response_eject_time[dest] = eject_time;
    _response_eject_time.erase(iter);
  }
  _in_flight_packets.erase(iter);
  for(unsigned char d = 0; d < packet->num_deps; ++d) {
    unsigned int const dep = packet->deps[d];
#ifdef DEBUG_NETRACE
    cout << "RETIRE: Waking up dependent packet " << dep << "." << endl;
#endif
    _check_packets.insert(dep);
  }
  assert(!_ctx->self_throttling);
  nt_clear_dependencies_free_packet(_ctx, packet);
}

void NetraceWorkload::printStats(ostream & os) const
{
  os << "Packets read from trace = " << _count << " of " << _limit << " (" << (100ll*_count/_limit)<< "%)" << endl;
  os << "Future packets = " << (_next_packet ? 1 : 0) << endl;
  os << "Waiting packets = " << _future_packets.size() << endl;
  os << "Stalled packets = " << _stalled_packets.size() << endl;
  int pend_count = 0;
  for(int n = 0; n < _nodes; ++n) {
    pend_count += _ready_packets[n].size();
  }
  os << "Packets pending injection = " << pend_count << endl;
  os << "Packets in flight = " << _in_flight_packets.size() << endl;
}
