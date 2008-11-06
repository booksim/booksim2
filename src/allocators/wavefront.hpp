#ifndef _WAVEFRONT_HPP_
#define _WAVEFRONT_HPP_

#include "allocator.hpp"

class Wavefront : public DenseAllocator {
  int _square;
  int _pri;
  int _num_requests;
  int _last_in;
  int _last_out;

public:
  Wavefront( Module *parent, const string& name,
	     int inputs, int outputs );
  
  void AddRequest( int in, int out, int label = 1, 
		   int in_pri = 0, int out_pri = 0 );
  void Allocate( );
};

#endif
