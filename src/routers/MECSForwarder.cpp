/*MECSForwarder
 *
 *The "drop-off" points along the MECSChannels
 *  When a flit arrives, check for its destination
 *  match = insert into flit queue and 0 gets forwarded 
 *  no match, flit gets forwarded
 */

#include "MECSForwarder.hpp"

MECSForwarder::MECSForwarder(Module* parent, string name, int router)
  :Module( parent, name ){
  location = router;
  chan_in = 0;
  chan_out = 0;
  ff  = 0;
}


void MECSForwarder::AddInChannel(FlitChannel* channel){
  chan_in = channel;
}
void MECSForwarder::AddOutChannel(FlitChannel* channel){
  chan_out = channel;
}

//this function is in mecs.cpp
int mecs_transformation(int dest);

void MECSForwarder::ReadInputs(){
  Flit *f = chan_in->ReceiveFlit();
  if(f){
    if(f->watch){
      cout<<f->id<<" at Forwarder "<<location<<endl;
    }
    //shoudl the flit be dropped off here?
    if((int)(mecs_transformation(f->intm)/gK) == location ||(int)(mecs_transformation(f->dest)/gK) == location){
      flit_queue.push(f);
      assert(flit_queue.size()<100); //if this trips, soemthign is wrong
      ff = 0; //terminate if reached the destination
      if(f->watch){
	cout<<f->id<<" halted at Forwarder "<<location<<endl;
      }
    } else {
      ff = f;
      if(f->watch){
	cout<<f->id<<" moved at Forwarder "<<location<<endl;
      }
    }
  }
}

void MECSForwarder::WriteOutputs(){
  //always send, if the channels exists 
  if(chan_out){
    chan_out->SendFlit(ff);
    ff = 0;
  }

}
