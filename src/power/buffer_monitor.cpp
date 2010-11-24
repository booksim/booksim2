// $Id$

/*
Copyright (c) 2007-2010, Trustees of The Leland Stanford Junior University
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

#include "buffer_monitor.hpp"

#include "flit.hpp"

BufferMonitor::BufferMonitor( int inputs ) {
  _cycles = 0 ;
  _inputs = inputs ;

  const int n = 4 * inputs  * Flit::NUM_FLIT_TYPES ;
  _reads.resize(n, 0) ;
  _writes.resize(n, 0) ;
}

int BufferMonitor::index( int input, int flitType ) const {
  if ( input < 0 || input > _inputs ) 
    cerr << "ERROR: input out of range in BufferMonitor" << endl ;
  if ( flitType < 0 || flitType> Flit::NUM_FLIT_TYPES ) 
    cerr << "ERROR: flitType out of range in flitType" << endl ;
  return flitType + Flit::NUM_FLIT_TYPES * input ;
}

void BufferMonitor::cycle() {
  _cycles++ ;
}

void BufferMonitor::write( int input, Flit const * f ) {
  _writes[ index(input, f->type) ]++ ;
}

void BufferMonitor::read( int input, Flit const * f ) {
  _reads[ index(input, f->type) ]++ ;
}

ostream& operator<<( ostream& os, const BufferMonitor& obj ) {
  for ( int i = 0 ; i < obj._inputs ; i++ ) {
    os << "[ " << i << " ] " ;
    for ( int f = 0 ; f < Flit::NUM_FLIT_TYPES ; f++ ) {
      os << "Type=" << f
	 << ":(R#" << obj._reads[ obj.index( i, f) ]  << ","
	 << "W#" << obj._writes[ obj.index( i, f) ] << ")" << " " ;
    }
    os << endl ;
  }
  return os ;
}

