////////////////////////////////////////////////////////////////////////
//
// QTree: A Quad-Tree Indirect Network.
//
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:14:07 $
//  $Id: qtree.hpp,v 1.1 2007/05/17 17:14:07 jbalfour Exp $
// 
////////////////////////////////////////////////////////////////////////

#ifndef _QTREE_HPP_
#define _QTREE_HPP_
#include <assert.h>
#include "network.hpp"

class QTree : public Network {

  int _k;
  int _n;

  void _ComputeSize( const Configuration& config );
  void _BuildNet( const Configuration& config );

  int _RouterIndex( int height, int pos );
  int _InputIndex( int height, int pos, int port );
  int _OutputIndex( int height, int pos, int port );

public:

  QTree( const Configuration& config );
  static void RegisterRoutingFunctions() ;

  static int HeightFromID( int id );
  static int PosFromID( int id );

};

#endif 
