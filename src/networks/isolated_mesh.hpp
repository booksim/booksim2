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
// IsolatedMesh: Provides two independent physical networks for
//               transporting different message types.
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:14:07 $
//  $Id$
// 
////////////////////////////////////////////////////////////////////////
#ifndef _ISOLATED_MESH_HPP_
#define _ISOLATED_MESH_HPP_

#include "network.hpp"
#include "kncube.hpp"
#include <assert.h>

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

  IsolatedMesh( const Configuration &config, const string & name );
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
