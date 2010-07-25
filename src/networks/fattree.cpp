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
//  $Id$
// 
////////////////////////////////////////////////////////////////////////


#include "booksim.hpp"
#include <vector>
#include <sstream>
#include <cmath>

#include "fattree.hpp"
#include "misc_utils.hpp"




FatTree::FatTree( const Configuration& config,const string & name )
  : Network( config ,name)
{
  

  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );

}

void FatTree::_ComputeSize( const Configuration& config )
{

  _k = config.GetInt( "k" );
  _n = config.GetInt( "n" );
  yrouter = config.GetInt( "yr" );
  xrouter = config.GetInt( "xr" );

  /*in case that we are using fattree other than 64 nodes*/
  //latency_correction = (double)_k/4;
 
   
  gK = _k; gN = _n;

  
  
  _sources = powi( _k, _n );
  _dests   = powi( _k, _n );

  _size = _n * powi( _k , _n - 1 );
  _channels = 2 * 2 * _k * powi( _k , _n-1 ); 
  

}


void FatTree::RegisterRoutingFunctions() {

}

void FatTree::_BuildNet( const Configuration& config )
{
  // Number of router positions at each depth of the network
  const int nPos = powi( _k, _n-1);

  //
  // Allocate Routers
  //
  ostringstream name;
  int d, pos, id, degree, port;
  for ( d = 0 ; d < _n ; ++d ) {
    for ( pos = 0 ; pos < nPos ; ++pos ) {
      
      if ( d == 0 )
	degree = _k;
      else
	degree = 2 * _k;

      id = d * nPos + pos;

      name.str("");
      name << "router_" << d << "_" << pos;
      _Router( d, pos ) = Router::NewRouter( config, this,
					     name.str( ), id,
					     degree, degree );
    }
  }

  //
  // Connect Channels to Routers
  //
  _AllocateChannelMap( );

  //
  // Connection Rule: Output Ports 0:3 Move DOWN Network
  //                  Output Ports 4:7 Move UP Network
  //
  
  // Injection & Ejection Channels
  //  Half of these channels pass over a tile ... the latency
  //  needs to be accounted for to correctly estimate buffer
  //  placement and power consumption
  //
  
  int _cY =  yrouter;
  int _cX =  xrouter;

  for ( pos = 0 ; pos < nPos ; ++pos ) {

    //the same as the cmesh networks
    int y_index = pos/_k;
    int x_index = pos%_k;
    for (int y = 0; y < _cY ; y++) {
      for (int x = 0; x < _cX ; x++) {
	
	int link = (_k * _cX) * (_cY * y_index + y) + (_cX * x_index + x) ;

	_Router( _n-1, pos)->AddInputChannel( _inject[link],
					      _inject_cred[link]);
	if (0 == pos % 2)
	  _inject[link]->SetLatency( 1 );
	else
	  _inject[link]->SetLatency( 1 );
      
	_Router( _n-1, pos)->AddOutputChannel( _eject[link],
					       _eject_cred[link]);
	if (0 == pos % 2)
	  _eject[link]->SetLatency( 1 );
	else 
	  _eject[link]->SetLatency( 1 );
      }
    }
  }

  // Connections between d = 0 and d = 1 Levels
  int c= 0;
  
  for ( pos = 0; pos < nPos; ++pos ) {
    for ( port = 0; port < _k; ++port ) {

      int parentPos  = pos;
      int parentPort = port;
      int childPos   = _k * port + pos % _k;
      int childPort  = _k + pos / _k;

      int latency = _WireLatency( 0, parentPos, 1, childPos );

      _ConnectRouterOutput( 0, parentPos, parentPort, c, latency );
      _ConnectRouterInput(  1, childPos,  childPort , c, latency );

      c++;

      _ConnectRouterInput(  0, parentPos, parentPort, c, latency );
      _ConnectRouterOutput( 1, childPos,  childPort,  c, latency );

      c++;

    }
  }

  // Connections between d = 1 and d = 2 Levels
  for ( pos = 0; pos < nPos ; ++pos ) {
    for ( port = 0; port < _k ; ++port ) {
      int parentPos  = pos;
      int parentPort = port;
      int childPos   = _k * (pos/_k) + port;
      int childPort  = _k + pos % _k;

      int latency = _WireLatency( 1, parentPos, 2, childPos );

      _ConnectRouterOutput( 1, parentPos, parentPort, c, latency );
      _ConnectRouterInput(  2, childPos,  childPort,  c, latency );

      c++;

      _ConnectRouterInput(  1, parentPos, parentPort, c, latency );
      _ConnectRouterOutput( 2, childPos,  childPort,  c, latency );

      c++;
      
    }
  }

  _FinalizeConnections( );
  _ReleaseChannelMap( );

}

Router*& FatTree::_Router( int depth, int pos ) 
{
  assert( depth < _n && pos < powi( _k, _n-1) );
  return _routers[depth * powi( _k, _n-1) + pos];
}

void FatTree::_AllocateChannelMap( ) 
{
  _mapSize =  powi( _k, _n ) * (2 * _k);

  _inputChannelMap  = new int [_mapSize];
  _outputChannelMap = new int [_mapSize];
  _latencyMap       = new int [_channels];

  for (int i = 0; i < _mapSize; ++i) {
    _inputChannelMap[i]  = -1;
    _outputChannelMap[i] = -1;
  }

  for (int j = 0; j < _channels; ++j) {
    _latencyMap[j] = 0 ;
  }

}

void FatTree::_ReleaseChannelMap( )
{
  delete[] _inputChannelMap;
  delete[] _outputChannelMap;
  delete[] _latencyMap;
}

int FatTree::_PortIndex( int depth, int pos, int port )
{
  return ( depth * powi( _k, _n-1) + pos) * (2*_k) + port;
}

void FatTree::_ConnectRouterInput( int depth, int pos, int port, 
				   int channel, int latency )
{
  int pi = _PortIndex( depth, pos, port);
  assert( pi < _mapSize );
  assert( _inputChannelMap[pi] == -1 );
  _inputChannelMap[ pi ] = channel;

  assert( (_latencyMap[channel] < 0) || (_latencyMap[channel] - latency < 0.1) );
  _latencyMap[channel] = latency;
}

void FatTree::_ConnectRouterOutput( int depth, int pos, int port, 
				    int channel, int latency )
{
  int pi = _PortIndex( depth, pos, port );
  assert( pi < _mapSize );
  assert( _outputChannelMap[pi] == -1 );
  _outputChannelMap[ pi ] = channel;

  assert( (_latencyMap[channel] < 0) || (_latencyMap[channel] - latency < 0.1) );
  _latencyMap[channel] = latency;
}

void FatTree::_FinalizeConnections( ) 
{

  //handle up to 1000
  int _parentDistance[3][100][10];
  int _portPreference[3][100][10];  
  for (int d = 0; d < _n; d++) {
    for (int p = 0; p < _k*_k; p++) {
      for (int c = 0; c < _k; c++ ) {
	_parentDistance[d][p][c] = 0;
	_portPreference[d][p][c] = 0;
      }
    }
  }
  for ( int c = 0; c < _channels; ++c ) {
    assert( _latencyMap[c] >= 0.0 );
    _chan[c]->SetLatency( _latencyMap[c] );
    _chan_cred[c]->SetLatency( _latencyMap[c] );
  }

  for ( int depth = 0; depth < _n; ++depth ) {
    for ( int pos = 0; pos < powi( _k, _n-1); ++pos ) {
      for ( int port = 0; port < (2*_k) ; ++port ) {
	
	int ic = _inputChannelMap[ _PortIndex( depth, pos, port ) ];

	if ( ic != -1 )
	  _Router( depth, pos)->AddInputChannel( _chan[ic],
						 _chan_cred[ic] );

	int oc = _outputChannelMap[ _PortIndex( depth, pos, port ) ];

	if ( oc != -1 )
	  _Router( depth, pos)->AddOutputChannel( _chan[oc],
						  _chan_cred[oc] );

	if ( port > _k ) {
	  _parentDistance[depth][pos][port-_k] = int( _chan[ic]->GetLatency() );
	}
      }
      
      
      // Sort the path latencys to the parent nodes
      for ( int i = 0; i < _k; i++ ) {
	int minp = 0;
	int mind = 100;
	for ( int p = 0; p < _k; p++ ) {
	  if ( _parentDistance[depth][pos][p] < mind ) {
	    minp = p;
	    mind = _parentDistance[depth][pos][p];
	  }
	}
	_portPreference[depth][pos][i] = minp;
	_parentDistance[depth][pos][minp] += 1000;
      }
    }
  }
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
  float  latency_correction = 1;
  // Distances between the Depth = 2 (Leaf) and Depth = 1 (Interior)
  // routers in the network based on the checkerboard floorplan.
  int _latency_d2_d1_0  = (int)(2 * latency_correction);
  int _latency_d2_d1_1  = (int)(2 * latency_correction);
  int _latency_d2_d1_2  = (int)(4 * latency_correction);
  int _latency_d2_d1_3  = (int)(4 * latency_correction);
  /*
  int _latency_d2_d1_0  = (int)(1 * latency_correction);
  int _latency_d2_d1_1  = (int)(1 * latency_correction);
  int _latency_d2_d1_2  = (int)(3 * latency_correction);
  int _latency_d2_d1_3  = (int)(3 * latency_correction);
  */

  // Distances between the Depth = 1 (Interior) and Depth = 0 (Root)
  // routers in the network based on the checkerboard floorplan.
    int _latency_d1_d0_0  = (int)(4 * latency_correction);
  int _latency_d1_d0_1  = (int)(4 * latency_correction);
  int _latency_d1_d0_2  = (int)(6 * latency_correction);
  int _latency_d1_d0_3  = (int)(6 * latency_correction);
  /*

  int _latency_d1_d0_0  = (int)(3 * latency_correction);
  int _latency_d1_d0_1  = (int)(3 * latency_correction);
  int _latency_d1_d0_2  = (int)(4 * latency_correction);
  int _latency_d1_d0_3  = (int)(4 * latency_correction);
  */
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

