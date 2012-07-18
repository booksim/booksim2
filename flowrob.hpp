#ifndef __FLOW_ROB__H__
#define __FLOW_ROB__H__

#include "booksim.hpp"
#include "flit.hpp"
#include "reservation.hpp"
#include <map>
#include <set>
class FlowROB{

public:
  FlowROB(int flow_size);
  ~FlowROB();

  Flit* insert(Flit* f);
  bool done();
  bool sn_check(int sn);
  int range();

  int _flow_creation_time;
  int _max_reorder;
  int _flow_size; //this is the max
  int _flid;
  int _status;
  set<int> _pid;
  set<int> _rob;
  int _sn_ceiling;

};

#endif
