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

#ifndef _PIPEFIFO_HPP_
#define _PIPEFIFO_HPP_

#include "module.hpp"

template<class T> class PipelineFIFO : public Module {
  int _lanes;
  int _depth;

  int _pipe_len;
  int _pipe_ptr;
  
  T ***_data;

public:
  PipelineFIFO( Module *parent, const string& name, int lanes, int depth );
  ~PipelineFIFO( );

  void Write( T* val, int lane = 0 );
  void WriteAll( T* val );

  T*   Read( int lane = 0 );

  void Advance( );
};

template<class T> PipelineFIFO<T>::PipelineFIFO( Module *parent, 
						 const string& name, 
						 int lanes, int depth ) :
  Module( parent, name ),
  _lanes( lanes ), _depth( depth )
{
  _pipe_len = depth + 1;
  _pipe_ptr = 0;

  _data = new T ** [_lanes];
  for ( int l = 0; l < _lanes; ++l ) {
    _data[l] = new T * [_pipe_len];

    for ( int d = 0; d < _pipe_len; ++d ) {
      _data[l][d] = 0;
    }
  }
}

template<class T> PipelineFIFO<T>::~PipelineFIFO( ) 
{
  for ( int l = 0; l < _lanes; ++l ) {
    delete [] _data[l];
  }
  delete [] _data;
}

template<class T> void PipelineFIFO<T>::Write( T* val, int lane )
{
  _data[lane][_pipe_ptr] = val;
}

template<class T> void PipelineFIFO<T>::WriteAll( T* val )
{
  for ( int l = 0; l < _lanes; ++l ) {
    _data[l][_pipe_ptr] = val;
  }
}

template<class T> T* PipelineFIFO<T>::Read( int lane )
{
  return _data[lane][_pipe_ptr];
}

template<class T> void PipelineFIFO<T>::Advance( )
{
  _pipe_ptr = ( _pipe_ptr + 1 ) % _pipe_len;
}

#endif 
