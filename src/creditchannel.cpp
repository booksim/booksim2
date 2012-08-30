// $Id$

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

//////////////////////////////////////////////////////////////////////
//
//  File Name: creditchannel.cpp
//  Author: James Balfour, Rebecca Schultz
//
/////
#include "creditchannel.hpp"
#include "booksim.hpp"
CreditChannel::CreditChannel() {
  _delay = 0;
  shared = false;
  chan_lock = 0;
  _queue = new normqueue<Credit*>();
}
CreditChannel::~CreditChannel(){
  if(shared){
    pthread_mutex_destroy(chan_lock);
    free(chan_lock);
  }
  delete(_queue);
}


//multithreading
void CreditChannel::SetShared(){
  if(!shared){
    shared = true;
    chan_lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(chan_lock,0);
    wait_valid = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
    pthread_cond_init(wait_valid,0);
    delete(_queue);
    _queue = new lfqueue<Credit*>();
    //_queue.initialize();
    valid = 0;
  }
}

void CreditChannel::SetLatency( int cycles ) {

  _delay = cycles ;
  while ( !_queue->empty() )
    _queue->pop( );
  for (int i = 1; i < _delay; i++)
    _queue->push(0);
}


void CreditChannel::SendCredit( Credit* credit ) {

  while ( (_queue->size() > (unsigned int)_delay) && (_queue->front() == 0) )
    _queue->pop( );

  _queue->push(credit);
  if(shared){
    pthread_mutex_lock(chan_lock);
    valid++;
    pthread_cond_signal(wait_valid);
    pthread_mutex_unlock(chan_lock);
  }
}

Credit* CreditChannel::ReceiveCredit() {
  if(shared){
    pthread_mutex_lock(chan_lock);
    if(!valid)
      pthread_cond_wait(wait_valid,chan_lock);
    valid--;
    pthread_mutex_unlock(chan_lock);
  }
  if ( _queue->empty( ) )
    return 0;

  Credit* c = _queue->front();
  _queue->pop();
  return c;
}

Credit* CreditChannel::PeekCredit( ) 
{
  if ( _queue->empty() )
    return 0;

  return _queue->front( );
}
