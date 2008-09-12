/*MECSCreditChannel
 *
 *A MECSChannel is composed of 1 more more normal channels
 *  bridging the normal channels are forwarders
 *
 *
 */

#include "MECSCreditChannel.hpp"

MECSCreditChannels::MECSCreditChannels(Module* parent, string name, int source, int direction, int stops)
  :Module(parent,name){

  source_router = source;
  drop_count = stops;

  drops = (MECSCreditForwarder**)malloc(drop_count*sizeof(MECSCreditForwarder*));
  //each forwarder is assigned a router number based on the current router number
  //and direction
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
   
    drops[i-1] = new MECSCreditForwarder(this, name, router);
  }

}

void MECSCreditChannels::AddChannel(CreditChannel* cred, int drop){

  //add the first channel doens't have to "add outchannel"
  if(drop == 0){
  } else{
    drops[drop-1]->AddOutChannel(cred);
  }
  drops[drop]->AddInChannel(cred);
}

void MECSCreditChannels::ReadInputs(){

  for(int i = 0; i<drop_count; i++){
    drops[i]->ReadInputs();
  }
}
void MECSCreditChannels::WriteOutputs(){

  for(int i = 0; i<drop_count; i++){
    drops[i]->WriteOutputs();
  }
}
