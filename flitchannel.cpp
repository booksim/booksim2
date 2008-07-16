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
//  $Id: flitchannel.cpp,v 1.2 2007/06/27 23:10:17 jbalfour Exp jbalfour $
// ----------------------------------------------------------------------
FlitChannel::FlitChannel() {
  _delay  = 0;
  for ( int i = 0; i < Flit::NUM_FLIT_TYPES; i++)
    _active[i] = 0;
  _idle   = 0;
  _cookie = 0;
}

FlitChannel::~FlitChannel() {

  // Total Number of Cycles
  const double NC = _active[0] + _active[1] + _active[2] + _active[3] + _idle;
  
  // Activity Factor 
  const double AFs = double(_active[0] + _active[3]) / NC;
  const double AFl = double(_active[1] + _active[2]) / NC;
  /*
  cout << "FlitChannel: " 
       << "[" 
       << _routerSource
       <<  " -> " 
       << _routerSink
       << "] " 
       << "[Latency: " << _delay << "] "
       << "(" << _active[0] << "," << _active[1] << "," << _active[2] 
       << "," << _active[3] << ") (I#" << _idle << ")" << endl ;
  */
}

void FlitChannel::SetSource( Router* router ) {
  _routerSource = router->Name() ;
}

void FlitChannel::SetSink( Router* router ) {
  _routerSink = router->Name() ;
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

  while ( (_queue.size() > _delay) && (_queue.front() == 0) )
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
