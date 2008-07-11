//////////////////////////////////////////////////////////////////////
//
//  File Name: creditchannel.cpp
//  Author: James Balfour, Rebecca Schultz
//
/////
#include "creditchannel.hpp"
#include "power.hpp"

CreditChannel::CreditChannel() {
  _delay = 0;
}

void CreditChannel::SetLatency( int cycles ) {

  _delay = cycles ;
  while ( !_queue.empty() )
    _queue.pop( );
  for (int i = 1; i < _delay; i++)
    _queue.push(0);
}


void CreditChannel::SendCredit( Credit* credit ) {

  while ( (_queue.size() > _delay) && (_queue.front() == 0) )
    _queue.pop( );

  _queue.push(credit);

}

Credit* CreditChannel::ReceiveCredit() {

  if ( _queue.empty( ) )
    return 0;

  Credit* c = _queue.front();
  _queue.pop();
  return c;
}

Credit* CreditChannel::PeekCredit( ) 
{
  if ( _queue.empty() )
    return 0;

  return _queue.front( );
}
