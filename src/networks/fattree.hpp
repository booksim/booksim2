// $Id$
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

  void _ComputeSize( const Configuration& config );
  void _BuildNet(    const Configuration& config );

  Router*& _Router( int depth, int pos );

  int  _mapSize;
  int* _inputChannelMap;
  int* _outputChannelMap; 
  int* _latencyMap;
  short** coords_to_router;

  int _WireLatency( int depth1, int pos1, int depth2, int pos2 );

  // Parent Distance
  //   [Depth][Position][Port]
  //int _parentDistance[3][16][4];
  //int _portPreference[3][16][4];
  int ***_parentDistance, ***_portPreference;

public:

  static void RegisterRoutingFunctions ();
  FatTree( const Configuration& config );
  ~FatTree();

};

#endif
