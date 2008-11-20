// $Id$
#ifndef _MECSCOMBINER_HPP_
#define _MECSCOMBINER_HPP_

#include "MECSForwarder.hpp"
#include "flitchannel.hpp"
#include "flit.hpp"

class MECSCombiner :public Module{

protected:
  int direction;
  int router;
  //list of input channels
  vector<MECSForwarder*> inputs;
  //to router
  FlitChannel* chan_out;
  //multi-flit packet
  bool seen_head;
  //for multi-flit packet, which forwarder to read from to preserve packet
  int location;
  
public:
  MECSCombiner(Module* parent, string name, int dir, int r);
  void AddOutChannel(FlitChannel* chan){
    chan_out = chan;
  }
  void AddForwarder(MECSForwarder* forwarder){
    inputs.push_back(forwarder);
  }
  void ReadInputs();
};
#endif
