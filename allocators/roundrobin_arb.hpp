// ----------------------------------------------------------------------
//
//  RoundRobin: Round Robin Arbiter
//
// ----------------------------------------------------------------------
#ifndef _ROUNDROBIN_HPP_
#define _ROUNDROBIN_HPP_

// ----------------------------------------------------------------------
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:10:51 $
//  $Id: roundrobin_arb.hpp,v 1.1 2007/05/17 17:10:51 jbalfour Exp $
// ----------------------------------------------------------------------

class RoundRobinArbiter {

  typedef struct { 
    bool valid ;
    int id ;
    int pri ;
  } entry_t ;
  
  entry_t* _request ;
  int  _input_size ;
  int  _skip_arb ;
  int  _selected ;

  // Priority matrix and access methods
  int  _pointer ;

public:

  // Constructors
  RoundRobinArbiter() ;
  ~RoundRobinArbiter();
  void Init( int size ) ; 

  // Print priority matrix to standard output
  void PrintPriority() const ;
  
  // Register request with arbiter
  void AddRequest( int input, int id, int pri ) ;

  // Update priority matrix based on last aribtration result
  void UpdatePriority() ; 

  // Arbitrate amongst requests. Returns winning input and 
  // updates pointers to metadata when valid pointers are passed
  int Arbitrate( int* id = 0, int* pri = 0) ;

} ;

#endif
