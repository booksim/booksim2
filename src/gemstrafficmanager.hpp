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

#ifndef _GEMSTRAFFICMANAGER_HPP_
#define _GEMSTRAFFICMANAGER_HPP_

#include "Vector.h"
#include <list>
#include <map>
#include <set>

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

#include "MessageBuffer.h"

#include "Consumer.h"


#include "Global.h"
class InjectConsumer : public Consumer{
  
public:
  static bool trigger_wakeup;
  Consumer * parent;
  InjectConsumer(Consumer* p){
    parent = p;
  }
  ~InjectConsumer(){
    
  }
  void wakeup(){
    if(trigger_wakeup){
      g_eventQueue_ptr->scheduleEvent(parent, 1);
      InjectConsumer::trigger_wakeup = false;
    }
  }
  void print(ostream& out) const{
    
  }
};

class GEMSTrafficManager : public TrafficManager {
protected:
  Vector<Vector<MessageBuffer*> >* input_buffer;
  Vector<Vector<MessageBuffer*> >* output_buffer;

  int vc_classes;
  int *vc_ptrs;
protected:

  virtual void _RetireFlit( Flit *f, int dest );


  

  virtual void _GeneratePacket( int source, int size, int cl, int time );


  void GemsInject();
public:
  GEMSTrafficManager(  const Configuration &config, const vector<BSNetwork *> & net , int vcc);
  ~GEMSTrafficManager( );
  



  void DisplayStats();
  void _Step( );
  void RegisterMessageBuffers(  Vector<Vector<MessageBuffer*> >* in,   Vector<Vector<MessageBuffer*> >* out);
  
  inline int getNetworkTime(){
    return _network_time;
  }
private:
  int flit_size;
  int _network_time;
  int next_report;
};

#endif
