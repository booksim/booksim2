// ----------------------------------------------------------------------
//
//  SeparableOutputFirstAllocator: Separable Output-First Allocator
//
// ----------------------------------------------------------------------

#ifndef _SEPARABLE_OUTPUT_FIRST_HPP_
#define _SEPARABLE_OUTPUT_FIRST_HPP_

#include "separable.hpp"

class SeparableOutputFirstAllocator : public SeparableAllocator {

public:
  
  SeparableOutputFirstAllocator( const Configuration& config, Module* parent,
				 const string& name, const string& arb_type,
				 int inputs, int outputs ) ;
  
  virtual void Allocate() ;

} ;

#endif
