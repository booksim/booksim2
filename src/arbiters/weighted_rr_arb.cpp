// $Id: roundrobin_arb.cpp 3510 2011-05-09 22:07:44Z qtedq $

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
#include "weighted_rr_arb.hpp"
#include "stats.hpp"
#include <iostream>
#include <limits>

using namespace std ;

WeightedRRArbiter::WeightedRRArbiter( Module *parent, const string &name,
				      int size ) 
  : Arbiter( parent, name, size ), _pointer( 0 ) {
  _share.resize(size,0);
}

void WeightedRRArbiter::PrintState() const  {
  cout << "Weighted RR Priority Pointer: " << endl ;
  cout << "  _pointer = " << _pointer << endl ;
}

void WeightedRRArbiter::UpdateState() {

  //only move pointer when share is depleted
  if ( _selected > -1 ) {
    _share[_selected]--;
    assert(_share[_selected]>=0);
    if(_share[_selected]==0){
      _pointer = ( _selected + 1 ) % _size ;
    } else {
      _pointer =  _selected;
    }
  }
}

void WeightedRRArbiter::AddRequest( int input, int id, int pri )
{

  assert(pri>0);
  //update weight
  if(_share[input] == 0){
    _share[input] = pri;
  }
  
  if(!_request[input].valid || (_request[input].pri < pri)) {
    if(_num_reqs == 0) {
      _highest_pri = pri;
      _best_input = input;
      //round robin the pointers, but check for _share
    } else if(RoundRobinArbiter::Supersedes(input,1,_best_input, 1, _pointer,_size)){
      assert(_share[input]!=0);
      _highest_pri = pri;
      _best_input = input;   
    }
  }
  Arbiter::AddRequest(input, id, pri);
}

int WeightedRRArbiter::Arbitrate( int* id, int* pri ) {
  
  _selected = _best_input;
  
  return Arbiter::Arbitrate(id, pri);
}

void WeightedRRArbiter::Clear()
{
  _highest_pri = numeric_limits<int>::min();
  _best_input = -1;
  Arbiter::Clear();
}
