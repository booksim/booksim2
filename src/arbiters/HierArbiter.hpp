#ifndef __HIERARBITER_H__
#define __HIERARBITER_H__

#include "arbiter.hpp"
#include "weighted_rr_arb.hpp"
#include "roundrobin_arb.hpp"

#include <vector>

class HierArbiter{
protected:
  int _leaf_size;
  int _root_size;
  
  vector<Arbiter*> _leaf_arb;
  Arbiter* _root_arb;
  
  int _port;
  int _vc;
  
public:
  int _num_reqs;
  HierArbiter(int leaf, int root,
			 const string& _leaf_arb_type,
			 const string& _root_arb_type);
  ~HierArbiter();
  
  void AddRequest(int input, int id, int pri);
  void UpdateState();
  int Arbitrate( int* id = 0, int* pri = 0 );
  void Clear();

};

#endif
