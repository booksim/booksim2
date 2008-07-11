// ----------------------------------------------------------------------
//
//  RoundRobin: RoundRobin Arbiter
//
// ----------------------------------------------------------------------
#include "roundrobin_arb.hpp"
#include <iostream>
using namespace std ;

// ----------------------------------------------------------------------
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:10:51 $
//  $Id: roundrobin_arb.cpp,v 1.1 2007/05/17 17:10:51 jbalfour Exp $
// ----------------------------------------------------------------------

RoundRobinArbiter::RoundRobinArbiter() {
  _input_size = 0 ;
  _request    = 0 ;
  _pointer    = 0 ;
  _skip_arb   = 1 ;
}

RoundRobinArbiter::~RoundRobinArbiter() {
  if ( _request ) 
    delete[] _request ;
}

void RoundRobinArbiter::Init( int size ) {
  _input_size = size ;

  _request = new entry_t [size] ;
  for ( int i = 0 ; i < size ; i++ ) 
    _request[i].valid = false ;
}

void RoundRobinArbiter::PrintPriority() const  {
  cout << "Round Robin Priority Pointer: " << endl ;
  cout << "  _pointer = " << _pointer << endl ;
}

void RoundRobinArbiter::AddRequest( int input, int id, int pri ) {
  assert( 0 <= input && input < _input_size ) ;
  _skip_arb = 0 ;
  _request[input].valid = true ;
  _request[input].id = id ;
  _request[input].pri = pri ;
  
}

void RoundRobinArbiter::UpdatePriority() {
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
  int input ;

  for (input = _pointer + 1 ; input < _input_size ; input++ ) {
    if ( _request[input].valid ) {
      _selected = input ;
      if ( id ) 
	*id = _request[input].id ;
      if ( pri ) 
	*pri = _request[input].pri ;
      break ;
    }
  }

  if ( _selected == -1 ) {
    for ( input = 0 ; input <= _pointer ; input++ ) {
      if ( _request[input].valid ) {
	_selected = input ;
	if ( id ) 
	  *id = _request[input].id ;
	if ( pri ) 
	  *pri = _request[input].pri ;
	break ;
      }
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
