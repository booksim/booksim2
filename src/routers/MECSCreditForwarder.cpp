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
  Credit *c = cred_in->Receive();
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
    cred_out->Send(cc);
    cc = 0; 
  }
}
