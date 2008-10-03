// ----------------------------------------------------------------------
//
//  RoundRobin: RoundRobin Arbiter
//
// ----------------------------------------------------------------------

#include "roundrobin_arb.hpp"
#include <iostream>
using namespace std ;

RoundRobinArbiter::RoundRobinArbiter() 
  : Arbiter(), _pointer( 0 ) {
}

void RoundRobinArbiter::PrintState() const  {
  cout << "Round Robin Priority Pointer: " << endl ;
  cout << "  _pointer = " << _pointer << endl ;
}

void RoundRobinArbiter::UpdateState() {
  // update priority matrix using last grant
  if ( _selected > -1 ) 
    _pointer = _selected ;
}

int RoundRobinArbiter::Arbitrate( int* id, int* pri ) {
  
  _selected = -1 ;
  
  // avoid running arbiter if it has not recevied at least one request
  if ( _skip_arb == 1 ) 
    return _selected ;
  
  // run the round-robin tournament
  for (int offset = 1 ; offset <= _input_size ; offset++ ) {
    int input = (_pointer + offset) % _input_size;
    if ( _request[input].valid ) {
      _selected = input ;
      if ( id ) 
	*id = _request[input].id ;
      if ( pri ) 
	*pri = _request[input].pri ;
      break ;
    }
  }
  
  // clear the request vector
  if ( _selected != -1 ) {
    for ( int i = 0; i < _input_size ; i++ )
      _request[i].valid = false ;
    _skip_arb = 1 ;
  }
  
  return _selected ;
}
