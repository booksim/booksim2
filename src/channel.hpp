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

//////////////////////////////////////////////////////////////////////
//
//  File Name: channel.hpp
//
//  The Channel models a generic channel with a multi-cycle 
//   transmission delay. The channel latency can be specified as 
//   an integer number of simulator cycles.
//
/////
#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <queue>

using namespace std;

template<class T>
class Channel {
public:
  Channel( int cycles = 1 );

  // Physical Parameters
  void SetLatency( int cycles );
  int GetLatency() { return _delay ; }
  
  // Send data 
  virtual void Send( T* data );
  
  // Receive data
  T* Receive( ); 

  // Peek at data
  T* Peek( );

protected:
  int       _delay;
  queue<T*> _queue;

};

template<class T>
Channel<T>::Channel( int cycles ) {
  SetLatency(cycles);
}

template<class T>
void Channel<T>::SetLatency( int cycles ) {

  _delay = cycles ;
  while ( !_queue.empty() )
    _queue.pop( );
  for (int i = 0; i < _delay; i++)
    _queue.push(0);
}

template<class T>
void Channel<T>::Send( T* data ) {

  while ( (_queue.size() > (unsigned int)_delay) && (_queue.front() == 0) )
    _queue.pop( );

  _queue.push(data);

}

template<class T>
T* Channel<T>::Receive() {

  if ( _queue.empty( ) )
    return 0;

  T* data = _queue.front();
  _queue.pop();
  return data;
}

template<class T>
T* Channel<T>::Peek( ) 
{
  if ( _queue.empty() )
    return 0;

  return _queue.front( );
}

#endif
