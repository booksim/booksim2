/*MECSChannel
 *
 * A MECSChannel is composed of several normal flit channels 
 * bridging the normal channels are forwarders that drop flits off at routers
 */

#include "MECSChannels.hpp"

MECSChannels::MECSChannels(Module* parent, string name, int source, int direction, int stops)
  :Module(parent,name){

  source_router = source;
  drop_count = stops;

  drops = (MECSForwarder**)malloc(drop_count*sizeof(MECSForwarder*));
  //router nunmbers are determined based ont he current router and
  //the channel direction
  for(int i =1; i<drop_count+1; i++){
    int router = source_router;
    switch(direction){
    case 0:
      router = source_router-i*gK;
      break;
    case 1:
      router = source_router+i;
      break;
    case 2:
      router = source_router+i*gK;
      break;
    case 3:
      router = source_router-i;
      break;
    default:
      cout<<direction<<endl;
      assert(false);
    }
   
    drops[i-1] = new MECSForwarder(this, name, router);
  }

}

void MECSChannels::AddChannel(FlitChannel* chan, int drop){

  //add the first channel doens't have to "add outchannel"
  if(drop == 0){
  } else{
    drops[drop-1]->AddOutChannel(chan);
  }
  drops[drop]->AddInChannel(chan);
}

void MECSChannels::ReadInputs(){

  for(int i = 0; i<drop_count; i++){
    drops[i]->ReadInputs();
  }
}
void MECSChannels::WriteOutputs(){

  for(int i = 0; i<drop_count; i++){
    drops[i]->WriteOutputs();
  }
}
