// $Id: trafficmanager.hpp 2203 2010-07-02 00:04:02Z qtedq $

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

#ifndef _SSTRAFFICMANAGER_HPP_
#define _SSTRAFFICMANAGER_HPP_

#include "booksim.hpp"
#include "globals.hpp"
#include <list>
#include <map>
#include <set>
#include <vector>

#include "trafficmanager.hpp"
#include "config_utils.hpp"
#include "network.hpp"
#include "flit.hpp"
#include "buffer_state.hpp"
#include "stats.hpp"
#include "traffic.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "injection.hpp"
#include <assert.h>

#include "network/network.h"
#include "network/networkSim.h"
using namespace SS_Network;

class SSTrafficManager : public TrafficManager {
protected:

  int vc_classes;
  int *vc_ptrs;
protected:
  virtual void _RetireFlit( Flit *f, int dest );
  virtual void _GeneratePacket( int source, int size, int cl, int time );

  void SSInject();
public:
  SSTrafficManager(  const Configuration &config, const vector<Booksim_Network *> & net );
  ~SSTrafficManager( );
  


  virtual void printPartialStats(int t , int i);

  virtual void DisplayStats( ostream & os = cout ) ;
  void _Step(int time);
  
  inline int getNetworkTime(){
    return _network_time;
  }
  void registerNode(Interface* i, int id){
    assert(id<_nodes);
    assert(i);
    nodes[id] = i;
  }
private:
  int flit_size;
  int _network_time;
  vector< Interface* > nodes;
  
  map<int, SS_Network::Message::Ptr> packet_payload;
  int next_report;

  int channel_width;/*bits*/


private:
  Stats * packet_size_stat;
  vector<Stats*> type_pair_sent;

  ostream * _trace_out;

  vector<int> trace_queue;
};

#endif
