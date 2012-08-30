// $Id: separable_output_first.cpp 938 2008-12-12 03:06:32Z dub $

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
//  SeparableOutputFirstAllocator: Separable Output-First Allocator
//
// ----------------------------------------------------------------------

#include "separable_output_first.hpp"

#include "booksim.hpp"
#include "arbiter.hpp"

#include <vector>
#include <iostream>
#include <string.h>

SeparableOutputFirstAllocator::
SeparableOutputFirstAllocator( Module* parent, const string& name, int inputs,
			       int outputs, const string& arb_type )
  : SeparableAllocator( parent, name, inputs, outputs, arb_type )
{}

void SeparableOutputFirstAllocator::Allocate() {
  
  _ClearMatching() ;

//  cout << "SeparableOutputFirstAllocator::Allocate()" << endl ;
//  PrintRequests() ;
  
  list<sRequest> out_reqs[_outputs];
  
  for ( int input = 0 ; input < _inputs ; input++ ) {
    
    // Add requests to the output arbiters
    list<sRequest>::const_iterator it  = _requests[input].begin() ;
    list<sRequest>::const_iterator end = _requests[input].end() ;
    while ( it != end ) {
      const sRequest& req = *it ;
      if ( req.label > -1 ) {
	_output_arb[req.port]->AddRequest( input, req.label, req.out_pri );
      }
      it++ ;
    }
    
  }
  
  for ( int output = 0; output < _outputs; output++ ) {
    
    // Execute the output arbiters and propagate the grants to the
    // input arbiters.
    int in = _output_arb[output]->Arbitrate( NULL, NULL ) ;
    
    if ( in > -1 ) {
      list<sRequest>::const_iterator it  = _requests[in].begin() ;
      list<sRequest>::const_iterator end = _requests[in].end() ;
      while ( it != end ) {
	const sRequest& req = *it ;
	if ( ( req.label > -1 ) && ( req.port == output ) ) {
	  _input_arb[in]->AddRequest( output, req.label, req.in_pri );
	}
	it++ ;
      }
    }
  }
  
  // Execute the input arbiters.
  for ( int input = 0 ; input < _inputs ; input++ ) {

    int label, pri ;
    int output      = _input_arb[input]->Arbitrate( &label, &pri ) ;
  
    if ( output > -1 ) {
      assert( _inmatch[input] == -1 && _outmatch[output] == -1 ) ;
      _inmatch[input]   = output ;
      _outmatch[output] = input ;
      _input_arb[input]->UpdateState() ;
      _output_arb[output]->UpdateState() ;

    }
  }
}
