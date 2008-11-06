// ----------------------------------------------------------------------
//
//  Matrix: Matrix Arbiter
//
// ----------------------------------------------------------------------

#ifndef _MATRIX_ARB_HPP_
#define _MATRIX_ARB_HPP_

#include "arbiter.hpp"

#include <iostream>
using namespace std ;

class MatrixArbiter : public Arbiter {

  // Priority matrix and access methods
  int* _matrix ;
  int  _Priority( int row, int column ) const ;
  void _SetPriority( int row, int column, int val ) ;

public:

  // Constructors
  MatrixArbiter( Module *parent, const string &name, int size ) ;

  // Print priority matrix to standard output
  virtual void PrintState() const ;
  
  // Update priority matrix based on last aribtration result
  virtual void UpdateState() ; 

  // Arbitrate amongst requests. Returns winning input and 
  // updates pointers to metadata when valid pointers are passed
  virtual int Arbitrate( int* id = 0, int* pri = 0) ;

} ;

#endif
