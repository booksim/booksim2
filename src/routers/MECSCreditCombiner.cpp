/*MECSCreditCombiner
 *
 *Muxes the multiple credit channle inputs from a given direction
 *
 * Round robin style read from the forwarder queues and send it to the router
 *
 */

#include "MECSCreditCombiner.hpp"

MECSCreditCombiner::MECSCreditCombiner(Module* parent, string name, int dir, int r)
  : Module(parent, name){
  round_robin = 0;
  direction = dir;
  router = r;
}

void MECSCreditCombiner::ReadInputs(){
  int time = -1;
  int location = -1;
  Credit *c = 0;

  //prevent circle around
  int begin_value = round_robin; 

  do{
    //  round robin based credit select
    if(inputs.size()!=0){
      c = inputs.at(round_robin)->ReceiveCredit();  
    }
    
    cred_out->SendCredit(c);
    round_robin++;
    if(round_robin>=inputs.size()){
      round_robin = 0;
    } 
  } while(round_robin!=begin_value  && c==0);
}
