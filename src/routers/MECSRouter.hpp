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

#ifndef _MECSROUTER_HPP_
#define _MECSROUTER_HPP_

#include "router.hpp"
#include "iq_router_baseline.hpp"
#include "MECSForwarder.hpp"
#include "MECSCreditForwarder.hpp"
#include "MECSCombiner.hpp"
#include "MECSCreditCombiner.hpp"
#include "MECSChannels.hpp"
#include "MECSCreditChannel.hpp"
#include <assert.h>

class MECSRouter: public Router{

  //The underlying operating router
  IQRouterBaseline*  sub_router;
  //muxes drop-off points into the subrouter
  MECSCombiner* n;
  MECSCombiner* e;
  MECSCombiner* s;
  MECSCombiner* w;
  //muxes credit drop-off points into subrouter
  MECSCreditCombiner* n_credit;
  MECSCreditCombiner* e_credit;
  MECSCreditCombiner* s_credit;
  MECSCreditCombiner* w_credit;

  //the output channels
  MECSChannels* n_channel;
  MECSChannels* e_channel;
  MECSChannels* s_channel;
  MECSChannels* w_channel;
  //the output credit channels
  MECSCreditChannels* n_credit_channel;
  MECSCreditChannels* e_credit_channel;
  MECSCreditChannels* s_credit_channel;
  MECSCreditChannels* w_credit_channel;


public:
  MECSRouter( const Configuration& config,
	    Module *parent, string name, int id,
	    int inputs, int outputs );
  
  virtual ~MECSRouter( );

  virtual void AddInputChannel( FlitChannel *channel, CreditChannel *backchannel);
  virtual void AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel );
  void AddInputChannel( FlitChannel *channel, CreditChannel *backchannel , int dir);
  void AddMECSChannel(MECSChannels *chan, int dir);
  void AddMECSCreditChannel(MECSCreditChannels *chan, int dir);
  void AddForwarder(MECSForwarder* forwarder, int dir);
  void AddCreditForwarder(MECSCreditForwarder* forwarder, int dir);

  virtual void ReadInputs( );
  virtual void InternalStep( );
  virtual void WriteOutputs( );
  virtual void Finalize ();

  virtual int GetCredit(int out, int vc_begin, int vc_end ) const {return -1;}
  virtual int GetBuffer(int i = -1) const {return sub_router->GetBuffer(i);}
  virtual int GetReceivedFlits(int i = -1) const {return sub_router->GetReceivedFlits(i);}
  virtual int GetSentFlits(int i = -1) const {return sub_router->GetSentFlits(i);}
  virtual void ResetFlitStats() {sub_router->ResetFlitStats();}
  
};

#endif
