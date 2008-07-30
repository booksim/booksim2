#ifndef _NETWORK_HPP_
#define _NETWORK_HPP_

#include <vector>

#include "module.hpp"
#include "flit.hpp"
#include "credit.hpp"
#include "router.hpp"
#include "module.hpp"
#include "flitchannel.hpp"
#include "creditchannel.hpp"
#include "config_utils.hpp"
#include "globals.hpp"



class Network : public Module {
protected:

  int _size;
  int _sources;
  int _dests;
  int _channels;

  Router **_routers;

  FlitChannel   *_inject;
  CreditChannel *_inject_cred;

  FlitChannel   *_eject;
  CreditChannel *_eject_cred;

  FlitChannel   *_chan;
  CreditChannel *_chan_cred;

  int *_chan_use;
  int _chan_use_cycles;

  virtual void _ComputeSize( const Configuration &config ) = 0;
  virtual void _BuildNet( const Configuration &config ) = 0;

  void _Alloc( );

public:
  Network( const Configuration &config );
  virtual ~Network( );

  virtual void WriteFlit( Flit *f, int source );
  virtual Flit *ReadFlit( int dest );
  virtual Flit *PeekFlit( int dest );

  virtual void    WriteCredit( Credit *c, int dest );
  virtual Credit *ReadCredit( int source );
  virtual Credit *PeekCredit( int source );

  int  NumSources( ) const;
  int  NumDests( ) const;

  virtual void InsertRandomFaults( const Configuration &config );
  void OutChannelFault( int r, int c, bool fault = true );

  virtual double Capacity( ) const;

  virtual void ReadInputs( );
  virtual void InternalStep( );
  virtual void WriteOutputs( );

  void Display( ) const;
};

#endif 

