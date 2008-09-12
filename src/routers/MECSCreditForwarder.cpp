#include "MECSCreditForwarder.hpp"

MECSCreditForwarder::MECSCreditForwarder(Module* parent, string name, int router)
  :Module( parent, name ){

  location = router;
  cc = 0;
  cred_in = 0;
  cred_out = 0;
}


void MECSCreditForwarder::AddInChannel(CreditChannel* backchannel){
  cred_in = backchannel;
}
void MECSCreditForwarder::AddOutChannel( CreditChannel* backchannel){
  cred_out = backchannel;
}


void MECSCreditForwarder::ReadInputs(){
  Credit *c = cred_in->ReceiveCredit();
  if(c){
    assert(c->dest_router>=0);
    if(c->dest_router ==location){
      credit_queue.push(c);
      assert(credit_queue.size()<100); //if this trips, soemthign is wrong
      cc = 0; //terminate if reached the destination
    } else{
      cc = c;
    }
  }
}

void MECSCreditForwarder::WriteOutputs(){
  //always send, if the channels exists 
  if(cred_out){
    cred_out->SendCredit(cc);
    cc = 0; 
  }
}
