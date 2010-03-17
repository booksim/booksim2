// $Id$

/*
Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
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

#ifndef _ANYNET_HPP_
#define _ANYNET_HPP_

#include "network.hpp"
#include "routefunc.hpp"
#include <assert.h>
#include <string>
#include <map>
#include <list>

class AnyNet : public Network {

  string file_name;
  //associtation between  nodes and routers
  map<int, int > node_list;
  map<int,  map<int, int>* >* router_list;
  //stores minimal routing information from every router to every node
  map<int, int>* routing_table;

  void _ComputeSize( const Configuration &config );
  void _BuildNet( const Configuration &config );
  void readFile();
  void buildRoutingTable();
  int findPath(int router, int dest, int* hop_count,map<int, bool>* visited); 

public:
  AnyNet( const Configuration &config, const string & name );

  int GetN( ) const{ return -1;}
  int GetK( ) const{ return -1;}

  static void RegisterRoutingFunctions();
  double Capacity( ) const {return -1;}
  void InsertRandomFaults( const Configuration &config ){}
};

void min_anynet( const Router *r, const Flit *f, int in_channel, 
		      OutputSet *outputs, bool inject );
#endif
