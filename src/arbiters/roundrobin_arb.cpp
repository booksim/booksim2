// $Id: roundrobin_arb.cpp 938 2008-12-12 03:06:32Z dub $

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this 
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

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
