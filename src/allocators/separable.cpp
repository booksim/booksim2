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
//  SeparableAllocator: Separable Allocator
//
// ----------------------------------------------------------------------

#include "separable.hpp"

#include "booksim.hpp"
#include "roundrobin_arb.hpp"
#include "matrix_arb.hpp"

#include <vector>
#include <iostream>
#include <string.h>
#include <sstream>

SeparableAllocator::SeparableAllocator( Module* parent, const string& name,
					int inputs, int outputs,
					const string& arb_type )
  : Allocator( parent, name, inputs, outputs )
{
  
  _requests  = new vector<sRequest> [inputs] ;
  
  _input_arb = new Arbiter*[inputs];
  _output_arb = new Arbiter*[outputs];
  
  ostringstream arb_name;
  
  for (int i = 0; i < inputs; ++i) {
    arb_name << "arb_i" << i;
    _input_arb[i] = Arbiter::NewArbiter(this, arb_name.str(), arb_type, outputs);
    arb_name.str("");
  }
  for (int i = 0; i < outputs; ++i) {
    arb_name << "arb_o" << i;
    _output_arb[i] = Arbiter::NewArbiter(this, arb_name.str( ), arb_type, inputs);
    arb_name.str("");
  }
  
  Clear() ;
}

SeparableAllocator::~SeparableAllocator() {

  delete[] _requests ;

  for (int i = 0; i < _inputs; ++i) {
    delete _input_arb[i];
  }
  delete[] _input_arb ;

  for (int i = 0; i < _outputs; ++i) {
    delete _output_arb[i];
  }
  delete[] _output_arb ;
}

void SeparableAllocator::Clear() {
  for ( int i = 0 ; i < _inputs ; i++ )
    _requests[i].clear() ;

}

int SeparableAllocator::ReadRequest( int in, int out ) const {
  sRequest r ;
  if ( !ReadRequest( r, in, out) ) {
    return -1 ;
  } 
  return r.label ;
}

bool SeparableAllocator::ReadRequest( sRequest &req, int in, int out ) const {

  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  const sRequest * sreq = NULL;

  vector<sRequest>::const_iterator match = _requests[in].begin() ;
  while ( match != _requests[in].end() ) {
    if ( ( match->port == out ) && ( !sreq || ( match->in_pri > sreq->in_pri ) ) ) {
      sreq = &(*match) ;
    }
    match++ ;
  }

  if(sreq) {
    req = *sreq;
    return true;
  }
  return false ;

}

void SeparableAllocator::AddRequest( int in, int out, int label, int in_pri,
				     int out_pri ) {

  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  sRequest req ;
  req.port    = out ;
  req.label   = label ;
  req.in_pri  = in_pri ;
  req.out_pri = out_pri ;
  
  _requests[in].push_back( req ) ;
  
}


void SeparableAllocator::RemoveRequest( int in, int out, int label ) {
  // Method not implemented yet
  assert( false ) ;

}

void SeparableAllocator::PrintRequests( ostream * os ) const {
  
  if(!os) os = &cout;
  
  *os << "Requests = [ ";
  
  for ( int input = 0 ; input < _inputs ; input++ ) {
    if ( _requests[input].empty() )
      continue ;
    
    vector<sRequest>::const_iterator it  = _requests[input].begin() ;
    vector<sRequest>::const_iterator end = _requests[input].end() ;

    *os << input << " -> [ ";
    while ( it != end ) {
      const sRequest& req = *it ;
      *os << "(lbl:" << req.label
	   << ",out:" << req.port
	   << ",ipr:" << req.in_pri
	   << ",opr:" << req.out_pri
	   << ") ";
      it++ ;
    }
    *os << "] ";
  }
  *os << "]." << endl;
}
