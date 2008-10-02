// ----------------------------------------------------------------------
//
//  SeparableAllocator: Separable Allocator
//
// ----------------------------------------------------------------------

#ifndef _SEPARABLE_HPP_
#define _SEPARABLE_HPP_

#include "allocator.hpp"
#include "arbiter.hpp"
#include <assert.h>

#include <list>

class SeparableAllocator : public Allocator {

  int  _num_vcs ;
  int* _matched ;

  Arbiter* _input_arb ;
  Arbiter* _output_arb ;

  Arbiter* _spec_input_arb ;
  Arbiter* _spec_output_arb ;

  list<sRequest>* _in_req ;
  list<sRequest>* _out_req ;

public:
  
  SeparableAllocator( const Configuration& config, Module* parent,
		      const string& name, const string &alloc_type,
		      int inputs, int outputs ) ;
  
  virtual ~SeparableAllocator() ;

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

#endif
