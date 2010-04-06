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
  Credit *c = 0;

  //prevent circle around
  int begin_value = round_robin; 

  do{
    //  round robin based credit select
    if(inputs.size()!=0){
      c = inputs.at(round_robin)->ReceiveCredit();  
    }
    
    cred_out->Send(c);
    round_robin++;
    if(round_robin>=inputs.size()){
      round_robin = 0;
    } 
  } while(round_robin!=begin_value  && c==0);
}
