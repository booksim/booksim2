////////////////////////////////////////////////////////////////////////
//
// IsolatedMesh: Provides two independent physical networks for
//               transporting different message types.
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:14:07 $
//  $Id: isolated_mesh.hpp,v 1.1 2007/05/17 17:14:07 jbalfour Exp $
// 
////////////////////////////////////////////////////////////////////////
#ifndef _ISOLATED_MESH_HPP_
#define _ISOLATED_MESH_HPP_

#include "network.hpp"
#include "kncube.hpp"

class IsolatedMesh : public Network {

  int _k;
  int _n;
  
  int* _f_read_history;
  int* _c_read_history;

  KNCube* _subMesh[2];
  int _subNetAssignment[Flit::NUM_FLIT_TYPES];

  void _ComputeSize(const Configuration& config );
  void _BuildNet(const Configuration& config );

public:

  IsolatedMesh( const Configuration &config );
  ~IsolatedMesh( );
  static void RegisterRoutingFunctions() ;

  void  WriteFlit( Flit *f, int source );
  Flit* ReadFlit( int dest );

  void    WriteCredit( Credit *c, int dest );
  Credit* ReadCredit( int source );
  
  void ReadInputs( );
  void InternalStep( );
  void WriteOutputs( );

  int GetN( ) const;
  int GetK( ) const;

};

#endif
