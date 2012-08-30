// $Id: roundrobin_arb.cpp 4080 2011-10-22 23:11:32Z dub $

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


#include "large_roundrobin_arb.hpp"


LargeRoundRobinArbiter::LargeRoundRobinArbiter(const string &name,
				      int size ) 
  :_claimed(false), _pointer( 0 ),_size(size),  _highest_pri(numeric_limits<int>::min()), _best_input(-1), _num_reqs(0){
}

void LargeRoundRobinArbiter::PrintState() const  {
  cout << "Round Robin Priority Pointer: " << endl ;
  cout << "  _pointer = " << _pointer << endl ;
}

void LargeRoundRobinArbiter::UpdateState() {
  // update priority matrix using last grant
  if (_claimed) 
    _pointer = (_best_input + 1 ) % _size ;
}

void LargeRoundRobinArbiter::AddRequest( int input, int id, int pri )
{
  //only support a single addreq call from each input
  //else it will replase it?
  assert(  _best_input != input);
  if((_num_reqs == 0) || 
     Supersedes(input, pri, _best_input, _highest_pri, _pointer,_size )) {
    _highest_pri = pri;
    _best_input = input;
  }
  assert( 0 <= input && input < _size ) ;
  _num_reqs++ ;
}


void LargeRoundRobinArbiter::Clear()
{
  _highest_pri = numeric_limits<int>::min();
  _best_input = -1;
  if(_num_reqs > 0){
    _num_reqs = 0 ;
  }
  _claimed=false;
}
