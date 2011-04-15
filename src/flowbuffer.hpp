#ifndef _FLOWBUFFER_H_
#define _FLOWBUFFER_H_
#include "flit.hpp"
#include "reservation.hpp"
#include <list>
struct flow{
  bool spec_sent;
  int flid;
  int vc;
  int rtime;
  int flow_size;
  bool collect;
  int create_time;
};

class FlowBuffer{
public:
  FlowBuffer();
  ~FlowBuffer();

  Flit* front();
  flow* front_flow();
  Flit* back();
  Flit* get_spec(int flid);
  int size();
  bool empty();
  bool full();
  void inc_spec();
  void push_flow(flow* f);
  void pop_flow();
  void push_back(Flit * f);
  void pop_front();
  bool remove_packet();
  void reset();  
  void nack();

  Flit** _flit_buffer;
  list<flow* > _flow_buffer;
  int _head;
  int _tail;
  int _size;
  int _capacity;			       
  int _status;
  int _spec_position;
  int _spec_sent;
};

#endif
