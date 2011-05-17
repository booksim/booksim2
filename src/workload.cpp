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
  }
  return result;
}

void Workload::reset()
{

}

SyntheticWorkload::SyntheticWorkload(int nodes, double load, int size, string injection, string traffic)
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
  _time = 0;
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
  ++_time;
  while(!_deferred.empty()) {
    int const & source = _deferred.front();
    _pending.push(source);
    _deferred.pop();
  }
  for(int i = 0; i < _ready.size(); ++i) {
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
  assert(!_pending.empty());
  int const & source = _pending.front();
  return source;
}

int SyntheticWorkload::dest() const
{
  assert(!_pending.empty());
  int const & source = _pending.front();
  return _traffic->dest(source);
}

int SyntheticWorkload::size() const
{
  assert(!_pending.empty());
  return _size;
}

int SyntheticWorkload::time() const
{
  assert(!_pending.empty());
  int const & source = _pending.front();
  return _qtime[source];
}

void SyntheticWorkload::inject()
{
  assert(!_pending.empty());
  int const & source = _pending.front();
  _ready.push(source);
  _pending.pop();
}

void SyntheticWorkload::defer()
{
  assert(!_pending.empty());
  int const & source = _pending.front();
  _deferred.push(source);
  _pending.pop();
}
