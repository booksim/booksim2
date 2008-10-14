// ----------------------------------------------------------------------
//
//  Arbiter: Base class for Matrix and Round Robin Arbiter
//
// ----------------------------------------------------------------------

#include "arbiter.hpp"

#include <assert.h>

using namespace std ;

Arbiter::Arbiter()
  : _input_size(0), _request(0), _num_reqs(0), _last_req(-1) {
}

Arbiter::~Arbiter() {
  if ( _request ) 
    delete[] _request ;
}

void Arbiter::Init( int size ) {
  _input_size = size;
  _request    = new entry_t [size] ;
  for ( int i = 0 ; i < size ; i++ ) 
    _request[i].valid = false ;
}

void Arbiter::AddRequest( int input, int id, int pri ) {
  assert( 0 <= input && input < _input_size ) ;
  if(!request[input].valid || (_request[input].pri < pri)) {
    _last_req = input ;
    if(!_request[input].valid) {
      _num_reqs++ ;
      _request[input].valid = true ;
    }
    _request[input].id = id ;
    _request[input].pri = pri ;
  }
}
