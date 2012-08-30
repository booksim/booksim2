// $Id: fair_wavefront.cpp 4080 2011-10-22 23:11:32Z dub $

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

/*fair_wavefront.cpp
 *
 *A fairer wave front allocator
 *
 */
#include "booksim.hpp"

#include "rr_wavefront.hpp"

RRWavefront::RRWavefront( Module *parent, const string& name,
			  int inputs, int outputs ) :
  Wavefront( parent, name, inputs, outputs ),
  _skip_diags(max(inputs, outputs))
{
}

void RRWavefront::AddRequest( int in, int out, int label, 
			      int in_pri, int out_pri )
{
  Wavefront::AddRequest(in, out, label, in_pri, out_pri);
  int offset = (in + (_square - out) + (_square - _pri)) % _square;
  if(offset < _skip_diags) {
    _skip_diags = offset;
  }
}

void RRWavefront::Allocate( )
{
  Wavefront::Allocate();
  _pri = (_pri + _skip_diags) % _square;
  _skip_diags = _square;
}


