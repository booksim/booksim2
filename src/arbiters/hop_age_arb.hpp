#ifndef HOP_AGE_ARB_HPP
#define HOP_AGE_ARB_HPP

#include "arbiter.hpp"
#include "roundrobin_arb.hpp"
#include <cassert>

// Switch allocation: priority=hop_count, vc_priority_donation=0.
// The caller supplies the front flit's injection time with SetRequestTime.
class HopAgeArbiter : public Arbiter {
private:
  int _pointer;

public:
  HopAgeArbiter(Module *parent, const string &name, int size);
  virtual int Arbitrate(int *id = 0, int *pri = 0);
  virtual void UpdateState();
  virtual void PrintState() const;

  // Higher hops, then earlier injection, then the ordinary cyclic order.
  static bool Supersedes(int input1, int hops1, int time1,
                         int input2, int hops2, int time2,
                         int pointer, int size)
  {
    assert(time1 >= 0 && time2 >= 0);
    if(hops1 != hops2) return hops1 > hops2;
    if(time1 != time2) return time1 < time2;
    return RoundRobinArbiter::Supersedes(input1, 0, input2, 0, pointer, size);
  }
};

#endif
