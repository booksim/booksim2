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
	       ( _Priority(i,input) //||
		 /*( _request[i].pri > _request[index].pri )*/
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
