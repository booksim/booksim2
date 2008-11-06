// ----------------------------------------------------------------------
//
//  RoundRobin: RoundRobin Arbiter
//
// ----------------------------------------------------------------------

#include "roundrobin_arb.hpp"
#include <iostream>
using namespace std ;

RoundRobinArbiter::RoundRobinArbiter( Module *parent, const string &name,
				      int size ) 
  : Arbiter( parent, name, size ), _pointer( 0 ) {
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
  
  // avoid running arbiter if it has not recevied at least two requests
  // (in this case, requests and grants are identical)
  if ( _num_reqs < 2 ) {
    
    _selected = _last_req ;
    
  } else {
    
    _selected = -1 ;
    
    // run the round-robin tournament
    for (int offset = 1 ; offset <= _input_size ; offset++ ) {
      int input = (_pointer + offset) % _input_size;
      if ( _request[input].valid ) {
	if ( ( _selected < 0 ) //||
	     /*( _request[_selected].pri < _request[index].pri )*/
	     )
	  _selected = input ;
      }
    }
  }
  
  if ( _selected > -1 ) {
    if ( id ) 
      *id = _request[_selected].id ;
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
