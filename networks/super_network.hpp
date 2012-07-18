// $Id: super_network.hpp 4080 2011-10-22 23:11:32Z dub $

/*
 Copyright (c) 2007-2011, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this 
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

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

#ifndef _SUPER_NETWORK_HPP_
#define _SUPER_NETWORK_HPP_

#include <map>
#include <vector>
#include <deque>

#include "flit.hpp"
#include "credit.hpp"
#include "module.hpp"
#include "timed_module.hpp"
#include "flitchannel.hpp"
#include "channel.hpp"
#include "config_utils.hpp"
#include "network.hpp"
#include "globals.hpp"

typedef Channel<Credit> CreditChannel;


class SuperNetwork : public TimedModule {
protected:

  int _size;
  int _nodes;
  int _channels;
  int _network_clusters;
  int _bottleneck_channels, _bottleneck_channels_total;
  int _transition_channels_per_cluster;
  int _transition_channel_latency;
  
  vector<Network *> _networks;

  vector<FlitChannel *> *_input_transition_chan;
  vector<CreditChannel *> *_input_transition_chan_cred;
  vector<FlitChannel *> *_output_transition_chan;
  vector<CreditChannel *> *_output_transition_chan_cred;
  vector<Router *> *_transition_routers; // We don't need to call the TimingModule functions on these because each network class will.
  
  vector<Router *> _get_routers_return_value;
  vector<FlitChannel *> _get_channels_return_value;
  
  Flit*** _temp_channels;
  Credit*** _temp_credits;

  void _Alloc( );
  void MapNode(int node, int *transformed_node, int *network_cluster) const;
  void AllocateSubnets(const Configuration & config, const string & name);
  void ConnectTransitionChannels();
  void CalculateChannelsPerCluster();

public:
  SuperNetwork( const Configuration &config, const string & name );
  virtual ~SuperNetwork( );

  static SuperNetwork *NewNetwork( const Configuration &config, const string & name );
  
  //virtual Flit* GetSpecial(FlitChannel* fc, int vc); // This is called from routers, so it won't propagate beyond the network class.
  virtual void WriteSpecialFlit(Flit*f, int source);
  virtual void WriteFlit( Flit *f, int source );
  virtual Flit *ReadFlit( int dest );

  virtual void    WriteCredit( Credit *c, int dest );
  virtual Credit *ReadCredit( int source );

  inline int NumNodes( ) const {return _nodes;}
  
  void RouteFlit(Flit* f, int network_cluster, bool is_injection = false);

  virtual void InsertRandomFaults( const Configuration &config );
  void OutChannelFault( int r, int c, bool fault = true );

  virtual double Capacity( ) const;

  virtual void ReadInputs( );
  virtual void Evaluate( );
  virtual void WriteOutputs( );

  void Display( ostream & os = cout ) const;
  void DumpChannelMap( ostream & os = cout, string const & prefix = "" ) const;
  void DumpNodeMap( ostream & os = cout, string const & prefix = "" ) const;

  int NumChannels() const {return _channels;}
  const vector<FlitChannel *> & GetChannels();
  const vector<Router *> & GetRouters();
  int NumRouters() const {return _size;}
  
  inline Network* GetNetwork (int network_cluster) const
  {
    assert(network_cluster >= 0 && network_cluster < _network_clusters);
    return _networks[network_cluster];
  }
};

#endif 
