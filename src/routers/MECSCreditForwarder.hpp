#ifndef _MECSCREDITFORWARDER_HPP_
#define _MECSCREDITFORWARDER_HPP_

#include "network.hpp"
#include "module.hpp"
#include "credit.hpp"
#include "creditchannel.hpp"
#include "config_utils.hpp"
#include <queue>


class MECSCreditForwarder : public Module {
  
protected:
  int location;
  
  queue<Credit*> credit_queue;//current
  Credit *cc;//tobe forwarded

  CreditChannel* cred_in;
  
  CreditChannel* cred_out;

public:

  MECSCreditForwarder(Module* parent, string name, int router);
  void AddInChannel( CreditChannel* backchannel);
  void AddOutChannel(CreditChannel* backchannel);
			
  void ReadInputs();

  Credit* PeekCredit(){ 
    if(credit_queue.size() == 0){
      return 0;
    }
    return credit_queue.front();
  }
  Credit* ReceiveCredit(){
    Credit* c = 0;
    if(credit_queue.size()>0){
      c = credit_queue.front(); 
      credit_queue.pop();
    } 
    return c;
  }

  void WriteOutputs();
  int GetLocation () {return location;}
  int CreditQueueSize(){return credit_queue.size();}

};
#endif
