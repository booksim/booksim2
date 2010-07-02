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

#ifndef _SEPARABLE_HPP_
#define _SEPARABLE_HPP_

#include "allocator.hpp"
#include "arbiter.hpp"
#include <assert.h>

#include <vector>

class SeparableAllocator : public Allocator {
  
protected:

  int* _matched ;

  Arbiter** _input_arb ;
  Arbiter** _output_arb ;

  vector<sRequest>* _requests ;

public:
  
  SeparableAllocator( Module* parent, const string& name, int inputs,
		      int outputs, const string& arb_type ) ;
  
  virtual ~SeparableAllocator() ;

  //
  // Allocator Interface
  //
  virtual void Clear() ;
  virtual int  ReadRequest( int in, int out ) const ;
  virtual bool ReadRequest( sRequest& req, int in, int out ) const ;
  virtual void AddRequest( int in, int out, int label = 1, 
			   int in_pri = 0, int out_pri = 0 ) ;
  virtual void RemoveRequest( int in, int out, int label = 1 ) ;
  virtual void Allocate() = 0 ;
  virtual void PrintRequests( ostream * os = NULL ) const ;

} ;

#endif
