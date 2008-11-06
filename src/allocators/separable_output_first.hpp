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
  
  SeparableOutputFirstAllocator( Module* parent, const string& name, int inputs,
				 int outputs, const string& arb_type ) ;
  
  virtual void Allocate() ;

} ;

#endif
