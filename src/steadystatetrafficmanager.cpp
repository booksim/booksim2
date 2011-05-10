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

#include <cmath>

#include "steadystatetrafficmanager.hpp"

SteadyStateTrafficManager::SteadyStateTrafficManager( const Configuration &config, const vector<Network *> & net )
: TrafficManager(config, net)
{
  _load = config.GetFloatArray("injection_rate"); 
  if(_load.empty()) {
    _load.push_back(config.GetFloat("injection_rate"));
  }
  _load.resize(_classes, _load.back());

  if(config.GetInt("injection_rate_uses_flits")) {
    for(int c = 0; c < _classes; ++c)
      _load[c] /= (double)_packet_size[c];
  }

  vector<string> injection_process = config.GetStrArray("injection_process");
  injection_process.resize(_classes, injection_process.back());

  _injection_process.resize(_classes);
  for(int c = 0; c < _classes; ++c) {
    _injection_process[c].resize(_nodes);
    for(int n = 0; n < _nodes; ++n) {
      _injection_process[c][n] = InjectionProcess::New(injection_process[c], _load[c]);
    }
  }

  _measure_latency = (config.GetStr("sim_type") == "latency");

  _sample_period = config.GetInt( "sample_period" );
  _max_samples    = config.GetInt( "max_samples" );
  _warmup_periods = config.GetInt( "warmup_periods" );

  _latency_thres = config.GetFloatArray( "latency_thres" );
  if(_latency_thres.empty()) {
    _latency_thres.push_back(config.GetFloat("latency_thres"));
  }
  _latency_thres.resize(_classes, _latency_thres.back());

  _warmup_threshold = config.GetFloatArray( "warmup_thres" );
  if(_warmup_threshold.empty()) {
    _warmup_threshold.push_back(config.GetFloat("warmup_thres"));
  }
  _warmup_threshold.resize(_classes, _warmup_threshold.back());

  _acc_warmup_threshold = config.GetFloatArray( "acc_warmup_thres" );
  if(_acc_warmup_threshold.empty()) {
    _acc_warmup_threshold.push_back(config.GetFloat("acc_warmup_thres"));
  }
  _acc_warmup_threshold.resize(_classes, _acc_warmup_threshold.back());

  _stopping_threshold = config.GetFloatArray( "stopping_thres" );
  if(_stopping_threshold.empty()) {
    _stopping_threshold.push_back(config.GetFloat("stopping_thres"));
  }
  _stopping_threshold.resize(_classes, _stopping_threshold.back());

  _acc_stopping_threshold = config.GetFloatArray( "acc_stopping_thres" );
  if(_acc_stopping_threshold.empty()) {
    _acc_stopping_threshold.push_back(config.GetFloat("acc_stopping_thres"));
  }
  _acc_stopping_threshold.resize(_classes, _acc_stopping_threshold.back());
}

SteadyStateTrafficManager::~SteadyStateTrafficManager( )
{
  for ( int c = 0; c < _classes; ++c ) {
    for ( int source = 0; source < _nodes; ++source ) {
      delete _injection_process[c][source];
    }
  }
}

bool SteadyStateTrafficManager::_IssuePacket( int source, int cl )
{
  if(_injection_process[cl][source]->test()) {
    int dest = _traffic_pattern[cl]->dest(source);
    int size = _packet_size[cl];
    int time = ((_include_queuing == 1) ? _qtime[cl][source] : _time);
    _GeneratePacket(source, dest, size, cl, time, -1, time);
    return true;
  }
  return false;
}

bool SteadyStateTrafficManager::_SingleSim( )
{
  int converged = 0;
  
  //once warmed up, we require 3 converging runs to end the simulation 
  vector<double> prev_latency(_classes, 0.0);
  vector<double> prev_accepted(_classes, 0.0);
  bool clear_last = false;
  int total_phases = 0;
  while( ( total_phases < _max_samples ) && 
	 ( ( _sim_state != running ) || 
	   ( converged < 3 ) ) ) {
    
    if ( clear_last || (( ( _sim_state == warming_up ) && ( ( total_phases % 2 ) == 0 ) )) ) {
      clear_last = false;
      _ClearStats( );
    }
    
    
    for ( int iter = 0; iter < _sample_period; ++iter )
      _Step( );
    
    cout << _sim_state << endl;
    if(_stats_out)
      *_stats_out << "%=================================" << endl;
    
    DisplayStats();
    
    int lat_exc_class = -1;
    int lat_chg_exc_class = -1;
    int acc_chg_exc_class = -1;
    
    for(int c = 0; c < _classes; ++c) {
      
      if(_measure_stats[c] == 0) {
	continue;
      }

      double cur_latency = _plat_stats[c]->Average( );

      double min, avg;
      _ComputeStats( _accepted_flits[c], &avg, &min );
      double cur_accepted = avg;

      double latency_change = fabs((cur_latency - prev_latency[c]) / cur_latency);
      prev_latency[c] = cur_latency;

      double accepted_change = fabs((cur_accepted - prev_accepted[c]) / cur_accepted);
      prev_accepted[c] = cur_accepted;

      double latency = (double)_plat_stats[c]->Sum();
      double count = (double)_plat_stats[c]->NumSamples();
      
      map<int, Flit *>::const_iterator iter;
      for(iter = _total_in_flight_flits[c].begin(); 
	  iter != _total_in_flight_flits[c].end(); 
	  iter++) {
	latency += (double)(_time - iter->second->time);
	count++;
      }
      
      if((lat_exc_class < 0) &&
	 (_latency_thres[c] >= 0.0) &&
	 ((latency / count) > _latency_thres[c])) {
	lat_exc_class = c;
      }
      
      cout << "class " << c << " latency change    = " << latency_change << endl;
      if(lat_chg_exc_class < 0) {
	if((_sim_state == warming_up) &&
	   (_warmup_threshold[c] >= 0.0) &&
	   (latency_change > _warmup_threshold[c])) {
	  lat_chg_exc_class = c;
	} else if((_sim_state == running) &&
		  (_stopping_threshold[c] >= 0.0) &&
		  (latency_change > _stopping_threshold[c])) {
	  lat_chg_exc_class = c;
	}
      }
      
      cout << "class " << c << " throughput change = " << accepted_change << endl;
      if(acc_chg_exc_class < 0) {
	if((_sim_state == warming_up) &&
	   (_acc_warmup_threshold[c] >= 0.0) &&
	   (accepted_change > _acc_warmup_threshold[c])) {
	  acc_chg_exc_class = c;
	} else if((_sim_state == running) &&
		  (_acc_stopping_threshold[c] >= 0.0) &&
		  (accepted_change > _acc_stopping_threshold[c])) {
	  acc_chg_exc_class = c;
	}
      }
      
    }
    
    // Fail safe for latency mode, throughput will ust continue
    if ( _measure_latency && ( lat_exc_class >= 0 ) ) {
      
      cout << "Average latency for class " << lat_exc_class << " exceeded " << _latency_thres[lat_exc_class] << " cycles. Aborting simulation." << endl;
      converged = 0; 
      _sim_state = warming_up;
      break;
      
    }
    
    if ( _sim_state == warming_up ) {
      if ( ( _warmup_periods > 0 ) ? 
	   ( total_phases + 1 >= _warmup_periods ) :
	   ( ( !_measure_latency || ( lat_chg_exc_class < 0 ) ) &&
	     ( acc_chg_exc_class < 0 ) ) ) {
	cout << "Warmed up ..." <<  "Time used is " << _time << " cycles" <<endl;
	clear_last = true;
	_sim_state = running;
      }
    } else if(_sim_state == running) {
      if ( ( !_measure_latency || ( lat_chg_exc_class < 0 ) ) &&
	   ( acc_chg_exc_class < 0 ) ) {
	++converged;
      } else {
	converged = 0;
      }
    }
    ++total_phases;
  }
  
  if ( _sim_state == running ) {
    ++converged;
    
    if ( _measure_latency ) {
      cout << "Draining all recorded packets ..." << endl;
      _sim_state  = draining;
      _drain_time = _time;
      int empty_steps = 0;
      while( _PacketsOutstanding( ) ) { 
	_Step( ); 
	
	++empty_steps;
	
	if ( empty_steps % 1000 == 0 ) {
	  
	  int lat_exc_class = -1;
	  
	  for(int c = 0; c < _classes; c++) {
	    
	    double threshold = _latency_thres[c];
	    
	    if(threshold < 0.0) {
	      continue;
	    }
	    
	    double acc_latency = _plat_stats[c]->Sum();
	    double acc_count = (double)_plat_stats[c]->NumSamples();
	    
	    map<int, Flit *>::const_iterator iter;
	    for(iter = _total_in_flight_flits[c].begin(); 
		iter != _total_in_flight_flits[c].end(); 
		iter++) {
	      acc_latency += (double)(_time - iter->second->time);
	      acc_count++;
	    }
	    
	    if((acc_latency / acc_count) > threshold) {
	      lat_exc_class = c;
	      break;
	    }
	  }
	  
	  if(lat_exc_class >= 0) {
	    cout << "Average latency for class " << lat_exc_class << " exceeded " << _latency_thres[lat_exc_class] << " cycles. Aborting simulation." << endl;
	    converged = 0; 
	    _sim_state = warming_up;
	    break;
	  }
	  
	  _DisplayRemaining( ); 
	  
	}
      }
    }
  } else {
    cout << "Too many sample periods needed to converge" << endl;
  }
  
  return ( converged > 0 );
}
