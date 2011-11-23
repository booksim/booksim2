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

#ifndef _WORKLOAD_HPP_
#define _WORKLOAD_HPP_

#include <string>
#include <vector>
#include <queue>
#include <list>
#include <fstream>

#include "injection.hpp"
#include "traffic.hpp"

using namespace std;

class Workload {
protected:
  int const _nodes;
  int _time;
  Workload(int nodes);
public:
  virtual ~Workload();
  static Workload * New(string const & workload, int nodes, 
			Configuration const * const config = NULL);
  virtual void reset();
  virtual void advanceTime();
  virtual bool empty() const = 0;
  virtual bool completed() const = 0;
  virtual int source() const = 0;
  virtual int dest() const = 0;
  virtual int size() const = 0;
  virtual int time() const = 0;
  virtual void inject() = 0;
  virtual void defer() = 0;
};

class NullWorkload : public Workload {
public:
  NullWorkload(int nodes) : Workload(nodes) {}
  virtual bool empty() const {return true;}
  virtual bool completed() const {return true;}
  virtual int source() const {return -1;}
  virtual int dest() const {return -1;}
  virtual int size() const {return -1;}
  virtual int time() const {return -1;}
  virtual void inject() {}
  virtual void defer() {}
};

class SyntheticWorkload : public Workload {
protected:
  vector<int> _sizes;
  vector<int> _rates;
  int _max_val;
  InjectionProcess * _injection;
  TrafficPattern * _traffic;
  vector<int> _qtime;
  queue<int> _ready;
  queue<int> _pending;
  queue<int> _deferred;
public:
  SyntheticWorkload(int nodes, double load, string const & traffic, 
		    string const & injection, 
		    vector<int> const & sizes, 
		    vector<int> const & rates, 
		    Configuration const * const config = NULL);
  virtual ~SyntheticWorkload();
  virtual void reset();
  virtual void advanceTime();
  virtual bool empty() const;
  virtual bool completed() const;
  virtual int source() const;
  virtual int dest() const;
  virtual int size() const;
  virtual int time() const;
  virtual void inject();
  virtual void defer();
};

class TraceWorkload : public Workload {

protected:

  vector<int> _packet_sizes;

  struct PacketInfo {
    int time;
    int source;
    int dest;
    int size;
  };

  list<PacketInfo>::iterator _ready_iter;
  list<PacketInfo> _waiting_packets;
  list<PacketInfo> _ready_packets;

  vector<ifstream *> _traces;
  
  vector<int> _counts;
  int _limit;

  vector<int> _scales;
  vector<int> _skips;

public:
  
  TraceWorkload(int nodes, vector<string> const & filenames,
		vector<int> const & packet_size, int limit = -1,
		vector<int> const & skips = vector<int>(),
		vector<int> const & scales = vector<int>());
  
  virtual ~TraceWorkload();
  virtual void reset();
  virtual void advanceTime();
  virtual bool empty() const;
  virtual bool completed() const;
  virtual int source() const;
  virtual int dest() const;
  virtual int size() const;
  virtual int time() const;
  virtual void inject();
  virtual void defer();
  
};

#endif
