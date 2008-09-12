#ifndef _MECSROUTER_HPP_
#define _MECSROUTER_HPP_

#include "router.hpp"
#include "iq_router.hpp"
#include "MECSForwarder.hpp"
#include "MECSCreditForwarder.hpp"
#include "MECSCombiner.hpp"
#include "MECSCreditCombiner.hpp"
#include "MECSChannels.hpp"
#include "MECSCreditChannel.hpp"

class MECSRouter: public Router{

  //The underlying operating router
  IQRouter*  sub_router;
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

  virtual int GetCredit(int out, int vc_begin, int vc_end ) const {}
  virtual int GetBuffer(int i) const {}
};

#endif
