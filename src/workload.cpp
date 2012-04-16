// $Id$

/*
 Copyright (c) 2007-2011, Trustees of The Leland Stanford Junior University
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
  }
  return result;
}

void Workload::reset()
{
  _time = 0;
  while(!_pending_nodes.empty()) {
    _pending_nodes.pop();
  }
  while(!_deferred_nodes.empty()) {
    _deferred_nodes.pop();
  }
}

void Workload::advanceTime()
{
  ++_time;
  while(!_deferred_nodes.empty()) {
    int const source = _deferred_nodes.front();
    _deferred_nodes.pop();
    _pending_nodes.push(source);
  }
  assert(_pending_nodes.size() <= _nodes);
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
  _pending_nodes.pop();
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
  while(!_sleeping_nodes.empty()) {
    _sleeping_nodes.pop();
  }
  _qtime.assign(_nodes, 0);
  _injection->reset();
  _traffic->reset();
  for(int source = 0; source < _nodes; ++source) {
    if(_injection->test(source)) {
      _pending_nodes.push(source);
    } else {
      _sleeping_nodes.push(source);
    }
  }
}

void SyntheticWorkload::advanceTime()
{
  Workload::advanceTime();
  for(size_t i = 0; i < _sleeping_nodes.size(); ++i) {
    int const & source = _sleeping_nodes.front();
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
    } else {
      _sleeping_nodes.push(source);
    }
    _sleeping_nodes.pop();
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
  int const & source = _pending_nodes.front();
  return _qtime[source];
}

void SyntheticWorkload::inject(int pid)
{
  assert(!_pending_nodes.empty());
  int const & source = _pending_nodes.front();
  _sleeping_nodes.push(source);
  _pending_nodes.pop();
}

TraceWorkload::TraceWorkload(int nodes, string const & filename, 
			     vector<int> const & packet_sizes, 
			     int limit, int skip, int scale)
  : Workload(nodes), 
    _packet_sizes(packet_sizes), _limit(limit), _scale(scale), _skip(skip)
{
  assert(limit < 0 || limit > skip);
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

void TraceWorkload::_refill(int time)
{
  while(((_limit < 0) || (_count < _limit)) && !_trace->eof()) {
    ++_count;
    int delay, source, dest, type;
    *_trace >> delay >> source >> dest >> type;
    assert(delay >= 0);
    assert((source >= 0) && (source < _nodes));
    assert((dest >= 0) && (dest < _nodes));
    time += delay;
    if(type >= 0) {
      _next_source = source;
      _next_packet.time = time;
      _next_packet.dest = dest;
      _next_packet.type = type;
      if(((_scale > 0) ? (time / _scale) : (time * -_scale)) <= _time) {
	if(_ready_packets[source].empty()) {
	  _pending_nodes.push(source);
	}
	_ready_packets[source].push(_next_packet);
	_next_source = -1;
      } else {
	break;
      }
    }
  }
  assert(_deferred_nodes.size() <= _nodes);
}

void TraceWorkload::reset()
{
  Workload::reset();

  _count = 0;
  _trace->seekg(0);
  while((_count < _skip) && !_trace->eof()) {
    ++_count;
    int delay, source, dest, type;
    *_trace >> delay >> source >> dest >> type;
    assert(delay >= 0);
    assert((source >= 0) && (source < _nodes));
    assert((dest >= 0) && (dest < _nodes));
  }
  _next_source = -1;
  _refill(0);
}

void TraceWorkload::advanceTime()
{
  Workload::advanceTime();

  if(_next_source >= 0) {
    int const time = _next_packet.time;
    if(((_scale > 0) ? (time / _scale) : (time * -_scale)) <= _time) {
      if(_ready_packets[_next_source].empty()) {
	_pending_nodes.push(_next_source);
      }
      _ready_packets[_next_source].push(_next_packet);
      _next_source = -1;
      _refill(time);
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
  int time = _ready_packets[source].front().time;
  if(_scale > 0) {
    time /= _scale;
  } else {
    time *= -_scale;
  }
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
  }
  assert(_deferred_nodes.size() <= _nodes);
}
