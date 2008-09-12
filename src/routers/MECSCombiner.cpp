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
}

void MECSCombiner::ReadInputs(){
  int time = -1;
  int location = -1;
  Flit *f = 0;
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
  if(location!=-1){
    f = inputs.at(location)->ReceiveFlit();
    assert(f);
    if(f->watch){
      cout<<f->id<<" load into router "<<router<<endl;
    }
  }

  // always send even if 0
  chan_out->SendFlit(f);

}
