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

/*MECSRouter
 *
 *Router used exclulsively for the MECS topology
 * Built on the foundation of a iq_router, but restrict input and output 
 * of the iq_router using forwarders and combiners
 * Only 4 + concentration input output port, all channels from a direction
 * is combined through a combiner
 * 4 flit output channels and 4 cred output channels
 * 
 * Due to the nature of the MECS, credit channels are "reversed"
 *
 * Currently cannot suport multiple flit per packet, the combiners need to be able to organize
 *
 * Direction are always  0N 1E 2S 3W
 */
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <assert.h>

#include "MECSRouter.hpp"

MECSRouter::MECSRouter(const Configuration& config,
		       Module *parent, string name, int id,
		       int inputs, int outputs ) 
  : Router( config, parent, name,  id, inputs, outputs ){


  sub_router = new IQRouterBaseline(config, this, name,  id, inputs, outputs );
  

  n  = new MECSCombiner(this, name, 0, id);
  e  = new MECSCombiner(this, name, 1, id);
  s  = new MECSCombiner(this, name, 2, id);
  w  = new MECSCombiner(this, name, 3, id);

  n_credit = new MECSCreditCombiner(this, name, 0, id);
  e_credit = new MECSCreditCombiner(this, name, 1, id);
  s_credit = new MECSCreditCombiner(this, name, 2, id);
  w_credit = new MECSCreditCombiner(this, name, 3, id);
}


MECSRouter::~MECSRouter(){
  delete n,e,s,w;
  delete n_credit,e_credit,s_credit,w_credit;
  delete sub_router;
}

void MECSRouter::AddInputChannel( FlitChannel *channel, CreditChannel *backchannel)
{
  _input_channels->push_back( channel );
  _input_credits->push_back( backchannel );

  //need to properly set source and sink in the future
  if(channel){
    channel->SetSink( this ) ;
  }
}

void MECSRouter::AddInputChannel( FlitChannel *channel, CreditChannel *backchannel , int dir)
{
  _input_channels->push_back( channel );
  _input_credits->push_back( backchannel );

 //need to properly set source and sink in the future
  if(channel){
    channel->SetSink( this ) ;
    switch(dir){
    case 0:
      n->AddOutChannel(channel);
      n_credit->AddOutChannel(backchannel);
      break;
    case 1:
      e->AddOutChannel(channel);
      e_credit->AddOutChannel(backchannel);
      break;
    case 2:
      s->AddOutChannel(channel);
      s_credit->AddOutChannel(backchannel);
      break;
    case 3:
      w->AddOutChannel(channel);
      w_credit->AddOutChannel(backchannel);
      break;
    default:
      assert(false);
    }
  }
}

void MECSRouter::AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  _output_channels->push_back( channel );
  _output_credits->push_back( backchannel );
  
  //need to properly set source and sink in the future
  _channel_faults->push_back( false );
  if(channel)
    channel->SetSource( this ) ;
}

//the MECS channels are constructed externally  
//at this point the MECS channels are already "connected with the routers"
void MECSRouter::AddMECSChannel(MECSChannels *chan,int dir){
  switch(dir){
  case 0:
    n_channel = chan;
    break;
  case 1:
    e_channel = chan;
    break;
  case 2:
    s_channel = chan;
      break;
  case 3:
    w_channel = chan;
    break;
  default:
    assert(false);
  }
}

//the MECS channels are constructed externally  
void MECSRouter::AddMECSCreditChannel(MECSCreditChannels *chan,int dir){
  switch(dir){
  case 0:
    n_credit_channel = chan;
    break;
  case 1:
    e_credit_channel = chan;
    break;
  case 2:
    s_credit_channel = chan;
      break;
  case 3:
    w_credit_channel = chan;
    break;
  default:
    assert(false);
  }
}

void MECSRouter::AddForwarder(MECSForwarder* forwarder, int dir){ 
  //this dir need to be negated north = south, west = east

  switch(dir){
  case 0: //south
    s->AddForwarder(forwarder);
    break;
  case 1: //west
    w->AddForwarder(forwarder);
    break;
  case 2: //north
    n->AddForwarder(forwarder);
    break;
  case 3: //east
    e->AddForwarder(forwarder);
    break;
  default:
    assert(false);
  }
}

void MECSRouter::AddCreditForwarder(MECSCreditForwarder* forwarder, int dir){ 
  //this dir need to be negated north = south, west = east
  switch(dir){
  case 0: //south
    s_credit->AddForwarder(forwarder);
    break;
  case 1: //west
    w_credit->AddForwarder(forwarder);
    break;
  case 2: //north
    n_credit->AddForwarder(forwarder);
    break;
  case 3:
    e_credit->AddForwarder(forwarder);
    break;
  default:
    assert(false);
  }
}


//order is very importand, channels, combiners then router
void MECSRouter::ReadInputs( ) {
  if(n_channel)
    n_channel->ReadInputs();
  if(e_channel)
    e_channel->ReadInputs();
  if(s_channel) 
    s_channel->ReadInputs();
  if(w_channel)
    w_channel->ReadInputs();

  if(n_credit_channel)
    n_credit_channel->ReadInputs();
  if(e_credit_channel)
    e_credit_channel->ReadInputs();
  if(s_credit_channel) 
    s_credit_channel->ReadInputs();
  if(w_credit_channel)
    w_credit_channel->ReadInputs();
  
  n->ReadInputs();
  e->ReadInputs();
  s->ReadInputs();
  w->ReadInputs();

  n_credit->ReadInputs();
  e_credit->ReadInputs();
  s_credit->ReadInputs();
  w_credit->ReadInputs();  

  sub_router->ReadInputs();
}
void MECSRouter::InternalStep( ) {
  sub_router->InternalStep();
}

//order is very important, router, channels
void MECSRouter::WriteOutputs( ) {
  sub_router->WriteOutputs();
  if(n_channel)
    n_channel->WriteOutputs();
  if(e_channel)
    e_channel->WriteOutputs();
  if(s_channel)
    s_channel->WriteOutputs();
  if(w_channel)
    w_channel->WriteOutputs();

  if(n_credit_channel)
    n_credit_channel->WriteOutputs();
  if(e_credit_channel)
    e_credit_channel->WriteOutputs();
  if(s_credit_channel)
    s_credit_channel->WriteOutputs();
  if(w_credit_channel)
    w_credit_channel->WriteOutputs();
}

//the credit channels are "reversed"
//injection ejection ports stays the same (i<gK)
//other wise, what use to be called "input credit channel" 
//is now actually output credit chcannels
void MECSRouter::Finalize(){
  for( int i = 0; i<_inputs; i++){
    if(i<gK){
      sub_router->AddInputChannel(_input_channels->at(i), _input_credits->at(i));
    } else {

      sub_router->AddInputChannel(_input_channels->at(i), _output_credits->at(i));
    }
  }

  for(int i = 0; i<_outputs; i++){
    if(i<gK){
      sub_router->AddOutputChannel(_output_channels->at(i), _output_credits->at(i));
    } else {
      sub_router->AddOutputChannel(_output_channels->at(i), _input_credits->at(i));
    }
  }
}
