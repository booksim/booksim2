#ifndef _MECSCREDITCHANNELS_HPP_
#define _MECSCREDITCHANNELS_HPP_

#include "MECSCreditForwarder.hpp"

class MECSCreditChannels: public Module{

protected:
  int source_router;
  int drop_count;
  //1 or more drop off points
  MECSCreditForwarder** drops;

public:
  MECSCreditChannels(Module* parent, string name, int source, int direction, int stops);
  
  int GetSize(){return drop_count;}
  MECSCreditForwarder* GetForwarder(int i){ assert(i<drop_count);return drops[i];}
  void AddChannel(CreditChannel* cred, int drop);
  void ReadInputs();
  void WriteOutputs();
  
};

#endif
