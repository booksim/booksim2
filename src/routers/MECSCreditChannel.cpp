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
