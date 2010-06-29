// $Id$

/*
Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
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
//  Arbiter: Base class for Matrix and Round Robin Arbiter
//
// ----------------------------------------------------------------------

#include "arbiter.hpp"
#include "roundrobin_arb.hpp"
#include "matrix_arb.hpp"

#include <limits>
#include <cassert>

using namespace std ;

Arbiter::Arbiter( Module *parent, const string &name, int size )
  : Module( parent, name ),
    _input_size(size), _request(0), _num_reqs(0), _last_req(-1), _best_input(-1), _highest_pri(numeric_limits<int>::min())
{
  _request = new entry_t[size];
  for ( int i = 0 ; i < size ; i++ ) 
    _request[i].valid = false ;
}

Arbiter::~Arbiter()
{
  if ( _request ) 
    delete[] _request ;
}

void Arbiter::AddRequest( int input, int id, int pri )
{
  assert( 0 <= input && input < _input_size ) ;
  if(!_request[input].valid || (_request[input].pri < pri)) {
    _last_req = input ;
    if(!_request[input].valid) {
      _num_reqs++ ;
      _request[input].valid = true ;
    }
    _request[input].id = id ;
    _request[input].pri = pri ;
  }
}

Arbiter *Arbiter::NewArbiter( Module *parent, const string& name,
			      const string &arb_type, int size)
{
  Arbiter *a = NULL;
  if(arb_type == "round_robin") {
    a = new RoundRobinArbiter( parent, name, size );
  } else if(arb_type == "matrix") {
    a = new MatrixArbiter( parent, name, size );
  } else assert(false);
  return a;
}
