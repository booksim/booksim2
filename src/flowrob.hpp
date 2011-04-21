#ifndef __FLOW_ROB__H__
#define __FLOW_ROB__H__

#include "booksim.hpp"
#include "flit.hpp"
#include "reservation.hpp"
#include <map>
#include <set>
class FlowROB{

public:
  FlowROB();
  ~FlowROB();

  Flit* insert(Flit* f);
  bool done();


  int _flow_size;
  int _flid;
  int _status;
  set<int> _pid;
  set<int> _rob;

};

#endif
