// $Id: roundrobin_arb.cpp 3510 2011-05-09 22:07:44Z qtedq $

/*
Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
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
				      int size , bool imp) 
  : Arbiter( parent, name, size ) {
  _improved = imp;
  _pointer = 0;
  _input_share.resize(size,0);
  _share.resize(size,0);
  _position.resize(size,0);
  _req.resize(size,0);
  _total_pri.resize(size,0);
  _total_share.resize(size,0);
}

WeightedRRArbiter::~WeightedRRArbiter(){
  /*
  cout<<FullName()<<endl;
  for(int i = 0; i<_size; i++){
    cout<<_req[i]<<"\t";
  }
  cout<<endl;
  for(int i = 0; i<_size; i++){
    cout<<_total_pri[i]<<"\t";
  }
  cout<<endl;
  for(int i = 0; i<_size; i++){
    cout<<_total_share[i]<<"\t";
  }
  cout<<endl;
  for(int i = 0; i<_size; i++){
    cout<<_position[i]<<"\t";
  }
  cout<<endl;
  */
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
 
    //bump the port that was ignored
    if(_improved){
      if(_pointer!=_selected){
	_share[_pointer]+=_input_share[_pointer];
	//_share[_pointer] = (_share[_pointer]>7)?7:_share[_pointer];
      }
    }

    //pointer calculation based on weights
    if(_share[_selected]==0){
      //natural expiration of weights, bump weight
      _share[_selected]+=_input_share[_selected];
      _pointer = ( _selected + 1 ) % _size ;
    } else {
      _pointer =  _selected;
    }
  }
  
  _position[_pointer]++;
}

void WeightedRRArbiter::AddRequest( int input, int id, int pri )
{
  assert(pri>0);

  //update weight
  if(_share[input] == 0){
    _share[input] = pri;
  }

  _input_share[input] = pri;

  _req[input]++;
  _total_pri[input]+=pri;
  _total_share[input]+=_share[input];

  if(!_request[input].valid || (_request[input].pri < pri)) {
    if(_num_reqs == 0) {
      _highest_pri = pri;
      _best_input = input;
      Arbiter::AddRequest(input, id, pri);
      //round robin the pointers, but check for _share
    } else if(RoundRobinArbiter::Supersedes(input,1,_best_input, 1, _pointer,_size)){
      assert(_share[input]>0);
      _highest_pri = pri;
      _best_input = input;   
      Arbiter::AddRequest(input, id, pri);
    }
  }
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
