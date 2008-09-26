/*MECSCominber
 *
 *Combines the flit drop-off points from channels of a given direction
 *
 *   Flits are selected from the drop-off points using age based arbitration
 *
 */
#include "MECSCombiner.hpp"

MECSCombiner::MECSCombiner(Module* parent, string name, int dir, int r)
  : Module(parent, name){
  direction = dir;
  router = r;
  seen_head = false;
  location = -1;
}

void MECSCombiner::ReadInputs(){
  int time = -1;

  Flit *f = 0;

  if(!seen_head){
    //age based flit select
    for(int i = 0; i<inputs.size(); i++){
      f = inputs.at(i)->PeekFlit();
      if(f){
	if(time == -1 || (f->time)<time){
	  location = i;
	  time = f->time;
	}
      }
    }
  }
  if(location!=-1 && inputs.at(location)->FlitQueueSize()!=0){
    f = inputs.at(location)->ReceiveFlit();
    assert(f);
    if(f->watch){
      cout<<f->id<<" load into router "<<router<<endl;
    }
  }
  if(f){
    
    if(f->head){
      seen_head = true;
      
    }
    if(f->tail){
      seen_head = false;
      location = -1;
    }
  }

  // always send even if 0
  chan_out->SendFlit(f);

}
