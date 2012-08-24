#ifndef _OUTPUT_BUFFER_HPP_
#define _OUTPUT_BUFFER_HPP_

#include <vector>
#include <queue>

#include "module.hpp"
#include "flit.hpp"
#include "config_utils.hpp"

class OutputBuffer: public Module {

protected:
  queue<Flit*>  _control_buffer;
  vector< queue<Flit*> > _buffers;
  //for age based output arbitration
  vector< queue<int> > _buffer_time;
  //otuput buffer allocated by packets
  vector<int> _buffer_slots;

  //denies speculative VC allocation is nonspeculative packets is available
  int _nonspec_slots;
  //output buffer is serviced packet contingously
  int _last_buffer;
  
  bool _control_tail;
  //don't don't break up buffer;
  vector<bool> _buffer_tail;
  int _watch;

  //total umber of pending packets this buffer;
  int  _total;
public:
  OutputBuffer( const Configuration& config,
		Module *parent, const string& name);
  ~OutputBuffer();

  Flit* SendFlit();
  void QueueControlFlit(Flit* f);
  void QueueFlit(int vc, Flit* f);

  int ControlSize();
  int Size(int vc);
  inline int Total(){
    return _total;
  }
  int Data(){
    int sum = 0;
    for(size_t i = 0; i<_buffers.size(); i++){
      sum+=_buffers[i].size();
    }
    return sum;
  }


  bool Full(int vc);
  void Take(int vc);
  void Release(int vc);
  bool ControlFull();

  static int _bubbles;
  static int _vcs;
  static vector<int> _buffer_capacity;
  static int _control_capacity;
  static vector<bool> _spec_vc;
  static void SetSpecVC(int vc, int size=1);
};
#endif //_OUTPUT_BUFFER_HPP_
