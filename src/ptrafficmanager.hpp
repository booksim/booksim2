// $Id: trafficmanager.hpp 1087 2009-02-10 23:53:08Z qtedq $

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

#ifndef _PTRAFFICMANAGER_HPP_
#define _PTRAFFICMANAGER_HPP_

#include <list>
#include<map>


#include "trafficmanager.hpp"
#include <assert.h>



class PTrafficManager : public TrafficManager {

protected:
  //each thread tracks their f->id individually 
  //in disjoint sets
  int  thread_fid;

  int *router_count;
  int node_count;
  int *router_list;
  int *node_list;

  // ============ Internal methods ============ 
protected:
  void _RetireFlitP( Flit *f, int dest , int t);

  Flit *_NewFlitP( int t);
  void _NormalInjectP(int t);
  void _StepP(int tid);


  void _GeneratePacketP( int source, int size, int cl, int time, int t);

  void  runthread(int tid);

  virtual int  _ComputeAccepted( double *avg, double *min ) const;

  virtual bool _SingleSim( );
  virtual void _FirstStep( );

  

public:
  PTrafficManager( const Configuration &config, Network **net );
  ~PTrafficManager( );


};

#endif
