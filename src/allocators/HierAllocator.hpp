#ifndef __HIERALLOCATOR_H__
#define __HIERALLOCATOR_H__
#include "allocator.hpp"
#include "HierArbiter.hpp"

#include <vector>

class HierAllocator : public SparseAllocator {
  
protected:
  
  int _input_size;
  int _input_leaf_size;
  int _input_root_size;
  
  int _output_size;
  int _output_leaf_size;
  int _output_root_size;
  
public:
  vector<HierArbiter*> _input_arb ;
  vector<HierArbiter*> _output_arb ;
  
  HierAllocator(Module* parent, const string& name,
		int input_leaf,
		int input_root,
		int output_leaf,
		int output_root,
		const string& input_arb_type,
		const string& input_root_arb_type,
		const string& output_arb_type,
		const string& output_root_arb_type) ;
  
  virtual ~HierAllocator() ;
  virtual void Allocate() ;
  virtual void Clear() ;
  

} ;

#endif // __HIERALLOCATOR_H__
