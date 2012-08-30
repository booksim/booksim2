// $Id: fattree.cpp 938 2008-12-12 03:06:32Z dub $

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

////////////////////////////////////////////////////////////////////////
//
// FatTree
//
//       Each level of the hierarchical indirect Network has
//       k^(n-1) Routers. The Routers are organized such that 
//       each node has k ( = 4 ) descendents, and each parent is
//       replicated k ( = 4 ) times.
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/06/26 22:50:48 $
//  $Id: fattree.cpp 938 2008-12-12 03:06:32Z dub $
// 
////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include "booksim.hpp"
#include <vector>
#include <sstream>
#include <cmath>

#include "fattree.hpp"
#include "misc_utils.hpp"

#define EJECT_LATENCY INJECT_LATENCY
#define INJECT_LATENCY 1
#define CHANNEL_LATENCY 1

FatTree::FatTree( const Configuration& config )
  : Network( config )
{
  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}

FatTree::~FatTree ()
{
  for (short i = 0; i < _n; ++i)
    delete [] coords_to_router[i];
  delete [] coords_to_router;
}

void FatTree::RegisterRoutingFunctions() {

}


void FatTree::_ComputeSize( const Configuration& config )
{

  _k = config.GetInt( "k" ); // k is the "fatness" of the tree. n is the amount of levels.
  _n = config.GetInt( "n" );

  gK = _k; gN = _n;
  realgk = _k;
  realgn = _n;
  _sources = powi( _k, _n );
  _dests   = powi( _k, _n );
  _size = 0;
  _channels = 0;
  int routers_there;
  int fatness_factor = 1;

  for (short depth = _n - 1; depth >= 0; --depth)
  {
    routers_there = powi(_k, depth); // The routers at the examined level;
    _size += routers_there;
    _channels += routers_there * (_k * fatness_factor + (fatness_factor * _k )); // Each router has _k+1 ports. _k downwards of one less fatness factor, and one upwards of that unit times _k.
    if (depth == 0 || depth == _n -1 ) // Root or lead nodes do not have connections to both sides.
      _channels -= routers_there * _k * fatness_factor;
    fatness_factor *= _k;
  }
}

void FatTree::_BuildNet( const Configuration& config )
{

  // Number of router positions at each depth of the network
  const int nPos_max = powi( _k, _n-1);
  ostringstream name; // Allocate routers.
  coords_to_router = new short *[_n];
  for (short i = 0; i < _n; ++i)
  {
    coords_to_router[i] = new short [powi(_k, i)];
  }
  int d, pos, id = 0, degree, port, nPos, fatness_factor = 1;
  for ( d = _n - 1 ; d >= 0 ; --d ) {
    nPos = powi(_k, d);
    if ( d == 0 )
      degree = _k * fatness_factor;
    else // Leaf nodes need the I/O ports for inject/eject.
      degree = fatness_factor * 2 * _k;
    fatness_factor *= _k;

    for ( pos = 0 ; pos < nPos ; ++pos ) {

      name.seekp( 0, ios::beg );
      name << "router_" << d << "_" << pos << " id " << id;
      coords_to_router[d][pos] = id;
      _Router( d, pos ) = Router::NewRouter( config, this,
					     name.str( ), id,
					     degree, degree );
      id++;
    }
  }


  //
  // Connect Channels to Routers
  //
  //_AllocateChannelMap( );

  //
  // Connection Rule: Output Ports 0:3 Move DOWN Network
  //                  Output Ports 4:7 Move UP Network
  //
  
  // Injection & Ejection Channels
  //  Half of these channels pass over a tile ... the latency
  //  needs to be accounted for to correctly estimate buffer
  //  placement and power consumption
  for ( pos = 0 ; pos < nPos_max ; ++pos ) {
    for ( port = 0 ; port < _k ; port++ ) {
     
      _Router( _n-1, pos)->AddInputChannel( &_inject[_k*pos+port],
					    &_inject_cred[_k*pos+port]);
      if (0 == pos % 2) {
	_inject[_k*pos+port].SetLatency( INJECT_LATENCY );
	_inject_cred[_k*pos+port].SetLatency( INJECT_LATENCY );
      }
      else {
	_inject[_k*pos+port].SetLatency( INJECT_LATENCY );
	_inject_cred[_k*pos+port].SetLatency( INJECT_LATENCY );
      }
      _Router( _n-1, pos)->AddOutputChannel( &_eject[_k*pos+port],
					     &_eject_cred[_k*pos+port]);
      if (0 == pos % 2) {
	_eject[_k*pos+port].SetLatency( EJECT_LATENCY );
	_eject_cred[_k*pos+port].SetLatency( EJECT_LATENCY );
      }
      else {
	_eject[_k*pos+port].SetLatency( EJECT_LATENCY );
	_eject_cred[_k*pos+port].SetLatency( EJECT_LATENCY );
      }
    }
  }


  int c= 0, childPos;
  fatness_factor = _k; // Because we do not start from the very bottom level.
  for (short depth = _n - 2; depth >= 0; --depth) // Connections between levels. The bottom level has their injection/ejection channels taken care of.
  {
    nPos = powi(_k, depth);
    for ( pos = 0; pos < nPos; ++pos ) {
      for ( port = 0; port < _k; ++port ) { // For each of the ports leading downwards. So for each child.
	int childPos = _k * pos + port;

        int latency = _WireLatency( depth, pos, depth+1, childPos );

	assert(latency!=0);
        //cout << "Connecting (0," << parentPos << ") and (1," 
        //     << childPos << ")" << endl;

	// Now we connect the node to the child below it. We create one channel per fatness factor.
	// We also connect an output from the child below to this router.
	for (int counter = 0; counter < fatness_factor; ++counter)
	{
	  _Router(depth, pos)->AddOutputChannel(&_chan[c], &_chan_cred[c]);
	  _Router(depth+1, childPos)->AddInputChannel(&_chan[c], &_chan_cred[c]);
	  if(_use_noc_latency){
	    _chan[c].SetLatency(latency);
	    _chan_cred[c].SetLatency(latency);
	  } else {
	    _chan[c].SetLatency(CHANNEL_LATENCY);
	    _chan_cred[c].SetLatency(CHANNEL_LATENCY);
	  }
	  c++;
	  _Router(depth+1, childPos)->AddOutputChannel(&_chan[c], &_chan_cred[c]);
          _Router(depth, pos)->AddInputChannel(&_chan[c], &_chan_cred[c]);
	  if(_use_noc_latency){
 	    _chan[c].SetLatency(latency);
	    _chan_cred[c].SetLatency(latency);
	  } else {
	    _chan[c].SetLatency(CHANNEL_LATENCY);
	    _chan_cred[c].SetLatency(CHANNEL_LATENCY);
	  }
	  c++;
	}

      }
    }
    fatness_factor *= _k;
  }

  //cout << "Used " << c << " of " << _channels << " channels." << endl;

  //_FinalizeConnections( );
  //_ReleaseChannelMap( );

}

Router*& FatTree::_Router( int depth, int pos ) 
{
  assert( depth < _n && pos < powi(_k, depth) );
  return _routers[coords_to_router[depth][pos]];
  //return _routers[powi( _k, depth) + pos];
}

int FatTree::_WireLatency( int depth1, int pos1, int depth2, int pos2 )
{

  int depthChild, depthParent, posChild, posParent;

  if (depth1 < depth2) {
    depthChild  = depth2;
    posChild    = pos2;
    depthParent = depth1;
    posParent   = pos1;
  } else {
    depthChild  = depth1;
    posChild    = pos1;
    depthParent = depth2;
    posParent   = pos2;
  }

  // Distances between the Depth = 2 (Leaf) and Depth = 1 (Interior)
  // routers in the network based on the checkerboard floorplan.
  int _latency_d2_d1_0  = 2 ;
  int _latency_d2_d1_1  = 2 ;
  int _latency_d2_d1_2  = 4 ;
  int _latency_d2_d1_3  = 4 ;
 
  // Distances between the Depth = 1 (Interior) and Depth = 0 (Root)
  // routers in the network based on the checkerboard floorplan.
  int _latency_d1_d0_0  = 4 ;
  int _latency_d1_d0_1  = 4 ;
  int _latency_d1_d0_2  = 6 ;
  int _latency_d1_d0_3  = 6 ;
 
  assert( depthChild == depthParent+1 );
  assert( depthChild != 3 );
     
  if (depthChild == 2) {
    // The latency is determined by the relative position of
    //  the Parent and Child nodes within their rows. The latency
    //  component contributed by this distance is the same at both
    //  stages of the networks
    if ( posParent % 4 == posChild % 4 )
      return _latency_d2_d1_0;
    else if ( posParent % 4 == (posChild+1) % 4)
      return _latency_d2_d1_1;
    else if ( posParent % 4 == (posChild+2) % 4)
      return _latency_d2_d1_2;
    else
      return _latency_d2_d1_3;
  }
  
  if (depthChild == 1) {

    if ( posParent / 4 == posChild / 4 )
      return _latency_d1_d0_0;
    else if ( posParent / 4 == (posChild+1) / 4)
      return _latency_d1_d0_1;
    else if ( posParent / 4 == (posChild+2) / 4)
      return _latency_d1_d0_2;
    else
      return _latency_d1_d0_3;
  }

  return 0 ;
}

