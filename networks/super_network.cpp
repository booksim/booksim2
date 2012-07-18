// $Id: super_network.cpp 4080 2011-10-22 23:11:32Z dub $

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

/*network.cpp
 *
 *This class is the basis of the entire network, it contains, all the routers
 *channels in the network, and is extended by all the network topologies
 *
 */

#include <cassert>
#include <sstream>
#include <stdlib.h>

#include "booksim.hpp"
#include "super_network.hpp"

#include "kncube.hpp"
#include "fly.hpp"
#include "cmesh.hpp"
#include "flatfly_onchip.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
#include "anynet.hpp"
#include "dragonfly.hpp"

// TODO We can either do SRP in this class, or mark the channels that need SRP and have the routers do it. The latter is more realistic.
SuperNetwork::SuperNetwork( const Configuration &config, const string & name ) :// The original network was a timed module. This doesn't need to be.
  TimedModule( 0, name ), _network_clusters(-1)
{
  _size     = -1; 
  _nodes    = -1; 
  _channels = -1;
  _transition_channel_latency = config.GetInt("transition_channel_latency");
  int classes = config.GetInt("classes");
  _network_clusters = config.GetInt("network_clusters");
  _networks.resize(_network_clusters, 0);
  _bottleneck_channels = config.GetInt("bottleneck_channels");
  _bottleneck_channels_total = _transition_channels_per_cluster * _network_clusters * 2; // Times two because each channel is basically two flitchannel objects.
  
  CalculateChannelsPerCluster();
  
  _transition_routers = new vector<Router *> [_network_clusters];
  _input_transition_chan = new vector<FlitChannel *> [_network_clusters];
  _input_transition_chan_cred = new vector<CreditChannel *> [_network_clusters];
  _output_transition_chan = new vector<FlitChannel *> [_network_clusters];
  _output_transition_chan_cred = new vector<CreditChannel *> [_network_clusters];
  _temp_channels = new Flit** [_network_clusters];
  _temp_credits = new Credit**[_network_clusters];
  int half_latency = (int)(_transition_channel_latency / 2);
  ostringstream temp_name;
  for (int i = 0; i < _network_clusters; ++i)
  {
    _temp_channels[i] = new Flit*[_transition_channels_per_cluster];
    _temp_credits[i] = new Credit* [_transition_channels_per_cluster];
    _input_transition_chan[i].resize(_transition_channels_per_cluster, 0);
    _input_transition_chan_cred[i].resize(_transition_channels_per_cluster, 0);
    _output_transition_chan[i].resize(_transition_channels_per_cluster, 0);
    _output_transition_chan_cred[i].resize(_transition_channels_per_cluster, 0);    
    for (int c = 0; c < _transition_channels_per_cluster; c++)
    {
      _temp_channels[i][c] = 0;
      _temp_credits[i][c] = 0;
      temp_name.str("");
      temp_name << "Input transition flit channel for network " << i << " index " << c;
      _input_transition_chan[i][c] = new FlitChannel(this, temp_name.str(), classes);
      _input_transition_chan[i][c]->SetTransition();
      _input_transition_chan[i][c]->SetLatency(half_latency);
      temp_name.str("");
      temp_name << "Output transition flit channel for network " << i << " index " << c;
      _output_transition_chan[i][c] = new FlitChannel(this, temp_name.str(), classes);
      _output_transition_chan[i][c]->SetTransition();
      _output_transition_chan[i][c]->SetLatency(half_latency);
      temp_name.str("");
      temp_name << "Input transition credit channel for network " << i << " index " << c;
      _input_transition_chan_cred[i][c] = new CreditChannel(this, temp_name.str());
      _input_transition_chan_cred[i][c]->SetLatency(half_latency);
      temp_name.str("");
      temp_name << "Output transition credit channel for network " << i << " index " << c;
      _output_transition_chan_cred[i][c] = new CreditChannel(this, temp_name.str());
      _output_transition_chan_cred[i][c]->SetLatency(half_latency);
    }
  }
  
  AllocateSubnets(config, name);
  ConnectTransitionChannels();
  _Alloc();
}

SuperNetwork::~SuperNetwork( )
{
  for (int n = 0; n < _network_clusters; n++)
  {
    delete _networks[n];
    delete [] _temp_channels[n];
    delete [] _temp_credits[n];
    for (int c = 0; c < _transition_channels_per_cluster; ++c)
    {
      delete _input_transition_chan[n][c];
      delete _input_transition_chan_cred[n][c];
      delete _output_transition_chan[n][c];
      delete _output_transition_chan_cred[n][c];
    }
  }
  delete [] _input_transition_chan;
  delete [] _input_transition_chan_cred;
  delete [] _output_transition_chan;
  delete [] _output_transition_chan_cred;
  delete [] _transition_routers;
  delete [] _temp_channels;
  delete [] _temp_credits;
}

SuperNetwork * SuperNetwork::NewNetwork(const Configuration & config, const string & name)
{
  return new SuperNetwork(config, name);
}

void SuperNetwork::AllocateSubnets(const Configuration & config, const string & name)
{
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n] = Network::NewNetwork(config, name);
    _networks[n]->AssignIndex(n);
  }
  _size = _networks[0]->NumRouters() * _network_clusters;
  _nodes = _networks[0]->NumNodes() * _network_clusters;
}

void SuperNetwork::ConnectTransitionChannels()
{
  if (_network_clusters == 1)
  {
    return;
  }
  else
  {
    
    vector<Router*> *temp = 0;
    for (int n = 0; n < _network_clusters; n++)
    {
      temp = _networks[n]->AddUpTransitionRouters(_bottleneck_channels);
      _transition_routers[n].insert(_transition_routers[n].end(), temp->begin(), temp->end());
      delete temp;
      temp = 0;
      if (_network_clusters > 2)
      {
        temp = _networks[n]->AddDownTransitionRouters(_bottleneck_channels);
        _transition_routers[n].insert(_transition_routers[n].end(), temp->begin(), temp->end());
        delete temp;
        temp = 0;
      }
      assert((int)_transition_routers[n].size() == _transition_channels_per_cluster);
    }

    int target_net = 1;
    // First we connect net with target net, then target_net with net.
    int index, temp_size, temp_size2;
    for (int net = 0; net < _network_clusters; net++)
    {
      temp_size = _transition_routers[net].size();
      temp_size2 = _transition_routers[target_net].size();
      // For example if we connect cluster 0 with 1 with two channels, channel 0 and 1 go from 0 to 1, and channels 2 and 3 go from 1 to 0.
      // Also, the bottom half of each _transnition_routers vector is for routers to go to a higher-numbered cluster.
      // This connects net (the lower number) with target_net;
      assert(target_net > 0 || net == _network_clusters - 1);
      index = 0;
      for (int c = 0; c < _bottleneck_channels; c++)
      {   
        assert(index < _bottleneck_channels && index < temp_size && index < temp_size2);
        _transition_routers[net][index]->AddTransitionOutputChannel(_output_transition_chan[net][index], _output_transition_chan_cred[net][index]);
        _transition_routers[target_net][index]->AddTransitionInputChannel(_input_transition_chan[target_net][index], _input_transition_chan_cred[target_net][index]);
        index++;
      }
      
      target_net = target_net == _network_clusters - 1 ? 0 : target_net + 1;
      if (_network_clusters == 2)
      {
        assert(net == 0 && target_net == 0);
        break; // Don't have double connections if there are only two clusters.
      }
    } // End of loop which connects lower-number clusters to higher-number.

    target_net = 1;
    for (int net = 0; net < _network_clusters; net++)
    {
      temp_size = _transition_routers[net].size();
      temp_size2 = _transition_routers[target_net].size(); 
      // For example if we connect cluster 0 with 1 with two channels, channel 0 and 1 go from 0 to 1, and channels 2 and 3 go from 1 to 0.
      // Also, the bottom half of each _transition_routers vector is for routers to go to a higher-numbered cluster.
      // This connects target_net (the higher number) with net.
      assert(target_net > 0 || net == _network_clusters - 1);
      index = _network_clusters == 2 ? 0 : _bottleneck_channels;
      for (int c = 0; c < _bottleneck_channels; c++)
      {   
        assert(index < _transition_channels_per_cluster && index < temp_size && index < temp_size2);
        _transition_routers[target_net][index]->AddTransitionOutputChannel(_output_transition_chan[target_net][index], _output_transition_chan_cred[target_net][index]);
        _transition_routers[net][index]->AddTransitionInputChannel(_input_transition_chan[net][index], _input_transition_chan_cred[net][index]);
        index++;
      }
      
      target_net = target_net == _network_clusters - 1 ? 0 : target_net + 1;
      if (_network_clusters == 2)
      {
        assert(net == 0 && target_net == 0);
        break; // Don't have double connections if there are only two clusters.
      }
    } // End of loop which connects higher-number clusters to lower-number.
     
    
    for (int net = 0; net < _network_clusters; net++)
    {
      assert((int)_transition_routers[net].size() == _transition_channels_per_cluster);
      for (vector<Router * >::iterator i = _transition_routers[net].begin(); i != _transition_routers[net].end(); i++)
      {
        assert((*i)->IsTransitionRouter());
      }
    }
  }
}

// TODO If it's a reservation grant (reply), it must follow the same path back (sequence of clusters and bottleneck channels) to the source because it must manipulate data structures on the way back.
void SuperNetwork::RouteFlit(Flit* f, int network_cluster, bool is_injection)
{
  if (f->head == false)
  {
    return;
  }
  assert(f != 0 && f->dest >= 0 && f->dest < _nodes && network_cluster >= 0 && network_cluster < _network_clusters && f->src >= 0 && f->src < _nodes);
  int nodes_per_cluster = _nodes / _network_clusters;
  if (is_injection == false)
  {
    f->intm = -1;
    f->ph = -1;
    f->minimal = 1;
  }
  else
  {
    assert(f->original_destination == -1 && f->source_network_cluster == -1);
    f->source_network_cluster = network_cluster;
    f->original_destination = f->dest;
    assert(_nodes % _network_clusters == 0); // Make them nice and round please.
    f->dest_network_cluster = f->dest / nodes_per_cluster;
    if (network_cluster == f->dest_network_cluster)
    {
      f->dest %= nodes_per_cluster;
      return;
    }
    else
    {
      int going_up = abs(f->dest_network_cluster - network_cluster);
      int going_down = abs(network_cluster - f->dest_network_cluster);
      if ((network_cluster < f->dest_network_cluster && _network_clusters == 2) || going_up < going_down)
      {
        f->going_up_clusters = true;
      }
      else
      {
        f->going_up_clusters = false;
      }
    }
  }
  assert(f->dest_network_cluster != -1 && f->dest_network_cluster != network_cluster);
  if (f->dest_network_cluster == network_cluster)
  { // The cluster it's going to is the destination one.
    f->dest = f->original_destination % nodes_per_cluster; 
  }
  else
  {
    assert(_network_clusters >= 2);
    int choice = RandomInt(_bottleneck_channels - 1); // Choose the next bottleneck channel randomly.
    int next_cluster;
    if (f->going_up_clusters) // It has more hops to go.
    {
      next_cluster = network_cluster + 1 >= _network_clusters ? 0 : network_cluster + 1;
      f->dest = _transition_routers[next_cluster][choice]->GetDestinationThisServes();
    }
    else
    {  // The first half of the transition routers vector takes us to higher-numbered clusters, and the lower half to lower-numbered clusters.
      next_cluster = network_cluster - 1 < 0 ? _network_clusters - 1 : network_cluster - 1;
      int add_factor = _network_clusters == 2 ? 0 : _bottleneck_channels;
      f->dest = _transition_routers[next_cluster][choice + add_factor]->GetDestinationThisServes();
    }
  }
}

const vector<FlitChannel *> & SuperNetwork::GetChannels()
{
  assert(_get_channels_return_value.empty());
  for (int n = 0; n < _network_clusters; n++)
  {
    vector<FlitChannel *> temp = _networks[n]->GetChannels();
    _get_channels_return_value.insert(_get_channels_return_value.begin(), temp.begin(), temp.end());
  }
  return _get_channels_return_value;
}

const vector<Router *> & SuperNetwork::GetRouters()
{
  assert(_get_routers_return_value.empty());
  for (int n = 0; n < _network_clusters; n++)
  {
    vector<Router *> temp = _networks[n]->GetRouters();
    _get_routers_return_value.insert(_get_routers_return_value.begin(), temp.begin(), temp.end());
  }
  return _get_routers_return_value;
}

void SuperNetwork::_Alloc( )
{
  assert( ( _size != -1 ) && 
	  ( _nodes != -1 ) && 
	  ( _bottleneck_channels_total != -1 ) );

  gNodes = _networks[0]->NumNodes(); // The deception continues.
  gNodes_total = _nodes;

}

void SuperNetwork::ReadInputs( )
{
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n]->ReadInputs();  
  }
  for (int n = 0; n < _network_clusters; n++)
  {
    for (int i = 0; i < _transition_channels_per_cluster; i++)
    {
      assert(_temp_channels[n][i] == 0 && _temp_credits[n][i] == 0);
      _input_transition_chan[n][i]->ReadInputs();
      _input_transition_chan_cred[n][i]->ReadInputs();
      _output_transition_chan[n][i]->ReadInputs();
      _output_transition_chan_cred[n][i]->ReadInputs(); // Read inputs first and then receive.
      _temp_channels[n][i] = _output_transition_chan[n][i]->Receive();
      _temp_credits[n][i] = _output_transition_chan_cred[n][i]->Receive();
    }
  }
}

void SuperNetwork::Evaluate( )
{
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n]->Evaluate();
    // TODO Do stuff to the flits in transit to implement reservation and such.
    for (int i = 0; i < _transition_channels_per_cluster; i++)
    {
      _input_transition_chan[n][i]->Evaluate();
      _input_transition_chan_cred[n][i]->Evaluate();
      _output_transition_chan[n][i]->Evaluate();
      _output_transition_chan_cred[n][i]->Evaluate();
    }
  }
}

void SuperNetwork::WriteOutputs( )
{
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n]->WriteOutputs();
  }
  for (int n = 0; n < _network_clusters; n++)
  {
    for (int i = 0; i < _transition_channels_per_cluster; i++)
    {
      _input_transition_chan[n][i]->WriteOutputs();
      _input_transition_chan_cred[n][i]->WriteOutputs();
      _output_transition_chan[n][i]->WriteOutputs();
      _output_transition_chan_cred[n][i]->WriteOutputs();
      _input_transition_chan[n][i]->Send(_temp_channels[n][i]);
      _input_transition_chan_cred[n][i]->Send(_temp_credits[n][i]);
      _temp_channels[n][i] = 0;
      _temp_credits[n][i] = 0;
    }
  }
}

// This is for the purposes of trafficmanager-network communication, not routing.
void SuperNetwork::MapNode(int node, int *transformed_node, int *network_cluster) const
{
  assert(node >= 0 && node < _nodes);
  int temp = _nodes / _network_clusters;
  *transformed_node = node % temp;
  *network_cluster = node / temp;
}

void SuperNetwork::WriteSpecialFlit( Flit *f, int source )
{
  assert( ( source >= 0 ) && ( source < _nodes ) );
  int net_cluster, transformed_node;
  MapNode(source, &transformed_node, &net_cluster);
  RouteFlit(f, net_cluster, true);
  assert(net_cluster >= 0 && net_cluster < _network_clusters && transformed_node >= 0 && transformed_node < _networks[net_cluster]->NumNodes());
  _networks[net_cluster]->WriteSpecialFlit(f, transformed_node);
}

void SuperNetwork::WriteFlit( Flit *f, int source )
{
  assert(false); //dont' use this, use write special flits
  assert( ( source >= 0 ) && ( source < _nodes ) );
  int net_cluster, transformed_node;
  MapNode(source, &transformed_node, &net_cluster);
  assert(f->id >= 0);
  RouteFlit(f, net_cluster, true);
  assert(net_cluster >= 0 && net_cluster < _network_clusters && transformed_node >= 0 && transformed_node < _networks[net_cluster]->NumNodes());
  _networks[net_cluster]->WriteFlit(f, transformed_node);
}

Flit *SuperNetwork::ReadFlit( int dest )
{
  assert( ( dest >= 0 ) && ( dest < _nodes ) );
  int net_cluster, transformed_node;
  MapNode(dest, &transformed_node, &net_cluster);
  assert(net_cluster >= 0 && net_cluster < _network_clusters && transformed_node >= 0 && transformed_node < _networks[net_cluster]->NumNodes());
  Flit * f = _networks[net_cluster]->ReadFlit(transformed_node);
  if (f != 0)
  {
    f->dest = f->original_destination; // To avoid confusing the traffic manager.
  }
  return f;
}

void SuperNetwork::WriteCredit( Credit *c, int dest )
{
  assert( ( dest >= 0 ) && ( dest < _nodes ) );
  int net_cluster, transformed_node;
  MapNode(dest, &transformed_node, &net_cluster);
  _networks[net_cluster]->WriteCredit(c, transformed_node);
}

Credit *SuperNetwork::ReadCredit( int source )
{
  assert( ( source >= 0 ) && ( source < _nodes ) );
  int net_cluster, transformed_node;
  MapNode(source, &transformed_node, &net_cluster);
  return _networks[net_cluster]->ReadCredit(transformed_node);
}

void SuperNetwork::InsertRandomFaults( const Configuration &config )
{
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n]->InsertRandomFaults(config);
  }
  return;
}

void SuperNetwork::OutChannelFault( int r, int c, bool fault )
{
  assert( ( r >= 0 ) && ( r < _size ) );
  for (int i = 0; i < _network_clusters; i++)
  {
    _networks[i]->OutChannelFault(r, c, fault);
  }
}

double SuperNetwork::Capacity( ) const
{
  return 1.0;
}

void SuperNetwork::Display( ostream & os ) const
{
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n]->Display(os);;
  }
}

void SuperNetwork::DumpChannelMap( ostream & os, string const & prefix ) const
{
  // TODO Dump the channel mapping of bottleneck channels.
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n]->DumpChannelMap(os, prefix);
  }
}

void SuperNetwork::DumpNodeMap( ostream & os, string const & prefix ) const
{
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n]->DumpNodeMap(os, prefix);
  }
}

void SuperNetwork::CalculateChannelsPerCluster()
{
 if (_network_clusters == 1)
  {
    _transition_channels_per_cluster = 0;
  }
  else if (_network_clusters == 2)
  {
    _transition_channels_per_cluster = _bottleneck_channels;
  }
  else
  {
    _transition_channels_per_cluster = 2 * _bottleneck_channels; // 0 to 1, 1 to 2, 2 to 0. So each cluster has twice as many channels as above.
  }
}
