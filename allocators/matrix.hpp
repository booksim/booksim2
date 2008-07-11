// ----------------------------------------------------------------------
//
//  Matrix: Matrix Arbiter based Allocator
//
// ----------------------------------------------------------------------
#include "allocator.hpp"
#include "matrix_arb.hpp"
#include <list>

// ----------------------------------------------------------------------
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:10:51 $
//  $Id: matrix.hpp,v 1.1 2007/05/17 17:10:51 jbalfour Exp $
// ----------------------------------------------------------------------
class Matrix : public Allocator {

  int  _num_vcs ;
  int* _matched ;

  MatrixArbiter* _input_arb ;
  MatrixArbiter* _output_arb ;

  MatrixArbiter* _spec_input_arb ;
  MatrixArbiter* _spec_output_arb ;

  list<sRequest>* _in_req ;
  list<sRequest>* _out_req ;

public:
  
  Matrix( const Configuration& config, Module* parent,
	  const string& name, int inputs, int outputs ) ;
  
  ~Matrix() ;

  //
  // Allocator Interface
  //
  virtual void Clear() ;
  virtual int  ReadRequest( int in, int out ) const ;
  virtual bool ReadRequest( sRequest& req, int in, int out ) const ;
  virtual void AddRequest( int in, int out, int label = 1, 
			   int in_pri = 0, int out_pri = 0 ) ;
  virtual void RemoveRequest( int in, int out, int label = 1 ) ;
  virtual void Allocate() ;
  virtual void PrintRequests() const ;

} ;
