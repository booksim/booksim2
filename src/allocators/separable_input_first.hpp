// ----------------------------------------------------------------------
//
//  SeparableInputFirstAllocator: Separable Input-First Allocator
//
// ----------------------------------------------------------------------

#ifndef _SEPARABLE_INPUT_FIRST_HPP_
#define _SEPARABLE_INPUT_FIRST_HPP_

#include "separable.hpp"

class SeparableInputFirstAllocator : public SeparableAllocator {

public:
  
  SeparableInputFirstAllocator( Module* parent, const string& name, int inputs,
				int outputs, const string& arb_type ) ;
  
  virtual void Allocate() ;

} ;

#endif
