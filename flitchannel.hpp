// ----------------------------------------------------------------------
//
//  File Name: flitchannel.hpp
//
//  The FlitChannel models a flit channel with a multi-cycle 
//   transmission delay. The channel latency can be specified as 
//   an integer number of simulator cycles.
// ----------------------------------------------------------------------

#ifndef FLITCHANNEL_HPP
#define FLITCHANNEL_HPP

// ----------------------------------------------------------------------
//  $Author: jbalfour $
//  $Date: 2007/06/27 23:10:17 $
//  $Id: flitchannel.hpp,v 1.2 2007/06/27 23:10:17 jbalfour Exp $
// ----------------------------------------------------------------------

#include "flit.hpp"
#include "globals.hpp"
#include <queue>
using namespace std;
class Router ;

class FlitChannel {
public:
  FlitChannel();
  ~FlitChannel();

  void SetSource( Router* router ) ;
  void SetSink( Router* router ) ;
  // Phsyical Parameters
  void SetLatency( int cycles ) ;
  int GetLatency() { return _delay ; }

  // Check for flit on input. Used for tracking channel use
  bool InUse();

  // Send flit 
  void SendFlit( Flit* flit );

  // Receive Flit
  Flit* ReceiveFlit( ); 

  // Peek at Flit
  Flit* PeekFlit( );

private:
  int          _delay;
  queue<Flit*> _queue;
  
  ////////////////////////////////////////
  //
  // Power Models OBSOLETE
  //
  ////////////////////////////////////////

  string _routerSource ;
  string _routerSink ;
  

  // Statistics for Activity Factors
  int          _active[Flit::NUM_FLIT_TYPES];
  int          _idle; 

public:
  int          _cookie;
};

#endif
