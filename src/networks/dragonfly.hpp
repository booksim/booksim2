/*
 Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
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
                                                                     
                                                                     
                                             
#ifndef _DragonFly_HPP_
#define _DragonFly_HPP_

#include "network.hpp"
#include "routefunc.hpp"

class DragonFlyNew : public Network {
///TODO: remove repeated members
  int _m;
  int _n;
  int _r;
  int _k;
  int _p, _a, _g;
  int _radix;
  int _net_size;
  int _stageout;
  int _numinput;
  int _stages;
  int _num_of_switch;
  int _grp_num_routers;
  int _grp_num_nodes;


  void _ComputeSize( const Configuration &config );
  void _BuildNet( const Configuration &config );


 
public:
  DragonFlyNew( const Configuration &config, const string & name, Module * clock, CreditBox *credits );

  /// Why are these not inline and const?
  int GetN( ) const;
  int GetK( ) const;

  int GetP( ) const {return _p;}
  int GetA( ) const {return _a;}
  int GetG( ) const {return _g;}

  double Capacity( ) const;
  static void RegisterRoutingFunctions();
  void InsertRandomFaults( const Configuration &config );

};
int dragonfly_port(int rID, int source, int dest, int routers, int ports);

void ugal_dragonflynew( const Router *r, const Flit *f, int in_channel,
		       OutputSet *outputs, bool inject, RoutingConfig *rc );
void min_dragonflynew( const Router *r, const Flit *f, int in_channel, 
		       OutputSet *outputs, bool inject, RoutingConfig *rc );

#endif 
