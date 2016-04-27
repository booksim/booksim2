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

#ifndef _WORKLOAD_HPP_
#define _WORKLOAD_HPP_

#include <string>
#include <vector>
#include <queue>
#include <map>
#include <list>
#include <fstream>

#include "injection.hpp"
#include "traffic.hpp"

extern "C" {
#include "netrace/netrace.h"
}

using namespace std;

class Workload {
protected:
  int const _nodes;
  queue<int> _pending_nodes;
  queue<int> _deferred_nodes;
  Workload(int nodes);
public:
  virtual ~Workload();
  static Workload * New(string const & workload, int nodes, 
			Configuration const * const config = NULL);
  virtual void reset();
  virtual void advanceTime();
  virtual bool empty() const;
  virtual bool completed() const = 0;
  virtual int source() const;
  virtual int dest() const = 0;
  virtual int size() const = 0;
  virtual int time() const = 0;
  virtual void inject(int pid) = 0;
  virtual void defer();
  virtual void retire(int pid) = 0;
  virtual void printStats(ostream & os) const;
};

class NullWorkload : public Workload {
public:
  NullWorkload(int nodes) : Workload(nodes) {}
  virtual bool completed() const {return true;}
  virtual int dest() const {return -1;}
  virtual int size() const {return -1;}
  virtual int time() const {return -1;}
  virtual void inject(int pid) {assert(false);}
  virtual void retire(int pid) {assert(false);}
};

class SyntheticWorkload : public Workload {
protected:
  unsigned int _time;
  vector<int> _sizes;
  vector<int> _rates;
  int _max_val;
  InjectionProcess * _injection;
  TrafficPattern * _traffic;
  vector<unsigned int> _qtime;
  queue<int> _sleeping_nodes;
public:
  SyntheticWorkload(int nodes, double load, string const & traffic, 
		    string const & injection, 
		    vector<int> const & sizes, 
		    vector<int> const & rates, 
		    Configuration const * const config = NULL);
  virtual ~SyntheticWorkload();
  virtual void reset();
  virtual void advanceTime();
  virtual bool completed() const;
  virtual int dest() const;
  virtual int size() const;
  virtual int time() const;
  virtual void inject(int pid);
  virtual void retire(int pid) {}
};

class TraceWorkload : public Workload {

protected:

  unsigned int _time;

  vector<int> _packet_sizes;

  struct PacketInfo {
    unsigned int time;
    int dest;
    int type;
  };

  int _next_source;
  PacketInfo _next_packet;
  vector<queue<PacketInfo> > _ready_packets;

  ifstream * _trace;
  
  unsigned int _count;
  int _limit;

  unsigned int _scale;
  unsigned int _skip;

  void _refill();

public:
  
  TraceWorkload(int nodes, string const & filename, 
		vector<int> const & packet_size, int limit = -1, 
		unsigned int skip = 0, unsigned int scale = 1);
  
  virtual ~TraceWorkload();
  virtual void reset();
  virtual void advanceTime();
  virtual bool completed() const;
  virtual int dest() const;
  virtual int size() const;
  virtual int time() const;
  virtual void inject(int pid);
  virtual void retire(int pid);
  virtual void printStats(ostream & os) const;
};

class NetraceWorkload : public Workload {

protected:

  unsigned long long int _time;

  nt_context_t * _ctx;

  nt_packet_t * _next_packet;

  vector<queue<nt_packet_t *> > _ready_packets;
  list<nt_packet_t *> _future_packets;
  set<unsigned int> _check_packets;
  map<unsigned int, nt_packet_t *> _stalled_packets;
  map<int, nt_packet_t *> _in_flight_packets;

  map<unsigned int, unsigned long long int> _response_eject_time;
  vector<unsigned long long int> _last_response_eject_time;

  unsigned int _channel_width;
  unsigned int _size_offset;

  unsigned int _region;

  unsigned int _window_size;

  unsigned long long int _count;
  unsigned long long int _limit;

  unsigned int _scale;

  unsigned long long int _skip;

  bool _enforce_deps;
  bool _enforce_lats;

  unsigned int _l2_tag_latency;
  unsigned int _l2_data_latency;
  unsigned int _mem_latency;
  unsigned int _trace_net_delay;

  void _refill();

public:
  
  NetraceWorkload(int nodes, string const & filename, 
		  unsigned int channel_width, long long int limit = -1ll, 
		  unsigned int scale = 1, int region = -1, 
		  bool enforce_deps = true, bool enforce_lats = false, 
		  unsigned int size_offset = 0);
  
  virtual ~NetraceWorkload();
  virtual void reset();
  virtual void advanceTime();
  virtual bool completed() const;
  virtual int dest() const;
  virtual int size() const;
  virtual int time() const;
  virtual void inject(int pid);
  virtual void retire(int pid);
  virtual void printStats(ostream & os) const;
};

#endif
