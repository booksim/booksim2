// $Id$

/*
Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this 
list of conditions and the following disclaimer in the documentation and/or 
other materials provided with the distribution.
Neither the name of the Stanford University nor the names of its contributors 
may be used to endorse or promote products derived from this software without 
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _MECSCREDITFORWARDER_HPP_
#define _MECSCREDITFORWARDER_HPP_

#include "network.hpp"
#include "module.hpp"
#include "credit.hpp"
#include "channel.hpp"
#include "config_utils.hpp"
#include <queue>
#include <assert.h>

typedef Channel<Credit> CreditChannel;


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
