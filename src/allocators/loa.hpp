#ifndef _LOA_HPP_
#define _LOA_HPP_

#include "allocator.hpp"

class LOA : public DenseAllocator {
  int *_counts;
  int *_req;

  int *_rptr;
  int *_gptr;

public:
  LOA( Module *parent, const string& name,
       int inputs, int outputs );
  ~LOA( );

  void Allocate( );
};

#endif
