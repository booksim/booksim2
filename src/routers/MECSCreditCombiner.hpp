#ifndef _MECSCREDITCOMBINER_HPP_
#define _MECSCREDITCOMBINER_HPP_

#include "MECSCreditForwarder.hpp"
#include "creditchannel.hpp"

class MECSCreditCombiner :public Module{

protected:
  int direction;
  int router;
  //select which credit channel input round robin style
  int round_robin;
  //the channel inputs
  vector<MECSCreditForwarder*> inputs;
  //output into the router
  CreditChannel* cred_out;
  
  
public:
  MECSCreditCombiner(Module* parent, string name, int dir, int r);
  void AddOutChannel(CreditChannel* cred){
    cred_out = cred;
  }
  void AddForwarder(MECSCreditForwarder* forwarder){
    inputs.push_back(forwarder);
  }
  void ReadInputs();
};
#endif
