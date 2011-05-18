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

#include <iostream>
#include <cstdlib>

#include "workload.hpp"

Workload::Workload()
{
  
}

Workload::~Workload()
{
  
}

Workload * Workload::New(string const & workload, int nodes)
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
    result = new SyntheticWorkload(nodes, load, size, injection, traffic);
  } else if(workload_name == "trace") {
    if(params.size() < 2) {
      cout << "Error: Missing parameter in trace workload definition: "
	   << workload << endl;
      exit(-1);
    }
    string const & filename = params[0];
    vector<string> psize_str = tokenize(params[1]);
    vector<int> psize(psize_str.size());
    for(size_t i = 0; i < psize_str.size(); ++i) {
      psize[i] = atoi(psize_str[i].c_str());
    }
    result = new TraceWorkload(filename, psize);
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
				     string const & traffic)
  : _nodes(nodes), _size(size)
{
  _injection = InjectionProcess::New(injection, nodes, load);
  _traffic = TrafficPattern::New(traffic, nodes);
  _qtime.resize(nodes);
  reset();
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

TraceWorkload::TraceWorkload(string const & filename, 
			     vector<int> const & packet_size)
  : _packet_size(packet_size)
{
  _trace.open(filename.c_str());
}

TraceWorkload::~TraceWorkload()
{
  _trace.close();
}

void TraceWorkload::_readPackets()
{
  while(!_trace.eof()) {
    int delay, source, dest, type;
    _trace >> delay >> source >> dest >> type;
    if(type >= 0) {
      _next_packet.time = _time + delay;
      _next_packet.source = source;
      _next_packet.dest = dest;
      _next_packet.size = _packet_size[type];
      if(delay > 0) {
	break;
      }
      _queueNextPacket();
    }
  }
}

void TraceWorkload::_queueNextPacket()
{
  _pending_packets.push_back(_next_packet);
  _next_packet.time = -1;
}

void TraceWorkload::reset()
{
  Workload::reset();
  _trace.seekg(0);
  _next_packet.time = -1;
  _readPackets();
  _current_packet = _pending_packets.begin();
}

void TraceWorkload::advanceTime()
{
  Workload::advanceTime();
  if(_next_packet.time == _time) {
    _queueNextPacket();
    _readPackets();
    _current_packet = _pending_packets.begin();
  }
}

bool TraceWorkload::empty() const
{
  return (_current_packet == _pending_packets.end());
}

int TraceWorkload::source() const
{
  assert(!empty());
  return _current_packet->source;
}

int TraceWorkload::dest() const
{
  assert(!empty());
  return _current_packet->dest;
}

int TraceWorkload::size() const
{
  assert(!empty());
  return _current_packet->size;
}

int TraceWorkload::time() const
{
  assert(!empty());
  return _current_packet->time;
}

void TraceWorkload::inject()
{
  assert(!empty());
  _current_packet = _pending_packets.erase(_current_packet);
}

void TraceWorkload::defer()
{
  assert(!empty());
  ++_current_packet;
}
