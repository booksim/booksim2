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

#include "booksim.hpp"
#include <iostream>
#include <assert.h>
#include "allocator.hpp"

/////////////////////////////////////////////////////////////////////////
//Allocator types
#include "maxsize.hpp"
#include "pim.hpp"
#include "islip.hpp"
#include "loa.hpp"
#include "wavefront.hpp"
#include "selalloc.hpp"
#include "separable_input_first.hpp"
#include "separable_output_first.hpp"
//
/////////////////////////////////////////////////////////////////////////

//==================================================
// Allocator base class
//==================================================

Allocator::Allocator( Module *parent, const string& name,
		      int inputs, int outputs ) :
  Module( parent, name ), _inputs( inputs ), _outputs( outputs )
{
 
  _inmatch  = new int [_inputs];   
  _outmatch = new int [_outputs];
  _outmask  = new int [_outputs];

  for ( int out = 0; out < _outputs; ++out ) {
    _outmask[out] = 0; // active
  }
}

Allocator::~Allocator( )
{
  delete [] _inmatch;
  delete [] _outmatch;
  delete [] _outmask;
}

void Allocator::_ClearMatching( )
{
  for ( int i = 0; i < _inputs; ++i ) {
    _inmatch[i] = -1;
  }

  for ( int j = 0; j < _outputs; ++j ) {
    _outmatch[j] = -1;
  }
}

int Allocator::OutputAssigned( int in ) const
{
  assert( ( in >= 0 ) && ( in < _inputs ) );

  return _inmatch[in];
}

int Allocator::InputAssigned( int out ) const
{
  assert( ( out >= 0 ) && ( out < _outputs ) );

  return _outmatch[out];
}

void Allocator::MaskOutput( int out, int mask )
{
  assert( ( out >= 0 ) && ( out < _outputs ) );
  _outmask[out] = mask;
}

//==================================================
// DenseAllocator
//==================================================

DenseAllocator::DenseAllocator( Module *parent, const string& name,
				int inputs, int outputs ) :
  Allocator( parent, name, inputs, outputs )
{
  _request  = new sRequest * [_inputs];

  for ( int i = 0; i < _inputs; ++i ) {
    _request[i]  = new sRequest [_outputs];  
  }

  Clear( );
}

DenseAllocator::~DenseAllocator( )
{  
  for ( int i = 0; i < _inputs; ++i ) {
    delete [] _request[i];
  }
  
  delete [] _request;
}

void DenseAllocator::Clear( )
{
  for ( int i = 0; i < _inputs; ++i ) {
    for ( int j = 0; j < _outputs; ++j ) {
      _request[i][j].label = -1;
    }
  }
}

int DenseAllocator::ReadRequest( int in, int out ) const
{
  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  return _request[in][out].label;
}

bool DenseAllocator::ReadRequest( sRequest &req, int in, int out ) const
{
  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  req = _request[in][out];

  return ( req.label != -1 );
}

void DenseAllocator::AddRequest( int in, int out, int label, 
				 int in_pri, int out_pri )
{
  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );
  
  if((_request[in][out].label == -1) || (_request[in][out].in_pri < in_pri)) {
    _request[in][out].label   = label;
    _request[in][out].in_pri  = in_pri;
    _request[in][out].out_pri = out_pri;
  }
}

void DenseAllocator::RemoveRequest( int in, int out, int label )
{
  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) ); 
  
  _request[in][out].label = -1;
}

void DenseAllocator::PrintRequests( ostream * os ) const
{
  if(!os) os = &cout;
  *os << "Requests = [ ";
  for ( int i = 0; i < _inputs; ++i ) {
    *os << "[ ";
    for ( int j = 0; j < _outputs; ++j ) {
      *os << ( _request[i][j].label != -1 ) << " ";
    }
    *os << "] ";
  }
  *os << "]." << endl;
}

//==================================================
// SparseAllocator
//==================================================

SparseAllocator::SparseAllocator( Module *parent, const string& name,
				  int inputs, int outputs ) :
  Allocator( parent, name, inputs, outputs )
{
  _in_req =  new list<sRequest> [_inputs];
  _out_req = new list<sRequest> [_outputs];
}


SparseAllocator::~SparseAllocator( )
{
  delete [] _in_req;
  delete [] _out_req;
}

void SparseAllocator::Clear( )
{
  for ( int i = 0; i < _inputs; ++i ) {
    _in_req[i].clear( );
  }

  for ( int j = 0; j < _outputs; ++j ) {
    _out_req[j].clear( );
  }

  _in_occ.clear( );
  _out_occ.clear( );
}

int SparseAllocator::ReadRequest( int in, int out ) const
{
  sRequest r;

  if ( ! ReadRequest( r, in, out ) ) {
    r.label = -1;
  } 

  return r.label;
}

bool SparseAllocator::ReadRequest( sRequest &req, int in, int out ) const
{
  bool found;

  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  list<sRequest>::const_iterator match;

  match = _in_req[in].begin( );
  while( ( match != _in_req[in].end( ) ) &&
	 ( match->port != out ) ) {
    match++;
  }

  if ( match != _in_req[in].end( ) ) {
    req = *match;
    found = true;
  } else {
    found = false;
  }

  return found;
}

void SparseAllocator::AddRequest( int in, int out, int label, 
				  int in_pri, int out_pri )
{
  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  list<sRequest>::iterator insert_point;
  list<int>::iterator occ_insert;
  sRequest req;

  // insert into occupied inputs list if
  // input is currently empty
  if ( _in_req[in].empty( ) ) {
    occ_insert = _in_occ.begin( );
    while( ( occ_insert != _in_occ.end( ) ) &&
	   ( *occ_insert < in ) ) {
      occ_insert++;
    }
    assert( ( occ_insert == _in_occ.end( ) ) || 
	    ( *occ_insert != in ) );

    _in_occ.insert( occ_insert, in );
  }

  // similarly for the output
  if ( _out_req[out].empty( ) ) {
    occ_insert = _out_occ.begin( );
    while( ( occ_insert != _out_occ.end( ) ) &&
	   ( *occ_insert < out ) ) {
      occ_insert++;
    }
    assert( ( occ_insert == _out_occ.end( ) ) || 
	    ( *occ_insert != out ) );

    _out_occ.insert( occ_insert, out );
  }

  // insert input request in order of it's output
  insert_point = _in_req[in].begin( );
  while( ( insert_point != _in_req[in].end( ) ) &&
	 ( insert_point->port < out ) ) {
    insert_point++;
  }

  req.port    = out;
  req.label   = label;
  req.in_pri  = in_pri;
  req.out_pri = out_pri;

  bool del = false;
  bool add = true;

  // For consistent behavior, delete the existing request
  // if it is for the same output and has a higher
  // priority

  if ( ( insert_point != _in_req[in].end( ) ) &&
       ( insert_point->port == out ) ) {
    if ( insert_point->in_pri < in_pri ) {
      del = true;
    } else {
      add = false;
    }
  }

  if ( add ) {
    _in_req[in].insert( insert_point, req );
  }

  if ( del ) {
    _in_req[in].erase( insert_point );
  }

  insert_point = _out_req[out].begin( );
  while( ( insert_point != _out_req[out].end( ) ) &&
	 ( insert_point->port < in ) ) {
    insert_point++;
  }

  req.port  = in;
  req.label = label;

  if ( add ) {
    _out_req[out].insert( insert_point, req );
  }

  if ( del ) {
    // This should be consistent, but check for sanity
    if ( ( insert_point == _out_req[out].end( ) ) ||
	 ( insert_point->port != in ) ) {
      Error( "Internal allocator error --- input and output requests non consistent" );
    }
    _out_req[out].erase( insert_point );
  }
}

void SparseAllocator::RemoveRequest( int in, int out, int label )
{
  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) ); 
  
  list<sRequest>::iterator erase_point;
  list<int>::iterator occ_remove;
				 
  // insert input request in order of it's output
  erase_point = _in_req[in].begin( );
  while( ( erase_point != _in_req[in].end( ) ) &&
	 ( erase_point->port != out ) ) {
    erase_point++;
  }

  assert( erase_point != _in_req[in].end( ) );
  _in_req[in].erase( erase_point );

  // remove from occupied inputs list if
  // input is now empty
  if ( _in_req[in].empty( ) ) {
    occ_remove = _in_occ.begin( );
    while( ( occ_remove != _in_occ.end( ) ) &&
	   ( *occ_remove != in ) ) {
      occ_remove++;
    }
    
    assert( occ_remove != _in_occ.end( ) );
    _in_occ.erase( occ_remove );
  }

  // similarly for the output
  erase_point = _out_req[out].begin( );
  while( ( erase_point != _out_req[out].end( ) ) &&
	 ( erase_point->port != in ) ) {
    erase_point++;
  }

  assert( erase_point != _out_req[out].end( ) );
  _out_req[out].erase( erase_point );

  if ( _out_req[out].empty( ) ) {
    occ_remove = _out_occ.begin( );
    while( ( occ_remove != _out_occ.end( ) ) &&
	   ( *occ_remove != out ) ) {
      occ_remove++;
    }

    assert( occ_remove != _out_occ.end( ) );
    _out_occ.erase( occ_remove );
  }
}

void SparseAllocator::PrintRequests( ostream * os ) const
{
  list<sRequest>::const_iterator iter;
  
  if(!os) os = &cout;
  
  *os << "Input requests = [ ";
  for ( int input = 0; input < _inputs; ++input ) {
    *os << input << " -> [ ";
    for ( iter = _in_req[input].begin( ); 
	  iter != _in_req[input].end( ); iter++ ) {
      *os << iter->port << " ";
    }
    *os << "]  ";
  }
  *os << "], output requests = [ ";
  for ( int output = 0; output < _outputs; ++output ) {
    *os << output << " -> ";
    if ( _outmask[output] == 0 ) {
      *os << "[ ";
      for ( iter = _out_req[output].begin( ); 
	    iter != _out_req[output].end( ); iter++ ) {
	*os << iter->port << " ";
      }
      *os << "]  ";
    } else {
      *os << "masked  ";
    }
    *os << "] ";
  }
  *os << "]." << endl;
}

//==================================================
// Global allocator allocation function
//==================================================

Allocator *Allocator::NewAllocator( Module *parent, const string& name,
				    const string &alloc_type, 
				    int inputs, int outputs,
				    int iters, const string &arb_type )
{
  Allocator *a = 0;
  
  if ( alloc_type == "max_size" ) {
    a = new MaxSizeMatch( parent, name, inputs, outputs );
  } else if ( alloc_type == "pim" ) {
    a = new PIM( parent, name, inputs, outputs, iters );
  } else if ( alloc_type == "islip" ) {
    a = new iSLIP_Sparse( parent, name, inputs, outputs, iters );
  } else if ( alloc_type == "loa" ) {
    a = new LOA( parent, name, inputs, outputs );
  } else if ( alloc_type == "wavefront" ) {
    a = new Wavefront( parent, name, inputs, outputs );
  } else if ( alloc_type == "select" ) {
    a = new SelAlloc( parent, name, inputs, outputs, iters );
  } else if (alloc_type == "separable_input_first") {
    a = new SeparableInputFirstAllocator( parent, name, inputs, outputs,
					  arb_type );
  } else if (alloc_type == "separable_output_first") {
    a = new SeparableOutputFirstAllocator( parent, name, inputs, outputs,
					   arb_type );
  }

//==================================================
// Insert new allocators here, add another else if 
//==================================================


  return a;
}

