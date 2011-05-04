#ifndef _FLOWBUFFER_H_
#define _FLOWBUFFER_H_
#include "flit.hpp"
#include "reservation.hpp"
#include <map>
#include <queue>
#include "globals.hpp"

#define FLOW_STAT_SPEC 0
#define FLOW_STAT_NACK 1
#define FLOW_STAT_WAIT 2
#define FLOW_STAT_NORM 3
#define FLOW_STAT_NORM_READY 4
#define FLOW_STAT_SPEC_READY 5
#define FLOW_STAT_NOT_READY 6
#define FLOW_STAT_FINAL_NOT_READY 7
#define FLOW_STAT_LIFETIME 9

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

  bool ack(int sn);
  bool nack(int sn);
  void grant(int time);

  bool eligible();
  bool receive_ready();
  bool send_norm_ready();
  bool send_spec_ready();
  bool done();


  Flit* send();
  Flit* receive();
  Flit* front();

  void update_transition();
  void update_stats();

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

  //these variables for stat collection
  int _spec_outstanding;
  vector<int> _stats;
  int _no_retransmit_loss;
};

#endif
