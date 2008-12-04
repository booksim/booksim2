// $Id $

/*
Copyright (c) 2007, Trustees of Leland Stanford Junior University
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
//  File Name: flitchannel.cpp
//  Author: James Balfour, Rebecca Schultz
//
// ----------------------------------------------------------------------
#include <iostream>
#include <iomanip>
#include "router.hpp"
#include "flitchannel.hpp"

// ----------------------------------------------------------------------
//  $Author: jbalfour $
//  $Date: 2007/06/27 23:10:17 $
//  $Id$
// ----------------------------------------------------------------------
FlitChannel::FlitChannel() {
  _delay  = 0;
  for ( int i = 0; i < Flit::NUM_FLIT_TYPES; i++)
    _active[i] = 0;
  _idle   = 0;
  _cookie = 0;
  _delay = 1; 
}

FlitChannel::~FlitChannel() {

  // Total Number of Cycles
  const double NC = _active[0] + _active[1] + _active[2] + _active[3] + _active[4]+ _idle;
  
  // Activity Factor 
  const double AFs = double(_active[0] + _active[3]) / NC;
  const double AFl = double(_active[1] + _active[2]) / NC;
  
  if(_print_activity){
    cout << "FlitChannel: " 
	 << "[" 
	 << _routerSource
	 <<  " -> " 
	 << _routerSink
	 << "] " 
	 << "[Latency: " << _delay << "] "
	 << "(" << _active[0] << "," << _active[1] << "," << _active[2] 
	 << "," << _active[3] << "," << _active[4] << ") (I#" << _idle << ")" << endl ;
  }
}

void FlitChannel::SetSource( Router* router ) {
  _routerSource = router->GetID() ;
}

int FlitChannel::GetSource(){
  return _routerSource;
}

void FlitChannel::SetSink( Router* router ) {
  _routerSink = router->GetID() ;
}

int FlitChannel::GetSink(){
  return _routerSink;
}


void FlitChannel::SetLatency( int cycles ) {

  _delay = cycles; 
  while ( !_queue.empty() )
    _queue.pop();  
  for (int i = 1; i < _delay ; i++)
    _queue.push(0);
}

bool FlitChannel::InUse() {
  if ( _queue.empty() )
    return false;
  return ( _queue.back() != 0 );
}

void FlitChannel::SendFlit( Flit* flit ) {

  if ( flit )
    ++_active[flit->type];
  else 
    ++_idle;

  while ( (_queue.size() > (unsigned int)_delay) && (_queue.front() == 0) )
    _queue.pop( );

  _queue.push(flit);
}

Flit* FlitChannel::ReceiveFlit() {
  if ( _queue.empty() )
    return 0;

  Flit* f = _queue.front();
  _queue.pop();
  return f;
}

Flit* FlitChannel::PeekFlit( )
{
  if ( _queue.empty() )
    return 0;

  return _queue.front();
}
