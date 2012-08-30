// $Id$

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
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

#include <sstream>

#include "workloadtrafficmanager.hpp"

WorkloadTrafficManager::WorkloadTrafficManager( const Configuration &config, 
						const vector<Network *> & net )
  : TrafficManager(config, net), _overall_runtime(0)
{
  _sample_period = config.GetInt( "sample_period" );
  _max_samples    = config.GetInt( "max_samples" );
  _warmup_periods = config.GetInt( "warmup_periods" );

  vector<string> workload = config.GetStrArray("workload");
  workload.resize(_classes, workload.back());

  _workload.resize(_classes);
  for(int c = 0; c < _classes; ++c) {
    Workload * wl = Workload::New(workload[c], _nodes, &config);
    assert(wl);
    _workload[c] = wl;
  }
}

WorkloadTrafficManager::~WorkloadTrafficManager( )
{
  for(int c = 0; c < _classes; ++c) {
    delete _workload[c];
  }
}

void WorkloadTrafficManager::_Inject( )
{
  for(int c = 0; c < _classes; ++c) {
    Workload * const wl = _workload[c];
    while(!wl->empty()) {
      int const source = wl->source();
      if(_partial_packets[c][source].empty()) {
	++_requests_outstanding[c][source];
	++_packet_seq_no[c][source];
	int const dest = wl->dest();
	int const size = wl->size();
	int const time = (_include_queuing == 1) ? wl->time() : _time;
	int const pid = _GeneratePacket(source, dest, size, c, time);
	wl->inject(pid);
      } else {
	wl->defer();
      }
    }
    wl->advanceTime();
  }
}

void WorkloadTrafficManager::_RetirePacket( Flit * head, Flit * tail )
{
  TrafficManager::_RetirePacket( head, tail );
  _workload[head->cl]->retire(head->pid);
}

void WorkloadTrafficManager::_ResetSim( )
{
  TrafficManager::_ResetSim( );

  for(int c = 0; c < _classes; ++c) {
    _workload[c]->reset();
  }
}

bool WorkloadTrafficManager::_SingleSim( )
{
  _sim_state = warming_up;
  
  if(_warmup_periods > 0) {
    
    cout << "Warming up..." << endl;
    
    while(_time < _warmup_periods * _sample_period) {
      
      _Step();
      
      if((_time % _sample_period) == 0) {
	UpdateStats();
	DisplayStats();
      }

    }

    _ClearStats();
    
    cout << "Warmup ends after " << _warmup_periods * _sample_period
	 << " cycles." << endl;
    
  }

  _sim_state = running;
  
  cout << "Beginning measurements..." << endl;
    
  while(!_Completed() && 
	((_max_samples < 0) || 
	 (_time < (_warmup_periods + _max_samples) * _sample_period))) {
    
    _Step();
    
    if((_time % _sample_period) == 0) {
      UpdateStats();
      DisplayStats();
    }

  }

  cout << "Completed measurements after " << _time << " cycles." << endl;

  _sim_state = draining;
  _drain_time = _time;

  return 1;
}

bool WorkloadTrafficManager::_Completed( )
{
  for(int c = 0; c < _classes; ++c) {
    if(_measure_stats[c] &&
       (!_workload[c]->completed() || !_measured_in_flight_flits[c].empty())) {
      return false;
    }
  }
  return true;
}

void WorkloadTrafficManager::_UpdateOverallStats()
{
  TrafficManager::_UpdateOverallStats();
  _overall_runtime += (_drain_time - _reset_time);
}
  
string WorkloadTrafficManager::_OverallStatsHeaderCSV() const
{
  ostringstream os;
  os << TrafficManager::_OverallStatsHeaderCSV()
     << ',' << "runtime";
  return os.str();
}

string WorkloadTrafficManager::_OverallClassStatsCSV(int c) const
{
  ostringstream os;
  os << TrafficManager::_OverallClassStatsCSV(c)
     << ',' << (double)_overall_runtime / (double)_total_sims;
  return os.str();
}

void WorkloadTrafficManager::_DisplayClassStats(int c, ostream & os) const
{
  _workload[c]->printStats(os);
  TrafficManager::_DisplayClassStats(c, os);
}

void WorkloadTrafficManager::_DisplayOverallClassStats(int c, ostream & os) const
{
  TrafficManager::_DisplayOverallClassStats(c, os);
  os << "Overall workload runtime = " << (double)_overall_runtime / (double)_total_sims
     << " (" << _total_sims << " samples)" << endl;
}
