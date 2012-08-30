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
//  Probabilistic: Probabilistic Arbiter
//
// ----------------------------------------------------------------------

#include "probabilistic_arb.hpp"
#include <iostream>
#include <limits>

using namespace std ;

ProbabilisticArbiter::ProbabilisticArbiter( Module *parent, const string &name,
				      int size ) 
  : Arbiter( parent, name, size ),_range(0) {
}

void ProbabilisticArbiter::PrintState() const  {
  cout << "Probabilistic arbiter has no pointers " << endl ;
}

void ProbabilisticArbiter::UpdateState() {
  //no state
}

void ProbabilisticArbiter::AddRequest( int input, int id, int pri )
{
  //requires non-zeo probability
  assert(pri>0);
  if(!_request[input].valid || (_request[input].pri < pri)) {
    _range+=pri;
    //subtract out previous pri
    if(_request[input].valid ){
      _range-=_request[input].pri;
    }

  } 
  
  Arbiter::AddRequest(input, id, pri);
}

int ProbabilisticArbiter::Arbitrate( int* id, int* pri ) {
 

  if(_range!=0){
    //overflow detect
    assert(_range>0);
    int choice = RandomInt(_range-1);
    int sum = 0;
    do{
      _best_input++;
      if(_request[_best_input].valid){
	sum+=_request[_best_input].pri;
      }
    }while(sum<=choice);
    assert(_best_input<_size);
  }
  _selected =  _best_input;
  return Arbiter::Arbitrate(id, pri);
}

void ProbabilisticArbiter::Clear()
{
  _range = 0;
  _highest_pri = numeric_limits<int>::min();
  _best_input = -1;
  Arbiter::Clear();
}
