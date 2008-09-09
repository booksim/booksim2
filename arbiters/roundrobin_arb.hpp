// ----------------------------------------------------------------------
//
//  RoundRobin: Round Robin Arbiter
//
// ----------------------------------------------------------------------

#ifndef _ROUNDROBIN_HPP_
#define _ROUNDROBIN_HPP_

#include "arbiter.hpp"

class RoundRobinArbiter : public Arbiter {

  // Priority matrix and access methods
  int  _pointer ;

public:

  // Constructors
  RoundRobinArbiter() ;

  // Print priority matrix to standard output
  virtual void PrintState() const ;
  
  // Update priority matrix based on last aribtration result
  virtual void UpdateState() ; 

  // Arbitrate amongst requests. Returns winning input and 
  // updates pointers to metadata when valid pointers are passed
  virtual int Arbitrate( int* id = 0, int* pri = 0) ;

} ;

#endif
