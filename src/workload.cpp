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
  vector<string> params = tokenize(param_str);
  
  Workload * result = NULL;
  if(workload_name == "synthetic") {
    if(params.size() < 4) {
      cout << "Error: Missing parameter in synthetic workload definition: "
	   << workload << endl;
      exit(-1);
    }
    double const load = atof(params[0].c_str());
    int const size = atoi(params[1].c_str());
    string const & injection = params[2];
    string const & traffic = params[3];
    result = new SyntheticWorkload(nodes, load, size, injection, traffic, 
				   config);
  } else if(workload_name == "trace") {
    if(params.size() < 2) {
      cout << "Error: Missing parameter in trace workload definition: "
	   << workload << endl;
      exit(-1);
    }
    vector<string> filenames = tokenize(params[0]);
    filenames.resize(nodes, filenames.back());
    vector<string> packet_sizes_str = tokenize(params[1]);
    vector<int> packet_sizes(packet_sizes_str.size());
    for(size_t i = 0; i < packet_sizes_str.size(); ++i) {
      packet_sizes[i] = atoi(packet_sizes_str[i].c_str());
    }
    int limit = -1;
    vector<int> scales;
    vector<int> skips;
    if(params.size() > 2) {
      limit = atoi(params[2].c_str());
      if(params.size() > 3) {
	vector<string> skips_str = tokenize(params[3]);
	skips.resize(skips_str.size());
	for(size_t i = 0; i < skips_str.size(); ++i) {
	  skips[i] = atoi(skips_str[i].c_str());
	}
	if(params.size() > 4) {
	  vector<string> scales_str = tokenize(params[4]);
	  scales.resize(scales_str.size());
	  for(size_t i = 0; i < scales_str.size(); ++i) {
	    scales[i] = atoi(scales_str[i].c_str());
	  }
	}
      }
    }
    result = new TraceWorkload(nodes, filenames, packet_sizes, limit, skips, scales);
  }
  return result;
}

void Workload::reset()
{
  _time = 0;
}

void Workload::advanceTime()
{
  ++_time;
}

SyntheticWorkload::SyntheticWorkload(int nodes, double load, int size, 
				     string const & injection, 
				     string const & traffic,
				     Configuration const * const config)
  : Workload(nodes), _size(size)
{
  _injection = InjectionProcess::New(injection, nodes, load, config);
  _traffic = TrafficPattern::New(traffic, nodes, config);
  _qtime.resize(nodes);
}

SyntheticWorkload::~SyntheticWorkload()
{
  delete _injection;
  delete _traffic;
}

void SyntheticWorkload::reset()
{
  Workload::reset();
  _qtime.assign(_nodes, 0);
  _injection->reset();
  _traffic->reset();
  while(!_ready.empty()) {
    _ready.pop();
  }
  while(!_pending.empty()) {
    _pending.pop();
  }
  while(!_deferred.empty()) {
    _deferred.pop();
  }
  for(int source = 0; source < _nodes; ++source) {
    if(_injection->test(source)) {
      _pending.push(source);
    } else {
      _ready.push(source);
    }
  }
}

void SyntheticWorkload::advanceTime()
{
  Workload::advanceTime();
  while(!_deferred.empty()) {
    int const & source = _deferred.front();
    _pending.push(source);
    _deferred.pop();
  }
  for(size_t i = 0; i < _ready.size(); ++i) {
    int const & source = _ready.front();
    bool generated = false;
    while(_qtime[source] < _time) {
      ++_qtime[source];
      if(_injection->test(source)) {
	generated = true;
	break;
      }
    }
    if(generated) {
      _pending.push(source);
    } else {
      _ready.push(source);
    }
    _ready.pop();
  }
}

bool SyntheticWorkload::empty() const
{
  return _pending.empty();
}

bool SyntheticWorkload::completed() const
{
  return false;
}

int SyntheticWorkload::source() const
{
  assert(!empty());
  int const & source = _pending.front();
  return source;
}

int SyntheticWorkload::dest() const
{
  assert(!empty());
  int const & source = _pending.front();
  return _traffic->dest(source);
}

int SyntheticWorkload::size() const
{
  assert(!empty());
  return _size;
}

int SyntheticWorkload::time() const
{
  assert(!empty());
  int const & source = _pending.front();
  return _qtime[source];
}

void SyntheticWorkload::inject()
{
  assert(!empty());
  int const & source = _pending.front();
  _ready.push(source);
  _pending.pop();
}

void SyntheticWorkload::defer()
{
  assert(!empty());
  int const & source = _pending.front();
  _deferred.push(source);
  _pending.pop();
}

TraceWorkload::TraceWorkload(int nodes, vector<string> const & filenames, 
			     vector<int> const & packet_sizes, int limit,
			     vector<int> const & skips, 
			     vector<int> const & scales)
  : Workload(nodes), _packet_sizes(packet_sizes), _limit(limit), 
    _scales(scales), _skips(skips)
{
  if(_scales.empty()) {
    _scales.push_back(1);
  }
  _scales.resize(nodes, _scales.back());

  if(_skips.empty()) {
    _skips.push_back(0);
  }
  _skips.resize(nodes, _skips.back());

  _traces.resize(nodes);
  for(int n = 0; n < _nodes; ++n) {
    string const & filename = (n < filenames.size()) ? filenames[n] : filenames.back();
    ifstream * trace = new ifstream(filename.c_str());
    _traces[n] = trace;
    if(!trace->is_open()) {
      cerr << "Unable to open trace file: " << filename << endl;
      exit(-1);
    }
  }
}

TraceWorkload::~TraceWorkload()
{
  for(int n = 0; n < _nodes; ++n) {
    ifstream * const trace = _traces[n];
    if(trace) {
      if(trace->is_open()) {
	trace->close();
      }
      delete trace;
    }
  }
}

void TraceWorkload::reset()
{
  Workload::reset();

  _counts.assign(_nodes, 0);

  // get first packet for each node
  for(int n = 0; n < _nodes; ++n) {
    ifstream * trace = _traces[n];
    trace->seekg(0);
    int & count = _counts[n];
    int const skip = _skips[n];
    int const scale = _scales[n];
    int time = 0;
    while(((_limit < 0) || (count < _limit)) && !trace->eof()) {
      ++count;
      int delay, source, dest, type;
      *trace >> delay >> source >> dest >> type;
      if(count > skip) {
	time += delay;
	if((source == n) && (type >= 0)) {
	  PacketInfo pi;
	  pi.time = time;
	  pi.source = source;
	  pi.dest = dest;
	  pi.size = _packet_sizes[type];
	  assert(pi.size > 0);
	  if(((scale > 0) ? (time / scale) : (time * -scale)) > 0) {
	    _waiting_packets.push_back(pi);
	  } else {
	    _ready_packets.push_back(pi);
	  }
	  break;
	}
      }
    }
  }
  _ready_iter = _ready_packets.begin();
}

void TraceWorkload::advanceTime()
{
  Workload::advanceTime();

  // promote from waiting to ready
  list<PacketInfo>::iterator iter = _waiting_packets.begin();
  while(iter != _waiting_packets.end()) {
    int time = iter->time;
    int scale = _scales[iter->source];
    if(((scale > 0) ? (time / scale) : (time * -scale)) <= _time) {
      list<PacketInfo>::iterator source_iter = iter;
      ++iter;
      _ready_packets.splice(_ready_packets.end(), _waiting_packets, source_iter);
    } else {
      ++iter;
    }
  }

  assert(_ready_iter == _ready_packets.end());
  _ready_iter = _ready_packets.begin();
}

bool TraceWorkload::empty() const
{
  return (_ready_iter == _ready_packets.end());
}

bool TraceWorkload::completed() const
{
  return (_waiting_packets.empty() && _ready_packets.empty());
}

int TraceWorkload::source() const
{
  assert(!empty());
  return _ready_iter->source;
}

int TraceWorkload::dest() const
{
  assert(!empty());
  return _ready_iter->dest;
}

int TraceWorkload::size() const
{
  assert(!empty());
  assert(_ready_iter->size > 0);
  return _ready_iter->size;
}

int TraceWorkload::time() const
{
  assert(!empty());
  int time = _ready_iter->time;
  int scale = _scales[_ready_iter->source];
  return (scale > 0) ? (time / scale) : (time * -scale);
}

void TraceWorkload::inject()
{
  assert(!empty());
  int const n = _ready_iter->source;
  int & count = _counts[n];
  int const scale = _scales[n];
  ifstream * trace = _traces[n];
  int time = _ready_iter->time;
  bool empty = true;
  while(((_limit < 0) || (count < _limit)) && !trace->eof()) {
    ++count;
    int delay, source, dest, type;
    *trace >> delay >> source >> dest >> type;
    time += delay;
    if((source == n) && (type >= 0)) {
      _ready_iter->time = time;
      assert(_ready_iter->source == source);
      _ready_iter->dest = dest;
      _ready_iter->size = _packet_sizes[type];
      assert(_ready_iter->size > 0);
      empty = false;
      break;
    }
  }
  if(empty) {
    _ready_iter = _ready_packets.erase(_ready_iter);
  } else if(((scale > 0) ? (time / scale) : (time * -scale)) > _time) {
    list<PacketInfo>::iterator source_iter = _ready_iter;
    ++_ready_iter;
    _waiting_packets.splice(_waiting_packets.end(), _ready_packets, source_iter);
  } else {
    ++_ready_iter;
  }
}

void TraceWorkload::defer()
{
  assert(!empty());
  ++_ready_iter;
}
