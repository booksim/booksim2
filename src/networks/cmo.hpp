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

#ifndef _CMO_HPP_
#define _CMO_HPP_

#include "network.hpp"
#include "routefunc.hpp"

class CMO : public Network {

  int _c;    
  // concentration degree --> must be perfectly divisible by this
  // only two slices
  // N = _c*8 * 2


  //   int _wire_delay1;
  //   int _wire_delay2;
  //   int _wire_delay3;
  //   int _wire_delay4;

  void _ComputeSize( const Configuration &config );
  void _BuildNet( const Configuration &config );

  int _LeftChannel( int node );
  int _RightChannel( int node );
  int _CrossChannel( int node );
  int _SliceChannel( int node );
  
  int _LeftNode( int node );
  int _RightNode( int node );
  int _CrossNode( int node );
  int _SliceNode( int node );
  
public:
  CMO( const Configuration &config, const string & name );

  int GetC( ) const;

  double Capacity( ) const;
  static void RegisterRoutingFunctions();
  void InsertRandomFaults( const Configuration &config );
  int MapNode(int physical_node) const;
  int UnmapNode(int physical_node) const;
};

void dim_order_cmo( const Router *r, const Flit *f, int in_channel,
		    OutputSet *outputs, bool inject );
void dor_next_cmo( int flitid, int cur, int dest, int in_port,
		   int *out_port, int *partition,
		   bool balance );
#endif
