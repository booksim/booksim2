// $Id$

/*
Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
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

#include <limits>

#include "packet_reply_info.hpp"
#include "random_utils.hpp"
#include "batchtrafficmanager.hpp"

BatchTrafficManager::BatchTrafficManager( const Configuration &config, 
					  const vector<Booksim_Network *> & net )
: TrafficManager(config, net), _last_batch_time(-1), _last_id(-1), _last_pid(-1)
{

  _batch_size = config.GetInt( "batch_size" );
  _batch_count = config.GetInt( "batch_count" );

  _batch_time = new Stats( this, "batch_time" );
  _stats["batch_time"] = _batch_time;
  
  _overall_batch_time = new Stats( this, "overall_batch_time" );
  _stats["overall_batch_time"] = _overall_batch_time;
  
}

BatchTrafficManager::~BatchTrafficManager( )
{
  delete _batch_time;
  delete _overall_batch_time;
}

void BatchTrafficManager::_RetireFlit( Flit *f, int dest )
{
  _last_id = f->id;
  _last_pid = f->pid;
  TrafficManager::_RetireFlit(f, dest);
}

int BatchTrafficManager::_IssuePacket( int source, int cl )
{
  int result;
  if(_use_read_write[cl]) { //read write packets
    //check queue for waiting replies.
    //check to make sure it is on time yet
    int pending_time = numeric_limits<int>::max(); //reset to maxtime+1
    if (!_repliesPending[source].empty()) {
      result = _repliesPending[source].front();
      pending_time = _repliesDetails.find(result)->second->time;
    }
    if (pending_time<=_qtime[source][cl]) {
      result = _repliesPending[source].front();
      _repliesPending[source].pop_front();
      
    } else if ((_sent_packets[source] >= _batch_size) || 
	       ((_maxOutstanding > 0) && 
		(_requestsOutstanding[source] >= _maxOutstanding))) {
      result = 0;
    } else {
      
      //coin toss to determine request type.
      result = (RandomFloat() < 0.5) ? -2 : -1;
      
      _sent_packets[source]++;
      _requestsOutstanding[source]++;
    } 
  } else { //normal
    if ((_sent_packets[source] >= _batch_size) || 
	((_maxOutstanding > 0) && 
	 (_requestsOutstanding[source] >= _maxOutstanding))) {
      result = 0;
    } else {
      result = _packet_size[cl];
      _sent_packets[source]++;
      _requestsOutstanding[source]++;
    } 
  } 
  return result;
}

bool BatchTrafficManager::_SingleSim( )
{
  int batch_index = 0;
  while(batch_index < _batch_count) {
    _sent_packets.assign(_nodes, 0);
    _last_id = -1;
    _last_pid = -1;
    _sim_state = running;
    int start_time = _time;
    bool batch_complete;
    do {
      _Step();
      batch_complete = true;
      for(int i = 0; i < _nodes; ++i) {
	if(_sent_packets[i] < _batch_size) {
	  batch_complete = false;
	  break;
	}
      }
      if(_sent_packets_out) {
	*_sent_packets_out << "sent_packets(" << _time << ",:) = " << _sent_packets << ";" << endl;
      }
    } while(!batch_complete);
    cout << "Batch " << batch_index + 1 << " ("<<_batch_size  <<  " packets) sent. Time used is " << _time - start_time << " cycles." << endl;
    cout << "Draining the Network...................\n";
    _sim_state = draining;
    _drain_time = _time;
    int empty_steps = 0;
    
    bool packets_left = false;
    for(int c = 0; c < _classes; ++c) {
      packets_left |= !_total_in_flight_flits[c].empty();
    }
    
    while( packets_left ) { 
      _Step( ); 
      
      ++empty_steps;
      
      if ( empty_steps % 1000 == 0 ) {
	_DisplayRemaining( ); 
	cout << ".";
      }
      
      packets_left = false;
      for(int c = 0; c < _classes; ++c) {
	packets_left |= !_total_in_flight_flits[c].empty();
      }
    }
    cout << endl;
    cout << "Batch " << batch_index + 1 << " ("<<_batch_size  <<  " packets) received. Time used is " << _time - _drain_time << " cycles. Last packet was " << _last_pid << ", last flit was " << _last_id << "." <<endl;

    _last_batch_time = _time - start_time;

    _batch_time->AddSample(_last_batch_time);

    cout << _sim_state << endl;

    if(_stats_out)
      *_stats_out << "%=================================" << endl;
    
    DisplayStats();
        
    if(_stats_out) {
      *_stats_out << "batch_time = " << _last_batch_time << ";" << endl;
    }

    ++batch_index;
  }
  return 1;
}

void BatchTrafficManager::_UpdateOverallStats() {
  TrafficManager::_UpdateOverallStats();
  _overall_batch_time->AddSample(_batch_time->Sum( ));
}
  
void BatchTrafficManager::DisplayStats(ostream & os) {
  os << "Batch duration = " << _last_batch_time << endl;
  TrafficManager::DisplayStats();
}

void BatchTrafficManager::DisplayOverallStats(ostream & os) const {
  os << "Overall batch duration = " << _overall_batch_time->Average( )
     << " (" << _overall_batch_time->NumSamples( ) << " samples)" << endl;
  TrafficManager::DisplayOverallStats(os);
}
