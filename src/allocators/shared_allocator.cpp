// $Id$

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
//  SeparableInputFirstAllocator: Separable Input-First Allocator
//
// ----------------------------------------------------------------------

#include "shared_allocator.hpp"

#include "booksim.hpp"
#include "arbiter.hpp"

#include <vector>
#include <iostream>
#include <cstring>

SharedAllocator::
SharedAllocator( Module* parent, 
		 const string& name, 
		 int inputs,int outputs, const string& in_arb_type
		 , const string& out_arb_type)
{
  
  nonspec = new SeparableInputFirstAllocator( parent, name+"_nospec",
					      inputs, 
					      outputs, 
					      in_arb_type , 
					      out_arb_type);
  spec =  new SeparableInputFirstAllocator( parent, name+"_spec",
					    inputs, 
					    outputs, 
					    in_arb_type , 
					    out_arb_type);
}

void SharedAllocator::Clear(){
  nonspec->Clear();
  spec->Clear();
}

void SharedAllocator::Allocate(){
  nonspec->Allocate();
  spec->Allocate();
}

void SharedAllocator::UpdateNonSpec(int input, int output){
  nonspec->UpdateState(input, output);
}


void SharedAllocator::UpdateSpec(int input, int output){
  spec->UpdateState(input, output);
}
