////////////////////////////////////////////////////////////////////////
//
// CMeshX2: Two Concentrated Meshes 
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/06/26 22:49:23 $
//  $Id: cmeshx2.hpp,v 1.2 2007/06/26 22:49:23 jbalfour Exp $
// 
////////////////////////////////////////////////////////////////////////
#ifndef _CMESHX2_HPP_
#define _CMESHX2_HPP_

#include "network.hpp"
#include "cmesh.hpp"

class CMeshX2 : public Network {

  int _k ;
  int _n ;
  int _c ;
  
  int* _f_read_history;
  int* _c_read_history;

  CMesh* _subMesh[2];
  int _subNetAssignment[Flit::NUM_FLIT_TYPES];

  void _ComputeSize(const Configuration& config );
  void _BuildNet(const Configuration& config );

public:

  CMeshX2( const Configuration &config );
  ~CMeshX2( );

  void  WriteFlit( Flit *f, int source );
  Flit* ReadFlit( int dest );

  void    WriteCredit( Credit *c, int dest );
  Credit* ReadCredit( int source );
  
  void ReadInputs( );
  void InternalStep( );
  void WriteOutputs( );

  int GetN( ) const;
  int GetK( ) const;

  static void RegisterRoutingFunctions() ;


};

#endif
