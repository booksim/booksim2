// $Id$
// ----------------------------------------------------------------------
//
//  Arbiter: Base class for Matrix and Round Robin Arbiter
//
// ----------------------------------------------------------------------

#ifndef _ARBITER_HPP_
#define _ARBITER_HPP_

#include "module.hpp"

class Arbiter : public Module {

protected:

  typedef struct { 
    bool valid ;
    int id ;
    int pri ;
  } entry_t ;
  
  entry_t* _request ;
  int  _input_size ;
  int  _num_reqs ;
  int  _last_req ;
  int  _selected ;

public:

  // Constructors
  Arbiter( Module *parent, const string &name, int size ) ;
  virtual ~Arbiter() ;
  
  // Print priority matrix to standard output
  virtual void PrintState() const = 0 ;
  
  // Register request with arbiter
  void AddRequest( int input, int id, int pri ) ;

  // Update priority matrix based on last aribtration result
  virtual void UpdateState() = 0 ; 

  // Arbitrate amongst requests. Returns winning input and 
  // updates pointers to metadata when valid pointers are passed
  virtual int Arbitrate( int* id = 0, int* pri = 0) = 0 ;
  
  static Arbiter *NewArbiter( Module *parent, const string &name,
			      const string &arb_type, int size );
} ;

#endif
