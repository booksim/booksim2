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

// ----------------------------------------------------------------------
//
//  Matrix: Matrix Arbiter
//
// ----------------------------------------------------------------------

#include "matrix_arb.hpp"
#include <iostream>
using namespace std ;

MatrixArbiter::MatrixArbiter( Module *parent, const string &name, int size )
  : Arbiter( parent, name, size ) {
  _matrix  = new int [ size * size ] ;
  for ( int i = 0 ; i < size*size ; i++ )
    _matrix[i] = 0 ;
  for ( int i = 0 ; i < size ; i++ )
    _SetPriority( i, size-1, 1 ) ;
}

int MatrixArbiter::_Priority( int row, int column ) const  {
  if ( row <= column ) 
    return _matrix[ row * _input_size + column ] ;
  return 1 -_matrix[ column * _input_size + row ]  ;
}

void MatrixArbiter::_SetPriority( int row, int column, int val )  {
  if ( row != column ) 
    _matrix[ row * _input_size + column ] = val ;
}

void MatrixArbiter::PrintState() const  {
  cout << "Priority Matrix: " << endl ;
  for ( int r = 0; r < _input_size ; r++ ) {
    for ( int c = 0 ; c < _input_size ; c++ ) {
      cout << _Priority(r,c) << " " ;
    }
    cout << endl ;
  }
  cout << endl ;
}

void MatrixArbiter::UpdateState() {
  // update priority matrix using last grant
  if ( _selected > -1 ) {
    for ( int i = 0; i < _input_size ; i++ ) {
      _SetPriority( _selected, i, 0 ) ;
      _SetPriority( i, _selected, 1 ) ;
    }
  }
}

int MatrixArbiter::Arbitrate( int* id, int* pri ) {
  
  // avoid running arbiter if it has not recevied at least two requests
  // (in this case, requests and grants are identical)
  if ( _num_reqs < 2 ) {
    
    _selected = _last_req ;
    
  } else {
    
    _selected = -1 ;

    for ( int input = 0 ; input < _input_size ; input++ ) {
      if(_request[input].valid) {
	
	bool grant = true;
	for ( int i = 0 ; i < _input_size ; i++ ) {
	  if ( _request[i].valid &&
	       ( ( ( _request[i].pri == _request[input].pri ) &&
		   _Priority(i,input)) ||
		 ( _request[i].pri > _request[input].pri )
		 ) ) {
	    grant = false ;
	    break ;
	  }
	}
	
	if ( grant ) {
	  _selected = input ;
	  break ; 
	}
      }
      
    }
  }
    
  if ( _selected != -1 ) {
    if ( id )
      *id  = _request[_selected].id ;
    if ( pri )
      *pri = _request[_selected].pri ;

    // clear the request vector
    for ( int i = 0; i < _input_size ; i++ )
      _request[i].valid = false ;
    _num_reqs = 0 ;
    _last_req = -1 ;
  } else {
    assert(_num_reqs == 0);
    assert(_last_req == -1);
  }

  return _selected ;
}
