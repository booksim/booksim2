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

/*MECSForwarder
 *
 *The "drop-off" points along the MECSChannels
 *  When a flit arrives, check for its destination
 *  match = insert into flit queue and 0 gets forwarded 
 *  no match, flit gets forwarded
 */

#include "MECSForwarder.hpp"
#include "globals.hpp"

MECSForwarder::MECSForwarder(Module* parent, string name, int router)
  :Module( parent, name ){
  location = router;
  chan_in = 0;
  chan_out = 0;
  ff  = 0;
}


void MECSForwarder::AddInChannel(FlitChannel* channel){
  chan_in = channel;
}
void MECSForwarder::AddOutChannel(FlitChannel* channel){
  chan_out = channel;
}

//this function is in mecs.cpp
int mecs_transformation(int dest);

void MECSForwarder::ReadInputs(){
  Flit *f = chan_in->Receive();
  if(f){
    if(f->watch){
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		  <<f->id<<" at Forwarder "<<location<<endl;
    }
    //shoudl the flit be dropped off here?
    if((int)(mecs_transformation(f->intm)/gK) == location ||(int)(mecs_transformation(f->dest)/gK) == location){
      flit_queue.push(f);
      assert(flit_queue.size()<100); //if this trips, soemthign is wrong
      ff = 0; //terminate if reached the destination
      if(f->watch){
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		    <<f->id<<" halted at Forwarder "<<location<<endl;
      }
    } else {
      ff = f;
      if(f->watch){
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		    <<f->id<<" moved at Forwarder "<<location<<endl;
      }
    }
  }
}

void MECSForwarder::WriteOutputs(){
  //always send, if the channels exists 
  if(chan_out){
    chan_out->Send(ff);
    ff = 0;
  }

}
