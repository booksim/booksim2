////////////////////////////////////////////////////////////////////////
//
//  FatTree
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/06/26 22:49:23 $
//  $Id$
// 
////////////////////////////////////////////////////////////////////////

#ifndef _FatTree_HPP_
#define _FatTree_HPP_

#include "network.hpp"

class FatTree : public Network {

  int _k;
  int _n;

  int _layout;
  int _channelWidth;

  double latency_correction;
  
  void _ComputeSize( const Configuration& config );
  void _BuildNet(    const Configuration& config );

  Router*& _Router( int depth, int pos );

  int  _mapSize;
  int* _inputChannelMap;
  int* _outputChannelMap; 
  int* _latencyMap;

  void _ConnectRouterInput( int depth, int pos, int port, int channel,
			    int latency);

  void _ConnectRouterOutput( int depth, int pos, int port, int channel,
			     int latency );

  void _FinalizeConnections( );
  void _AllocateChannelMap( );
  void _ReleaseChannelMap( );
  int  _PortIndex( int depth, int pos, int port );

  int _WireLatency( int depth1, int pos1, int depth1, int pos2 );

  // Parent Distance
  //   [Depth][Position][Port]


public:

  FatTree( const Configuration& config ,const string & name );
  static void RegisterRoutingFunctions() ;

  //
  // Methods to Assit Routing Functions
  //
  static int PreferedPort( const Router* r, int index );
			 
};

#endif
