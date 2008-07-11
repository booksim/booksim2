//////////////////////////////////////////////////////////////////////
//
//  File Name: creditchannel.hpp
//  Author: James Balfour, Rebecca Schultz
//
//  The CreditChannel models a credit channel with a multi-cycle 
//   transmission delay. The channel latency can be specified as 
//   an integer number of simulator cycles.
//
/////
#ifndef CREDITCHANNEL_HPP
#define CREDITCHANNEL_HPP

#include "credit.hpp"
#include <queue>
using namespace std;

class CreditChannel {
public:
  CreditChannel();

  // Physical Parameters
  void SetLatency( int cycles );
  // Send credit 
  void SendCredit( Credit* credit );
  
  // Receive Credit
  Credit* ReceiveCredit( ); 

  // Peek at Credit
  Credit* PeekCredit( );

private:
  int            _delay;
  queue<Credit*> _queue;

  ////////////////////////////////////////
  //
  // Power Models
  //
  ////////////////////////////////////////


  // Statistics for Activity Factors
  int    _active;
  int    _idle;

};

#endif
