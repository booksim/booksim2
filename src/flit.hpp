#ifndef _FLIT_HPP_
#define _FLIT_HPP_

#include "booksim.hpp"
#include <iostream>

struct Flit {

  const static int NUM_FLIT_TYPES = 5;
  enum FlitType { READ_REQUEST  = 0, 
		  READ_REPLY    = 1,
		  WRITE_REQUEST = 2,
		  WRITE_REPLY   = 3,
                  ANY_TYPE      = 4 };
  FlitType type;

  int vc;

  bool head;
  bool tail;
  bool true_tail;
  
  int  time;

  int  sn;
  int  rob_time;

  int  id;
  bool record;

  int  src;
  int  dest;

  int  pri;

  int  hops;
  bool watch;

  //for credit tracking, last router visited
  mutable int from_router;

  // Fields for multi-phase algorithms
  mutable int intm;
  mutable int ph;

  //reservation
  mutable int delay;

  mutable int dr;
  mutable int minimal; // == 1 minimal routing, == 0, nonminimal routing

  // Which VC parition to use for deadlock avoidance in a ring
  mutable int ring_par;

  // Fileds for XY or YX randomized routing
  mutable int x_then_y;

  // Fields for arbitrary data
  void* data ;

  // Constructor
  Flit() ;
};

ostream& operator<<( ostream& os, const Flit& f );

#endif
