// $Id$
#ifndef _PIM_HPP_
#define _PIM_HPP_

#include "allocator.hpp"

class PIM : public DenseAllocator {
  int _PIM_iter;

  int *_grants;
public:
  PIM( Module *parent, const string& name,
       int inputs, int outputs, int iters );

  ~PIM( );

  void Allocate( );
};

#endif
