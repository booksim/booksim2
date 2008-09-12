#ifndef _MECSFORWARDER_HPP_
#define _MECSFORWARDER_HPP_

#include "network.hpp"
#include "module.hpp"
#include "flit.hpp"
#include "flitchannel.hpp"
#include "flit.hpp"
#include "config_utils.hpp"
#include <queue>


class MECSForwarder : public Module {
  
protected:
  //which router
  int location;

  queue<Flit*> flit_queue; //set of flits for this router 
  Flit *ff; //flit to be forwarded down the channel 

  FlitChannel* chan_in;
  
  FlitChannel* chan_out;

public:

  MECSForwarder(Module* parent, string name, int router);
  void AddInChannel(FlitChannel* channel);
  void AddOutChannel(FlitChannel* channel);
			
  void ReadInputs();

  Flit* PeekFlit(){ 
    if(flit_queue.size()==0){
      return 0;
    }
    return flit_queue.front();
  }

  Flit* ReceiveFlit(){
    Flit* f = 0;
    if (flit_queue.size()>0){
      f =  flit_queue.front(); 
      flit_queue.pop(); 
    }
    return f;
  }


  void WriteOutputs();
  int GetLocation () {return location;}
  int FlitQueueSize(){ return flit_queue.size();}

};
#endif
