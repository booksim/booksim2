// $Id$

/*
Copyright (c) 2007-2011, Trustees of The Leland Stanford Junior University
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

#include "synthetictrafficmanager.hpp"

SyntheticTrafficManager::SyntheticTrafficManager( const Configuration &config, const vector<Network *> & net )
: TrafficManager(config, net)
{

  // ============ Traffic ============ 

  vector<string> traffic = config.GetStrArray("traffic");
  traffic.resize(_classes, traffic.back());

  _traffic_pattern.resize(_classes);
  for(int c = 0; c < _classes; ++c) {
    _traffic_pattern[c] = TrafficPattern::New(traffic[c], _nodes, config);
  }

  // ============ Injection queues ============ 

  _qtime.resize(_classes);
  _qdrained.resize(_classes);

  for ( int c = 0; c < _classes; ++c ) {
    _qtime[c].resize(_nodes);
    _qdrained[c].resize(_nodes);
  }

}

SyntheticTrafficManager::~SyntheticTrafficManager( )
{

  for ( int c = 0; c < _classes; ++c ) {
    delete _traffic_pattern[c];
  }
  
}

void SyntheticTrafficManager::_Inject( )
{

  for ( int c = 0; c < _classes; ++c ) {
    for ( int source = 0; source < _nodes; ++source ) {
      // Potentially generate packets for any (source,class)
      // that is currently empty
      if ( _partial_packets[c][source].empty() ) {
	if(_request_class[c] >= 0) {
	  _qtime[c][source] = _time;
	} else {
	  while(_qtime[c][source] <= _time) {
	    ++_qtime[c][source];
	    if(_IssuePacket(source, c)) { //generate a packet
	      _requests_outstanding[c][source]++;
	      _sent_packets[c][source]++;
	      break;
	    }
	  }
	}
	if((_sim_state == draining) && (_qtime[c][source] > _drain_time)) {
	  _qdrained[c][source] = true;
	}
      }
    }
  }
}

bool SyntheticTrafficManager::_PacketsOutstanding( ) const
{
  if(TrafficManager::_PacketsOutstanding()) {
    return true;
  }
  for ( int c = 0; c < _classes; ++c ) {
    if ( _measure_stats[c] ) {
      assert( _measured_in_flight_flits[c].empty() );
      for ( int s = 0; s < _nodes; ++s ) {
	if ( !_qdrained[c][s] ) {
	  return true;
	}
      }
    }
  }
  return false;
}

void SyntheticTrafficManager::_ResetSim( )
{
  TrafficManager::_ResetSim();

  //reset queuetime for all sources and initialize traffic patterns
  for ( int c = 0; c < _classes; ++c ) {
    _qtime[c].assign(_nodes, 0);
    _qdrained[c].assign(_nodes, false);
    _traffic_pattern[c]->reset();
  }
}
