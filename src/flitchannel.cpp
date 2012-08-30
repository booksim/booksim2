// $Id: flitchannel.cpp 1144 2009-03-06 07:13:09Z mebauer $

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
//  $Id: flitchannel.cpp 1144 2009-03-06 07:13:09Z mebauer $
// ----------------------------------------------------------------------
FlitChannel::FlitChannel() {
  _delay  = 0;
  for ( int i = 0; i < Flit::NUM_FLIT_TYPES; i++)
    _active[i] = 0;
  _idle   = 0;
  _cookie = 0;
  _delay = 1; 
  shared = false;
  type = NORMAL;
  send_buf_pos =0;
  recv_buf_pos =0;
  send_align = 0;
  recv_align = 0;
  recv_buffer[0].valid = false;
  recv_buffer[1].valid = false;
  send_req[0]=MPI_REQUEST_NULL;
  send_req[1]=MPI_REQUEST_NULL;
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

//multithreading
void FlitChannel::SetShared(FlitChannelType t, int src, int sin){
  if(!shared){
    shared = true;
    type = t;
    sink_id = sin;
    source_id = src;
  }
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
  
  if(shared){
    if(!flit){
      send_buffer[send_buf_pos%2].valid = false;
    } else {
      flit->valid = true;
      memcpy(&(send_buffer[send_buf_pos%2]),flit,sizeof(Flit));
      delete flit;
    }
  
    MPI_Wait(&(send_req[send_buf_pos%2]),MPI_STATUS_IGNORE);
    MPI_Isend(&(send_buffer[send_buf_pos%2]),1,MPI_Flit,this->sink_id,((this->id<<3)+(this->send_align++)%MAX_ALIGN),MPI_COMM_WORLD, &(send_req[send_buf_pos%2]));
    send_buf_pos++;
  } else {
    while ( (_queue.size() > (unsigned int)_delay) && (_queue.front() == 0) )
      _queue.pop( );
    
    _queue.push(flit);
  }
}

Flit* FlitChannel::ReceiveFlit() {
  Flit* f;
  if(shared){
    MPI_Recv(&(recv_buffer[recv_buf_pos%2]),1,MPI_Flit,this->source_id,((this->id<<3)+(this->recv_align++)%MAX_ALIGN),MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    
    if(recv_buffer[recv_buf_pos%2].valid == false){
      _queue.push(0);
    } else {
      
      Flit* nflit = new Flit();
      memcpy(nflit,&(recv_buffer[recv_buf_pos%2]),sizeof(Flit));
      _queue.push(nflit);
    }
    recv_buf_pos++;
  }
  
  if ( _queue.empty() )
    return 0;
  f = _queue.front();
  _queue.pop();
  return f;
}

Flit* FlitChannel::PeekFlit( )
{
  if ( _queue.empty() )
    return 0;

  return _queue.front();
}
