#ifndef _MECSCHANNELS_HPP_
#define _MECSCHANNELS_HPP_

#include "MECSForwarder.hpp"

class MECSChannels: public Module{

protected:
  int source_router;
  int drop_count;
  //1 or more flit drop off points
  MECSForwarder** drops;

public:
  MECSChannels(Module* parent, string name, int source, int direction, int stops);
  
  int GetSize(){return drop_count;}
  MECSForwarder* GetForwarder(int i){ assert(i<drop_count);return drops[i];}
  void AddChannel(FlitChannel* chan, int drop);
  void ReadInputs();
  void WriteOutputs();
  
};

#endif
