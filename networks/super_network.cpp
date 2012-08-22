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
#include <math.h>

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
#include "trafficmanager.hpp"

#define MIN(X,Y) ((X)<(Y)?(X):(Y))
#define MAX(X,Y) ((X)>(Y)?(X):(Y))

extern int RESERVATION_CHUNK_LIMIT;
extern int cycles_per_epoch;
extern float RESERVATION_OVERHEAD_FACTOR;

SuperNetwork::SuperNetwork( const Configuration &config, const string & name ) :// The original network was a timed module. This doesn't need to be.
  TimedModule( 0, name ), _network_clusters(-1)
{
  _size     = -1; 
  _nodes    = -1; 
  _channels = -1;
  _try_again_delay = config.GetInt("try_again_delay");
  _transition_channel_latency = config.GetInt("transition_channel_latency");
  int classes = config.GetInt("classes");
  _network_clusters = config.GetInt("network_clusters");
  _networks.resize(_network_clusters, 0);
  _bottleneck_channels = config.GetInt("bottleneck_channels");
  _bottleneck_channels_total = _transition_channels_per_cluster * _network_clusters * 2; // Times two because each channel is basically two flitchannel objects.
  _cycles_into_the_future = config.GetInt("cycles_into_the_future");
  _bit_vector_length = config.GetInt("bit_vector_length");
  _enable_multi_SRP = config.GetInt("enable_multi_SRP") > 0;
  assert(_enable_multi_SRP == false || (gReservation == true && gECN == false));
  _how_many_time_slots_to_reserve = config.GetInt("how_many_time_slots_to_reserve");
  _cycles_per_element = int(ceil(float(_cycles_into_the_future) / float(_bit_vector_length)));
  _current_epoch = 0;
  //_counter_max = int(ceil(float(_cycles_per_element) / float(RESERVATION_CHUNK_LIMIT)));
  _counter_max = _cycles_per_element; // We keep track of cycles fine-grain.
  _time_slot_to_begin = config.GetInt("time_slot_to_begin");
  
  // Cycles per element needs to be big enough for the boundary conditions between time slots to not matter much.
  assert(_cycles_per_element > 0);
  
  assert(_cycles_per_element * 2 > RESERVATION_CHUNK_LIMIT);
  
  CalculateChannelsPerCluster();
  
  _transition_routers = new vector<Router *> [_network_clusters];
  _input_transition_chan = new vector<FlitChannel *> [_network_clusters];
  _input_transition_chan_cred = new vector<CreditChannel *> [_network_clusters];
  _output_transition_chan = new vector<FlitChannel *> [_network_clusters];
  _output_transition_chan_cred = new vector<CreditChannel *> [_network_clusters];
  _bit_vectors = new vector<pair<int, vector<pair<int,int> > > > *[_network_clusters];
  _temp_channels = new Flit** [_network_clusters];
  _temp_credits = new Credit**[_network_clusters];
  _already_sent = new bool *[_network_clusters];
  _already_sent_credit = new bool *[_network_clusters];
  int half_latency = (int)(_transition_channel_latency / 2);
  ostringstream temp_name;
  assert(_transition_channels_per_cluster % 2 == 0 || _transition_channels_per_cluster == 1);
  for (int i = 0; i < _network_clusters; ++i)
  {
    _temp_channels[i] = new Flit*[_transition_channels_per_cluster];
    _temp_credits[i] = new Credit* [_transition_channels_per_cluster];
    _input_transition_chan[i].resize(_transition_channels_per_cluster, 0);
    _input_transition_chan_cred[i].resize(_transition_channels_per_cluster, 0);
    _output_transition_chan[i].resize(_transition_channels_per_cluster, 0);
    _output_transition_chan_cred[i].resize(_transition_channels_per_cluster, 0);
    _bit_vectors[i] = new vector<pair<int, vector<pair<int,int> > > > [_transition_channels_per_cluster];
    _already_sent[i] = new bool [_transition_channels_per_cluster];
    _already_sent_credit[i] = new bool [_transition_channels_per_cluster];
    for (int c = 0; c < _transition_channels_per_cluster; c++)
    {
      InitializeBitVector(&(_bit_vectors[i][c]), _bit_vector_length, _counter_max);
      _temp_channels[i][c] = 0;
      _temp_credits[i][c] = 0;
      _already_sent[i][c] = false;
      _already_sent_credit[i][c] = false;
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
  assert(_network_clusters <= 2 || _transition_channels_per_cluster / 2 == _bottleneck_channels);
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
    delete [] _bit_vectors[n];
    delete [] _already_sent[n];
    delete [] _already_sent_credit[n];
  }
  delete [] _bit_vectors;
  delete [] _input_transition_chan;
  delete [] _input_transition_chan_cred;
  delete [] _output_transition_chan;
  delete [] _output_transition_chan_cred;
  delete [] _transition_routers;
  delete [] _temp_channels;
  delete [] _temp_credits;
  delete [] _already_sent;
  delete [] _already_sent_credit;
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

void SuperNetwork::IncrementClusterHops(Flit *f)
{
  if (f != 0)
  {
    f->cluster_hops_taken++;
    assert(f->head == false || (f->cluster_hops_taken <= f->cluster_hops && f->cluster_hops_taken < _network_clusters));
  }
}

// This should be called when a flit arrives at the first reservation point.
void SuperNetwork::InitializeBitVector(Flit *f, int bit_vector_length, int cycles_per_element)
{
  assert(f->reservation_vector.empty() == true && f->res_type == RES_TYPE_RES && f->epoch == -1);
  // The flits carry bit vectos that just say that "this time slot is an option" or not.
  f->reservation_vector.resize(bit_vector_length, true);
  f->epoch = TrafficManager::DefineEpoch(GetSimTime(), cycles_per_element);
}

// This should be called by the supernetwork class.
void SuperNetwork::InitializeBitVector(vector<pair<int, vector<pair<int,int> > > > *vec, int bit_vector_length, int counter_max)
{
  vec->resize(bit_vector_length);
  for (int i = 0; i < bit_vector_length; i++)
  {
    (*vec)[i].first = counter_max;
    (*vec)[i].second.clear();
  }
}

// For when an epoch changed. This is also used by flits which means that suddenly they gain more options even for reservation points that passed them.
// That's ok though because it's unlikely that enough reservations will have to use the last slot to cause a problem, but even if so it will cause a retry which would had happened anyway.
void SuperNetwork::ShiftBitVector(vector<pair<int, vector<pair<int,int> > > > *vec, int bit_vector_length, int counter_max)
{
  vec->erase(vec->begin());
  vec->resize(bit_vector_length);
  vec->back().first = counter_max;
  assert(vec->back().second.empty() == true);
}

void SuperNetwork::ShiftBitVector(vector<bool> *vec, int bit_vector_length)
{
  vec->erase(vec->begin());
  vec->push_back(true);
  assert((int)vec->size() == bit_vector_length);
}

void SuperNetwork::IncrementEpoch(int new_epoch)
{
  assert(new_epoch == 0 || abs(new_epoch - _current_epoch) == 1);
  _current_epoch = new_epoch;
  for (int n = 0; n < _network_clusters; n++)
  {
    for (int c = 0; c < _transition_channels_per_cluster; c++)
    {
      ShiftBitVector(&(_bit_vectors[n][c]), _bit_vector_length, _counter_max);
    }
  }
}

void SuperNetwork::ReserveBitVector(Flit *f, int net, int chan)
{
  int slots_to_reserve = _how_many_time_slots_to_reserve;
  bool all_falses = true;
  assert(f && f->res_type == RES_TYPE_RES && f->payload > 0);
  if (f->try_again_after_time != -1)
  {
    return;
  }
  int payload = int(ceil(float(f->payload)*RESERVATION_OVERHEAD_FACTOR));
  assert(_enable_multi_SRP == true);
  for (int i = _time_slot_to_begin; i < _bit_vector_length; i++)
  {
    assert(_bit_vectors[net][chan][i].first >= 0 && _bit_vectors[net][chan][i].first <= _counter_max);
    bool opening = HasAnOpening(net, chan, i, payload);
    if (f->reservation_vector[i] == true && opening == true) // Both agree
    {
      if (slots_to_reserve > 0)
      {
          
        if (_bit_vectors[net][chan][i].first >= payload)
        {
          _bit_vectors[net][chan][i].first -= payload;
          _bit_vectors[net][chan][i].second.push_back(make_pair<int,int>(f->flid,payload));
          assert(_bit_vectors[net][chan][i].first >= 0);
          slots_to_reserve--;
        }
        else if (i + 1 < _bit_vector_length && _bit_vectors[net][chan][i].first > 0 && _bit_vectors[net][chan][i].first + _bit_vectors[net][chan][i+1].first >= payload)
        {
          int reduction_amount = MIN(payload, _bit_vectors[net][chan][i].first);
          int rest_of_reduction = payload - reduction_amount;
          assert(rest_of_reduction > 0 && rest_of_reduction < _cycles_per_element && reduction_amount <= _cycles_per_element && _bit_vectors[net][chan][i+1].first >= rest_of_reduction);
          _bit_vectors[net][chan][i].first -= reduction_amount;
          _bit_vectors[net][chan][i].second.push_back(make_pair<int,int>(f->flid,reduction_amount));
          _bit_vectors[net][chan][i+1].first -= rest_of_reduction;
          _bit_vectors[net][chan][i+1].second.push_back(make_pair<int,int>(f->flid,rest_of_reduction));
          i++; // Don't reserve in that (i+1) slot again.
          assert(i < _bit_vector_length);
          f->reservation_vector[i] = false;
          assert(_bit_vectors[net][chan][i-1].first >= 0 && _bit_vectors[net][chan][i].first >= 0);
          slots_to_reserve--;
        }
        else
        {
          assert(false);
        }
      }
    }
    else if (f->reservation_vector[i] == true && opening == false) // Then no agreement
    {
      f->reservation_vector[i] = false;
    }
    if (f->reservation_vector[i] == true)
    {
      all_falses = false;
    }
  }
  if (all_falses == true)
  {
    int old_value = f->try_again_after_time;
    f->try_again_after_time = GetSimTime() + _try_again_delay;
    f->try_again_after_time = MAX(old_value, f->try_again_after_time);
    // We can't set payload to -1 because the trafficmanager needs that information to decide what retry to issue.
  }
}

bool SuperNetwork::HasAnOpening(int net, int chan, int vector_index, int size) const
{
  if (vector_index == -1)
  {
    return false;
  }
  assert(vector_index >= 0 && vector_index < _bit_vector_length && (int)_bit_vectors[net][chan].size() <= _bit_vector_length);
  int reservation_size = size;
  reservation_size -= _bit_vectors[net][chan][vector_index].first;
  if (vector_index + 1 < _bit_vector_length && reservation_size > 0)
  {
    reservation_size -= _bit_vectors[net][chan][vector_index + 1].first;
  }
  return reservation_size <= 0 && _bit_vectors[net][chan][vector_index].first > 0;
}

// What timeslot in the bit vector the specified timestamp belongs in
int SuperNetwork::BelongsInThatTimeSlot(int timestamp, int is_valid) const
{
  if (is_valid != -1)
  {
    return -2;
  }
  int sim_time = GetSimTime();
  int relative_timestamp = timestamp - (sim_time - sim_time % _cycles_per_element);
  if (relative_timestamp <= -1 * _cycles_per_element)
  {
    return -2;
  }
  else if (relative_timestamp < 0)
  {
    return -1; // So that if a reservation was made at the first cell of the bit vector, it will be released.
  }
  int return_value = (int)(relative_timestamp / _cycles_per_element);
  assert(return_value < _bit_vector_length);
  return return_value;
}

int SuperNetwork::MaxTimestampCovered() const
{
  return GetSimTime() - GetSimTime() % _cycles_per_element + _bit_vector_length * _cycles_per_element;
}

// When grants come back, we need to reconstruct what network and channel the corresponding reservation went through.
int SuperNetwork::GetOtherChannel(int chan) const
{
  assert(_network_clusters > 1);
  int return_value = -1;
  if (_network_clusters == 2)
  {
    return_value = chan;
  }
  else
  {
    if (chan < _transition_channels_per_cluster / 2)
    {
      // This means that the channel is going to a higher numbered cluster. The corresponding channel is going to a lower numbered channel.
      return_value = chan + _transition_channels_per_cluster / 2;
    }
    else
    {
      return_value = chan - _transition_channels_per_cluster / 2;
    }
  }
  assert(return_value >= 0 && return_value < _transition_channels_per_cluster);
  return return_value;
}

// TODO: test with more than two cluster.
// Frees any slots it had reserved for this flow id. Be careful though that grant flits carry timestamps, they don't use bit vectors any more.
void SuperNetwork::HandleGrantFlits(Flit *f, int net, int chan)
{
  if (f->try_again_after_time != -1 || f->reservation_size == -1)
  {
    return;
  }
        
  // Since this grant is going backwards from where it made its reservation, we need to reconstruct where the reservations are.
  net = GetNextCluster(net, chan);
  chan = GetOtherChannel(chan);
  
  int size;
  bool found_flow;
  assert(f && f->res_type == RES_TYPE_GRANT);
  int timeslot_it_belongs = BelongsInThatTimeSlot(f->payload, f->try_again_after_time);
  for (int i = 0; i < _bit_vector_length; i++)
  {
    size = _bit_vectors[net][chan][i].second.size();
    found_flow = false;
    assert(_bit_vectors[net][chan][i].first >= 0 && _bit_vectors[net][chan][i].first <= _counter_max);
    for (int c = 0; c < size; c++)
    {
      if (_bit_vectors[net][chan][i].second[c].first == f->flid)
      {
        assert(found_flow == false);
        found_flow = true;
        if (timeslot_it_belongs != i && !(timeslot_it_belongs == -1 && i == 0))
        {
          _bit_vectors[net][chan][i].first += _bit_vectors[net][chan][i].second[c].second; // Restore the bit that was reserved for this flow but wasn't granted.
          _bit_vectors[net][chan][i].second.erase(_bit_vectors[net][chan][i].second.begin() + c);
          // There should be only one entry per flid so we can break the inner for loop here.
          break;
        }
        else if (timeslot_it_belongs == i || (timeslot_it_belongs == -1 && i == 0))
        {
          // If timeslot_it_belongs is -1 it means that the timestamp is on the time slot that just got shifted. In that case don't erase any reservations in the bit vector of slot 0.
          // In case those bit should had been erased, speculative packets will use that bandwidth.
          // Doesn't really matter if we delete it or not. Might as well to speed up future searches.
          _bit_vectors[net][chan][i].second.erase(_bit_vectors[net][chan][i].second.begin() + c);
          if (i + 1 < _bit_vector_length && !(timeslot_it_belongs == -1 && i == 0))
          {
            // We also go in to the next time slot and delete the leftover fraction so it doesn't get released.
            for (vector<pair<int,int> >::iterator a = _bit_vectors[net][chan][i+1].second.begin(); a != _bit_vectors[net][chan][i+1].second.end();)
            {
              if ((*a).first == f->flid)
              {
                a = _bit_vectors[net][chan][i+1].second.erase(a);
              }
              else
              {
                a++;
              }
            }
          }
          break;
        }
      }
    }
    // If the bit wasn't reserved for this flow at this channel but the granted timestamp belongs in a time slot which is all taken (bits zero), transform the grant into a try again.
    // We could just preempt some other reserved bit, but that is identical to not reserving in the first place
    if (found_flow == false && timeslot_it_belongs == i)
    {
      // If the grant was for this slot, let's see if the timestamps can accommodate us or not.
      bool has_opening = HasAnOpening(net, chan, timeslot_it_belongs, f->payload);
      if (has_opening == false)
      {
        // Can't accommodate us. Must generate a retry response.
        int old_value = f->try_again_after_time;
        f->try_again_after_time = GetSimTime() + _try_again_delay;
        f->try_again_after_time = MAX(old_value, f->try_again_after_time);
        f->payload = -1;
        assert(_enable_multi_SRP == true);
      }
      else
      {
        int reduction = MIN(f->reservation_size, _bit_vectors[net][chan][i].first);
        assert(reduction > 0 && _bit_vectors[net][chan][i].first > 0);
        _bit_vectors[net][chan][i].first -= reduction;
        reduction = f->reservation_size - reduction;
        assert(reduction == 0 || i + 1 < _bit_vector_length);
        if (reduction > 0)
        {
          _bit_vectors[net][chan][i+1].first -= reduction;
          assert(_bit_vectors[net][chan][i].first == 0);
        }
      }

    }
  }
}

void SuperNetwork::HandleNonResFlits(Flit *f, int net, int chan)
{
  if (f == 0 || f->res_type != RES_TYPE_NORM)
  {
    return;
  }
  int size = f->packet_size + 4;
  for (int a = 0; a < _bit_vector_length && size > 0; a++)
  {
    if (_bit_vectors[net][chan][a].first > 0)
    {
      int reduction_amount = MIN(size, _bit_vectors[net][chan][a].first);
      _bit_vectors[net][chan][a].first -= reduction_amount;
      size -= reduction_amount;
      assert(size >= 0);
    }
  }
}

void SuperNetwork::HandleResGrantFlits(Flit *f, int n, int i)
{
  if (f == 0)
  {
    return;
  }
  if (f->res_type == RES_TYPE_RES)
  {
    assert(f->head == true && f->tail == true);
    if (f->epoch == -1)
    {
      InitializeBitVector(f, _bit_vector_length, _cycles_per_element);
    }
    else if (f->epoch != _current_epoch)
    {
      ShiftBitVector(&(f->reservation_vector), _bit_vector_length);
      f->epoch = _current_epoch;
    }
    if (_enable_multi_SRP == true)
    {
      ReserveBitVector(f, n, i);
    }
  }
  else if (f->res_type == RES_TYPE_GRANT)
  {
    assert(f->head == true && f->tail == true);
    HandleGrantFlits(f, n, i);
  }
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
    assert(f->original_destination == -1 && f->source_network_cluster == -1 && f->cluster_hops_taken == 0);
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
      int going_up;
      if (f->dest_network_cluster >= network_cluster)
      {
        going_up = f->dest_network_cluster - network_cluster;
      }
      else
      {
        going_up = f->dest_network_cluster + _network_clusters - network_cluster;
      }
      int going_down = _network_clusters - going_up;
      bool go_up = false;
      if (going_down == going_up && _network_clusters > 2 && RandomInt(1) == 1) // If distances are equal, randomize the choice.
      {
        go_up = true;
      }
      if ((network_cluster < f->dest_network_cluster && _network_clusters == 2) || ((going_up < going_down || (going_up == going_down && go_up == true)) && _network_clusters > 2))
      {
        f->going_up_clusters = true;
        f->cluster_hops = going_up;
      }
      else
      {
        f->going_up_clusters = false;
        f->cluster_hops = going_down;
      }
      assert(f->cluster_hops > 0);
    }
  }
  assert(f->dest_network_cluster != -1 && f->source_network_cluster != -1);
  if (f->dest_network_cluster == network_cluster)
  { // The cluster it's going to is the destination one.
    f->dest = f->original_destination % nodes_per_cluster; 
  }
  else
  {
    assert(_network_clusters >= 2);
    int choice;
    if (!(f->res_type == RES_TYPE_GRANT || f->res_type == RES_TYPE_ACK))
    {
      assert(is_injection == true ||f->bottleneck_channel_choices.empty() == false);
      choice = RandomInt(_bottleneck_channels - 1); // Choose the next bottleneck channel randomly.
      f->bottleneck_channel_choices.push_back(choice);
      int size_list = (int)f->bottleneck_channel_choices.size();
      assert(size_list <= f->cluster_hops);
    }
    else
    {
      assert(f->bottleneck_channel_choices.empty() == false);
      choice = f->bottleneck_channel_choices.front();
      f->bottleneck_channel_choices.pop_front();
    }
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
  _get_channels_return_value.clear();
  for (int n = 0; n < _network_clusters; n++)
  {
    vector<FlitChannel *> temp = _networks[n]->GetChannels();
    _get_channels_return_value.insert(_get_channels_return_value.begin(), temp.begin(), temp.end());
  }
  return _get_channels_return_value;
}

const vector<Router *> & SuperNetwork::GetRouters()
{
  _get_routers_return_value.clear();
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
      _temp_credits[n][i] = _input_transition_chan_cred[n][i]->Receive();
      assert((_already_sent[n][i] == true && _already_sent_credit[n][i] == true) || GetSimTime() == 0);
      _already_sent[n][i] = false;
      _already_sent_credit[n][i] = false;
    }
  }
}

void SuperNetwork::Evaluate( )
{
  for (int n = 0; n < _network_clusters; n++)
  {
    _networks[n]->Evaluate();
    for (int i = 0; i < _transition_channels_per_cluster; i++)
    {
      _input_transition_chan[n][i]->Evaluate();
      _input_transition_chan_cred[n][i]->Evaluate();
      _output_transition_chan[n][i]->Evaluate();
      _output_transition_chan_cred[n][i]->Evaluate();
      Flit *f = _temp_channels[n][i];
      if (_enable_multi_SRP == true)
      {
        assert(gReservation == true);
        HandleResGrantFlits(f, n, i);
	HandleNonResFlits(f, n, i);
      }
      IncrementClusterHops(f);
      if (f != 0)
      {
        assert(f->head == false || f->dest_network_cluster != f->source_network_cluster);
        RouteFlit(f, GetNextCluster(n, i), false);
      }
    }
  }
  int new_epoch = TrafficManager::DefineEpoch(GetSimTime(), _cycles_per_element);
  if (new_epoch != _current_epoch)
  {
    IncrementEpoch(new_epoch);    
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
      SendTransitionFlits(_temp_channels[n][i], _temp_credits[n][i], n, i);
      _temp_channels[n][i] = 0;
      _temp_credits[n][i] = 0;
    }
  }
}

int SuperNetwork::GetNextCluster(int net, int chan) const
{
  int other_net = -1;
  if (_network_clusters == 2)
  {
    other_net = net == 1 ? 0 : 1;
  }
  else
  {
    if (chan < _transition_channels_per_cluster / 2)
    {
      other_net = net == _network_clusters - 1 ? 0 : net + 1;
    }
    else
    {
      other_net = net == 0 ? _network_clusters - 1 : net - 1;
    }
  }
  assert(other_net >= 0);
  return other_net;
}

int SuperNetwork::GetNextClusterCredit(int net, int chan) const
{
  int other_net = -1;
  if (_network_clusters == 2)
  {
    other_net = net == 1 ? 0 : 1;
  }
  else
  {
    if (chan >= _transition_channels_per_cluster / 2)
    {
      other_net = net == _network_clusters - 1 ? 0 : net + 1;
    }
    else
    {
      other_net = net == 0 ? _network_clusters - 1 : net - 1;
    }
  }
  assert(other_net >= 0);
  return other_net;
}

void SuperNetwork::SendTransitionFlits(Flit *f, Credit *c, int net, int chan)
{
  assert(_network_clusters > 1);
  int other_net = GetNextCluster(net, chan);
  assert(_already_sent[other_net][chan] == false);
  _already_sent[other_net][chan] = true;
  _input_transition_chan[other_net][chan]->Send(f);
  other_net = GetNextClusterCredit(net, chan);
  assert(_already_sent_credit[other_net][chan] == false);
  _already_sent_credit[other_net][chan] = true;
  _output_transition_chan_cred[other_net][chan]->Send(c);
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
