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
  
  SeparableInputFirstAllocator( const Configuration& config, Module* parent,
				const string& name, const string& arb_type,
				int inputs, int outputs ) ;
  
  virtual void Allocate() ;

} ;

#endif
