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
//  SeparableInputFirstAllocator: Separable Input-First Allocator
//
// ----------------------------------------------------------------------

#include "separable_input_first.hpp"

#include "booksim.hpp"
#include "arbiter.hpp"

#include <vector>
#include <iostream>
#include <string.h>

SeparableInputFirstAllocator::
SeparableInputFirstAllocator( Module* parent, const string& name, int inputs,
			      int outputs, const string& arb_type )
  : SeparableAllocator( parent, name, inputs, outputs, arb_type )
{}

void SeparableInputFirstAllocator::AddRequest( int in, int out, int label, int in_pri,
				     int out_pri ) {

  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  sRequest req ;
  req.port    = out ;
  req.label   = label ;
  req.in_pri  = in_pri ;
  req.out_pri = out_pri ;
  _requests[in].push_back( req ) ;
  in_event.insert(in);
  if ( req.label > -1 ) {
    _input_arb[in]->AddRequest( out, _requests[in].size()-1, in_pri ) ;
  }
  
}


void SeparableInputFirstAllocator::Allocate() {
  
  _ClearMatching() ;
  
  //  cout << "SeparableInputFirstAllocator::Allocate()" << endl ;
  //  PrintRequests() ;
  
  // Execute the input arbiters and propagate the grants to the
  // output arbiters.
  for(set<int>::iterator i = in_event.begin(); i!=in_event.end(); i++){
    int input = *i;
    int id;
    int pri;
    int out =_input_arb[input]->Arbitrate( &id, &pri );
    const sRequest& req = (_requests[input][id]); 
    assert(out == req.port && pri == req.in_pri);
    _output_arb[out]->AddRequest( input, req.label, req.out_pri );
    out_event.insert(out);
  }


  // Execute the output arbiters.
  for(set<int>::iterator i = out_event.begin(); i!=out_event.end(); i++){

    int label, pri ;
    int output = *i;
    int  input = _output_arb[output]->Arbitrate( &label, &pri ) ;
    assert( _inmatch[input] == -1 && _outmatch[output] == -1 ) ;
    _inmatch[input]   = output ;
    _outmatch[output] = input ;
    _input_arb[input]->UpdateState() ;
    _output_arb[output]->UpdateState() ;

  }
  in_event.clear();
  out_event.clear();
}
