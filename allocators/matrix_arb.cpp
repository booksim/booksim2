// ----------------------------------------------------------------------
//
//  Matrix: Matrix Arbiter
//
// ----------------------------------------------------------------------
#include "matrix_arb.hpp"
#include <iostream>
using namespace std ;

// ----------------------------------------------------------------------
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:10:51 $
//  $Id: matrix_arb.cpp,v 1.1 2007/05/17 17:10:51 jbalfour Exp $
// ----------------------------------------------------------------------

MatrixArbiter::MatrixArbiter() {
  _input_size = 0 ;
  _request    = 0 ;
  _matrix     = 0 ;
  _skip_arb   = 1 ;
}

MatrixArbiter::~MatrixArbiter() {
  if ( _request ) 
    delete[] _request ;
}

void MatrixArbiter::Init( int size ) {
  _input_size = size ;

  _request = new entry_t [size] ;
  for ( int i = 0 ; i < size ; i++ ) 
    _request[i].valid = false ;
  
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
  if ( row < column ) 
    _matrix[ row * _input_size + column ] = val ;
}

void MatrixArbiter::PrintMatrix() const  {
  cout << "Priority Matrix: " << endl ;
  for ( int r = 0; r < _input_size ; r++ ) {
    for ( int c = 0 ; c < _input_size ; c++ ) {
      cout << _Priority(r,c) << " " ;
    }
    cout << endl ;
  }
  cout << endl ;
}

void MatrixArbiter::AddRequest( int input, int id, int pri ) {
  assert( 0 <= input && input < _input_size ) ;
  _skip_arb = 0 ;
  _request[input].valid = true ;
  _request[input].id = id ;
  _request[input].pri = pri ;
  
}

void MatrixArbiter::UpdateMatrix() {
  // update priority matrix using last grant
  if ( _selected > -1 ) {
    for ( int i = 0; i < _input_size ; i++ ) {
      _SetPriority( _selected, i, 0 ) ;
    }
    
    for ( int i = 0 ; i < _input_size ; i++ ) {
      _SetPriority( i, _selected, 1 ) ;
    }
  }
}

int MatrixArbiter::Arbitrate( int* id, int* pri ) {
  
  _selected = -1 ;
  
  // avoid running arbiter if it has not recevied at least one request
  if ( _skip_arb == 1 ) 
    return _selected ;
  

  for ( int input = 0 ; input < _input_size ; input++ ) {
    
    bool grant = _request[input].valid ;
    for ( int i = 0 ; i < _input_size ; i++ ) {
      if ( _request[i].valid && _Priority(i,input) ) {
	grant = false ;
	break ;
      }
    }
    
    if ( grant ) {
      _selected = input ;
      if ( id )
	*id  = _request[_selected].id ;
      if ( pri )
	*pri = _request[_selected].pri ;
      break ;
    }
    
  }
  
  // clear the request vector
  for ( int i = 0; i < _input_size ; i++ )
    _request[i].valid = false ;
  _skip_arb = 1 ;
  
  return _selected ;
}
