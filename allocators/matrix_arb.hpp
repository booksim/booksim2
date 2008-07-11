// ----------------------------------------------------------------------
//
//  Matrix: Matrix Arbiter
//
// ----------------------------------------------------------------------
#ifndef _MATRIX_ARB_HPP_
#define _MATRIX_ARB_HPP_
#include <iostream>
using namespace std ;

// ----------------------------------------------------------------------
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:10:51 $
//  $Id: matrix_arb.hpp,v 1.1 2007/05/17 17:10:51 jbalfour Exp $
// ----------------------------------------------------------------------

class MatrixArbiter {

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
  int* _matrix ;
  int  _Priority( int row, int column ) const ;
  void _SetPriority( int row, int column, int val ) ;

public:

  // Constructors
  MatrixArbiter() ;
  ~MatrixArbiter();
  void Init( int size ) ; 

  // Print priority matrix to standard output
  void PrintMatrix() const ;
  
  // Register request with arbiter
  void AddRequest( int input, int id, int pri ) ;

  // Update priority matrix based on last aribtration result
  void UpdateMatrix() ; 

  // Arbitrate amongst requests. Returns winning input and 
  // updates pointers to metadata when valid pointers are passed
  int Arbitrate( int* id = 0, int* pri = 0) ;

} ;

#endif
