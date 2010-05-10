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

/*MECSCominber
 *
 *Combines the flit drop-off points from channels of a given direction
 *
 *   Flits are selected from the drop-off points using age based arbitration
 *
 */
#include "MECSCombiner.hpp"
#include "globals.hpp"

MECSCombiner::MECSCombiner(Module* parent, string name, int dir, int r)
  : Module(parent, name){
  direction = dir;
  router = r;
  seen_head = false;
  location = -1;
}

void MECSCombiner::ReadInputs(){
  int time = -1;

  Flit *f = 0;

  if(!seen_head){
    //age based flit select
    for(int i = 0; i<inputs.size(); i++){
      f = inputs.at(i)->PeekFlit();
      if(f){
	if(time == -1 || (f->time)<time){
	  location = i;
	  time = f->time;
	}
      }
    }
  }
  if(location!=-1 && inputs.at(location)->FlitQueueSize()!=0){
    f = inputs.at(location)->ReceiveFlit();
    assert(f);
    if(f->watch){
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		  <<f->id<<" load into router "<<router<<endl;
    }
  }
  if(f){
    
    if(f->head){
      seen_head = true;
      
    }
    if(f->tail){
      seen_head = false;
      location = -1;
    }
  }

  // always send even if 0
  chan_out->Send(f);

}
