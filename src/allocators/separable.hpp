// $Id$
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
  
protected:

  int* _matched ;

  Arbiter** _input_arb ;
  Arbiter** _output_arb ;

  list<sRequest>* _requests ;

public:
  
  SeparableAllocator( Module* parent, const string& name, int inputs,
		      int outputs, const string& arb_type ) ;
  
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
  virtual void Allocate() = 0 ;
  virtual void PrintRequests() const ;

} ;

#endif
