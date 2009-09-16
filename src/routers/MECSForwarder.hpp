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

#ifndef _MECSFORWARDER_HPP_
#define _MECSFORWARDER_HPP_

#include "network.hpp"
#include "module.hpp"
#include "flit.hpp"
#include "flitchannel.hpp"
#include "flit.hpp"
#include "config_utils.hpp"
#include <queue>
#include <assert.h>


class MECSForwarder : public Module {
  
protected:
  //which router
  int location;

  queue<Flit*> flit_queue; //set of flits for this router 
  Flit *ff; //flit to be forwarded down the channel 

  FlitChannel* chan_in;
  
  FlitChannel* chan_out;

public:

  MECSForwarder(Module* parent, string name, int router);
  void AddInChannel(FlitChannel* channel);
  void AddOutChannel(FlitChannel* channel);
			
  void ReadInputs();

  Flit* PeekFlit(){ 
    if(flit_queue.size()==0){
      return 0;
    }
    return flit_queue.front();
  }

  Flit* ReceiveFlit(){
    Flit* f = 0;
    if (flit_queue.size()>0){
      f =  flit_queue.front(); 
      flit_queue.pop(); 
    }
    return f;
  }


  void WriteOutputs();
  int GetLocation () {return location;}
  int FlitQueueSize(){ return flit_queue.size();}

};
#endif
