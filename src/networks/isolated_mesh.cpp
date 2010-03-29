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
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:14:07 $
//  $Id$
// 
////////////////////////////////////////////////////////////////////////

#include "booksim.hpp"
#include "isolated_mesh.hpp"
#include "kncube.hpp"
#include "misc_utils.hpp"

IsolatedMesh::IsolatedMesh( const Configuration &config, const string & name ) 
: Network( config, name ) 
{

  _subMesh[0] = new KNCube( config, name, true );
  _subMesh[1] = new KNCube( config, name, true );

  int k = config.GetInt( "k");
  int n = config.GetInt( "n");
  _size = powi( k, n);
  _channels = 0;
  _sources = _size;
  _dests   = _size;

  _f_read_history = new int[_size];
  _c_read_history = new int[_size];

  for (int i = 0; i < _size; i++) {
    _f_read_history[i] = 0;
    _c_read_history[i] = 0;
  }

  _subNetAssignment[Flit::READ_REQUEST] 
    = config.GetInt( "read_request_subnet" );
  assert( _subNetAssignment[Flit::READ_REQUEST] == 0 ||
	  _subNetAssignment[Flit::READ_REQUEST] == 1);

  _subNetAssignment[Flit::READ_REPLY] 
    = config.GetInt( "read_reply_subnet" );
  assert( _subNetAssignment[Flit::READ_REPLY] == 0 ||
	  _subNetAssignment[Flit::READ_REPLY] == 1 );

  _subNetAssignment[Flit::WRITE_REQUEST] 
    = config.GetInt( "write_request_subnet" );
  assert( _subNetAssignment[Flit::WRITE_REQUEST] == 0 ||
	  _subNetAssignment[Flit::WRITE_REQUEST] == 1);

  _subNetAssignment[Flit::WRITE_REPLY]
    = config.GetInt( "write_reply_subnet" );
  assert( _subNetAssignment[Flit::WRITE_REPLY] == 0 ||
	  _subNetAssignment[Flit::WRITE_REPLY] == 1 );

}

IsolatedMesh::~IsolatedMesh( )
{
  if ( _subMesh[0] ) delete _subMesh[0];
  if ( _subMesh[1] ) delete _subMesh[1];
  if ( _f_read_history ) delete _f_read_history;
}

void IsolatedMesh::_ComputeSize(const Configuration & config ) 
{ }

void IsolatedMesh::_BuildNet(const Configuration & config )
{ }

void IsolatedMesh::RegisterRoutingFunctions(){

}

int IsolatedMesh::GetN( ) const 
{
  return _subMesh[0]->GetN();
}

int IsolatedMesh::GetK( ) const 
{
  return _subMesh[0]->GetK();
}

void IsolatedMesh::WriteFlit( Flit *f, int source ) 
{
  int subNet = 0;
  if ( f ) {
    subNet = _subNetAssignment[f->type];
    _subMesh[subNet]->WriteFlit( f, source );
    _subMesh[1-subNet]->WriteFlit( 0, source );
  } else {
    _subMesh[0]->WriteFlit( 0, source );
    _subMesh[1]->WriteFlit( 0, source );
  }
}

Flit* IsolatedMesh::ReadFlit( int dest ) 
{
  Flit* f = 0;

  if ( _f_read_history[dest] == 1 ) {
    f = _subMesh[0]->ReadFlit( dest );
    if ( f ) {
      _f_read_history[dest] = 0;
      if ( _subMesh[1]->PeekFlit( dest ) == 0 )
	_subMesh[1]->ReadFlit( dest );
      return f;
    } 
    f = _subMesh[1]->ReadFlit( dest );
    if ( f ) {
      _f_read_history[dest] = 1;
      return f;
    }
  } else {
    f = _subMesh[1]->ReadFlit( dest );
    if ( f ) {
      _f_read_history[dest] = 1;
      if ( _subMesh[0]->PeekFlit( dest ) == 0 )
	_subMesh[0]->ReadFlit( dest );
      return f;
    } 
    f = _subMesh[0]->ReadFlit( dest );
    if ( f ) {
      _f_read_history[dest] = 0;
      return f;
    } 
  }
  return f;
}

void IsolatedMesh::WriteCredit( Credit *c, int dest )
{
  // We rely on the traffic manager writing a credit
  //  for the most recently read flit
  if ( _f_read_history[dest] == 0 ) {
    _subMesh[0]->WriteCredit( c, dest );
    _subMesh[1]->WriteCredit( 0, dest );
  } else {
    _subMesh[0]->WriteCredit( 0, dest );
    _subMesh[1]->WriteCredit( c, dest );
  }
}

Credit* IsolatedMesh::ReadCredit( int source )
{
  Credit* c = 0;

  if ( _c_read_history[source] == 1 ) {
    c = _subMesh[0]->ReadCredit( source );
    if ( c ) {
      _c_read_history[source] = 0;
      if ( _subMesh[1]->PeekCredit( source) == 0 )
	_subMesh[1]->ReadCredit(source);
      return c;
    }
    c = _subMesh[1]->ReadCredit( source );
    if ( c ) {
      _c_read_history[source] = 1;
      return c;
    }
  } else {
    c = _subMesh[1]->ReadCredit( source );
    if ( c ) {
      _c_read_history[source] = 1;
      if ( _subMesh[0]->PeekCredit( source ) == 0 )
	_subMesh[0]->ReadCredit( source );
      return c;
    }
    c = _subMesh[0]->ReadCredit( source );
    if ( c ) {
      _c_read_history[source] = 0;
      return c;
    }
  }
  return c;
} 

void IsolatedMesh::ReadInputs( )
{
  _subMesh[0]->ReadInputs( );
  _subMesh[1]->ReadInputs( );
}

void IsolatedMesh::InternalStep( ) 
{
  _subMesh[0]->InternalStep( );
  _subMesh[1]->InternalStep( );
}

void IsolatedMesh::WriteOutputs( )
{
  _subMesh[0]->WriteOutputs( );
  _subMesh[1]->WriteOutputs( );
}
