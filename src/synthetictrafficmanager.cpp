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

#include <sstream>

#include "synthetictrafficmanager.hpp"

SyntheticTrafficManager::SyntheticTrafficManager( const Configuration &config, const vector<Network *> & net )
: TrafficManager(config, net)
{

  // ============ Traffic ============ 

  _traffic = config.GetStrArray("traffic");
  _traffic.resize(_classes, _traffic.back());

  _traffic_pattern.resize(_classes);
  for(int c = 0; c < _classes; ++c) {
    _traffic_pattern[c] = TrafficPattern::New(_traffic[c], _nodes, config);
  }

  _packet_size = config.GetIntArray( "packet_size" );
  if(_packet_size.empty()) {
    _packet_size.push_back(config.GetInt("packet_size"));
  }
  _packet_size.resize(_classes, _packet_size.back());
  
  _reply_class = config.GetIntArray("reply_class"); 
  if(_reply_class.empty()) {
    _reply_class.push_back(config.GetInt("reply_class"));
  }
  _reply_class.resize(_classes, _reply_class.back());

  _request_class.resize(_classes, -1);
  for(int c = 0; c < _classes; ++c) {
    int const & reply_class = _reply_class[c];
    if(reply_class >= 0) {
      assert(_request_class[reply_class] < 0);
      _request_class[reply_class] = c;
    }
  }

  // ============ Injection queues ============ 

  _qtime.resize(_classes);
  _qdrained.resize(_classes);

  for ( int c = 0; c < _classes; ++c ) {
    _qtime[c].resize(_nodes);
    _qdrained[c].resize(_nodes);
  }

  // ============ Statistics ============ 

  _tlat_stats.resize(_classes);
  _overall_min_tlat.resize(_classes, 0.0);
  _overall_avg_tlat.resize(_classes, 0.0);
  _overall_max_tlat.resize(_classes, 0.0);

  _pair_tlat.resize(_classes);

  for ( int c = 0; c < _classes; ++c ) {
    ostringstream tmp_name;

    tmp_name << "tlat_stat_" << c;
    _tlat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
    _stats[tmp_name.str()] = _tlat_stats[c];
    tmp_name.str("");

    _pair_tlat[c].resize(_nodes*_nodes);
    for ( int i = 0; i < _nodes; ++i ) {
      for ( int j = 0; j < _nodes; ++j ) {
	tmp_name << "pair_tlat_stat_" << c << "_" << i << "_" << j;
	_pair_tlat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
	_stats[tmp_name.str()] = _pair_tlat[c][i*_nodes+j];
	tmp_name.str("");
      }
    }
  }
}

SyntheticTrafficManager::~SyntheticTrafficManager( )
{
  for ( int c = 0; c < _classes; ++c ) {
    delete _traffic_pattern[c];
    delete _tlat_stats[c];
    for ( int source = 0; source < _nodes; ++source ) {
      for ( int dest = 0; dest < _nodes; ++dest ) {
	delete _pair_tlat[c][source*_nodes+dest];
      }
    }
  }
}

void SyntheticTrafficManager::_RetirePacket(Flit * head, Flit * tail, int dest)
{
  int const & reply_class = _reply_class[tail->cl];
  assert(reply_class < _classes);
  
  if (reply_class < 0) {
    if ( tail->watch ) { 
      *gWatchOut << GetSimTime() << " | "
		 << "node" << dest << " | "
		 << "Completing transation " << tail->tid
		 << " (lat = " << tail->atime - head->ttime
		 << ", src = " << head->src 
		 << ", dest = " << head->dest
		 << ")." << endl;
    }
    int const & request_class = _request_class[tail->cl];
    assert(request_class < _classes);
    if(request_class < 0) {
      // single-packet transactions "magically" notify source of completion 
      // when packet arrives at destination
      _requests_outstanding[tail->cl][tail->src]--;
    } else {
      // request-reply transactions complete when reply arrives
      _requests_outstanding[request_class][dest]--;
    }
    
    // Only record statistics once per packet (at tail)
    // and based on the simulation state
    if ( ( _sim_state == warming_up ) || tail->record ) {
      int const cl = (request_class < 0) ? tail->cl : request_class;
      _tlat_stats[cl]->AddSample( tail->atime - tail->ttime );
      _pair_tlat[cl][dest*_nodes+tail->src]->AddSample( tail->atime - tail->ttime );
    }
    
  } else {
    _sent_packets[tail->cl][dest]++;
    _GeneratePacket( head->dest, head->src, _packet_size[reply_class], 
		     reply_class, tail->atime + 1, tail->tid, tail->ttime );
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

void SyntheticTrafficManager::_ClearStats( )
{
  for ( int c = 0; c < _classes; ++c ) {
    _tlat_stats[c]->Clear( );
    for ( int i = 0; i < _nodes; ++i ) {
      for ( int j = 0; j < _nodes; ++j ) {
	_pair_tlat[c][i*_nodes+j]->Clear( );
      }
    }
  }
}

void SyntheticTrafficManager::_UpdateOverallStats( )
{
  TrafficManager::_UpdateOverallStats();
  for ( int c = 0; c < _classes; ++c ) {
    if(_measure_stats[c] == 0) {
      continue;
    }
    assert(_tlat_stats[c]->NumSamples() > 0);
    _overall_min_tlat[c] += _tlat_stats[c]->Min();
    _overall_avg_tlat[c] += _tlat_stats[c]->Average();
    _overall_max_tlat[c] += _tlat_stats[c]->Max();
  }
}

string SyntheticTrafficManager::_OverallStatsHeaderCSV() const
{
  ostringstream os;
  os << "traffic"
     << ',' << "psize"
     << ',' << TrafficManager::_OverallStatsHeaderCSV()
     << ',' << "min_tlat"
     << ',' << "avg_tlat"
     << ',' << "max_tlat";
  return os.str();
}

string SyntheticTrafficManager::_OverallClassStatsCSV(int c) const
{
  ostringstream os;
  os << _traffic[c] << ','
     << _packet_size[c] << ','
     << TrafficManager::_OverallClassStatsCSV(c)
     << ',' << _overall_min_tlat[c] / (double)_total_sims
     << ',' << _overall_avg_tlat[c] / (double)_total_sims
     << ',' << _overall_max_tlat[c] / (double)_total_sims;
  return os.str();
}

void SyntheticTrafficManager::_WriteClassStats(int c, ostream & os) const
{
  TrafficManager::_WriteClassStats(c, os);
  os << "pair_tlat(" << c+1 << ",:) = [ ";
  for(int i = 0; i < _nodes; ++i) {
    for(int j = 0; j < _nodes; ++j) {
      os << _pair_tlat[c][i*_nodes+j]->Average( ) << " ";
    }
  }
  os << "];" << endl;
}

void SyntheticTrafficManager::_DisplayOverallClassStats(int c, ostream & os) const
{
  TrafficManager::_DisplayOverallClassStats(c, os);
  os << "Overall minimum transaction latency = " << _overall_min_tlat[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall average transaction latency = " << _overall_avg_tlat[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl
     << "Overall maximum transaction latency = " << _overall_max_tlat[c] / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl;
}
