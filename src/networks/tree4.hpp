// $Id$
////////////////////////////////////////////////////////////////////////
//
// Tree4: Network with 64 Terminal Nodes arranged in a tree topology
//        with 4 routers at the root of the tree
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/06/26 22:49:23 $
//  $Id$
// 
////////////////////////////////////////////////////////////////////////

#ifndef _TREE4_HPP_
#define _TREE4_HPP_
#include <assert.h>
#include "network.hpp"

class Tree4 : public Network {

  int _k;
  int _n;

  int *_speedup;

  int _channelWidth;

  void _ComputeSize( const Configuration& config );
  void _BuildNet( const Configuration& config );


  Router*& _Router( int height, int pos );

  int _WireLatency( int height1, int pos1, int height2, int pos2 );

public:

  Tree4( const Configuration& config );
  static void RegisterRoutingFunctions() ;
  
  static int HeightFromID( int id );
  static int PosFromID( int id );
  static int SpeedUp( int height );
};

#endif
