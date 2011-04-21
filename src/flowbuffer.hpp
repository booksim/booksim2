#ifndef _FLOWBUFFER_H_
#define _FLOWBUFFER_H_
#include "flit.hpp"
#include "reservation.hpp"
#include <map>
#include <queue>
#include "globals.hpp"
struct flow{
  int flid;
  int vc;
  int rtime;
  int flow_size;
  bool collect;
  int create_time;
  queue<Flit*> data;
};

class FlowBuffer{
public:
  FlowBuffer(int id, int size, bool reservation_enabled, flow* fl);
  ~FlowBuffer();

  void ack(int sn);
  void nack(int sn);
  void grant(int time);

  bool receive_ready();
  bool send_norm_ready();
  bool send_spec_ready();
  bool done();

  Flit* send();
  Flit* receive();
  Flit* front();

  map<int, int> _flit_status;
  map<int, Flit*> _flit_buffer;
  flow* fl;
  Flit* _reservation_flit;
  int _capacity;			       
  int _id;

  int _status;
  bool _tail_sent; //if the last flit sent was a tail
  bool _tail_received; //tail received from the node
  int _last_sn;
  int _guarantee_sent;
  int _received;
  int _ready;
  bool _spec_sent;
  int _vc;

  bool _watch;
};

#endif
