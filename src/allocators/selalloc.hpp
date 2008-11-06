#ifndef _SELALLOC_HPP_
#define _SELALLOC_HPP_

#include "allocator.hpp"

class SelAlloc : public SparseAllocator {
  int _iter;

  int *_grants;
  int *_aptrs;
  int *_gptrs;

public:
  SelAlloc( Module *parent, const string& name,
	    int inputs, int outputs, int iters );
  ~SelAlloc( );

  void Allocate( );
};

#endif 
