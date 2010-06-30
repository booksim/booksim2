// $Id$

/*
Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
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

/*vc.cpp
 *
 *this class describes a virtual channel in a router
 *it includes buffers and virtual channel state and controls
 *
 *This class calls the routing functions
 */

#include "globals.hpp"
#include "booksim.hpp"
#include "vc.hpp"

int VC::total_cycles = 0;
const char * const VC::VCSTATE[] = {"idle",
				    "routing",
				    "vc_alloc",
				    "active",
				    "vc_spec",
				    "vc_spec_grant"};

VC::state_info_t VC::state_info[] = {{0},
				     {0},
				     {0},
				     {0},
				     {0},
				     {0}};
int VC::occupancy = 0;

VC::VC( const Configuration& config, int outputs ) :
  Module( )
{
  _Init( config, outputs );
}

VC::VC( const Configuration& config, int outputs, 
	Module *parent, const string& name ) :
  Module( parent, name )
{
  _Init( config, outputs );
}

VC::~VC( )
{
}

void VC::_Init( const Configuration& config, int outputs )
{
  _state      = idle;
  _state_time = 0;

  _size = int( config.GetInt( "vc_buf_size" ) );

  _route_set = new OutputSet( outputs );

  _total_cycles    = 0;
  _vc_alloc_cycles = 0;
  _active_cycles   = 0;
  _idle_cycles     = 0;
  _routing_cycles     = 0;

  string priority;
  config.GetStr( "priority", priority );
  if ( priority == "local_age" ) {
    _pri_type = local_age_based;
  } else if ( priority == "queue_length" ) {
    _pri_type = queue_length_based;
  } else if ( priority == "hop_count" ) {
    _pri_type = hop_count_based;
  } else if ( priority == "none" ) {
    _pri_type = none;
  } else {
    _pri_type = other;
  }

  _pri = 0;
  _priority_donation = config.GetInt("vc_priority_donation");

  _out_port = 0 ;
  _out_vc = 0 ;

  _watched = false;
}

bool VC::AddFlit( Flit *f )
{
  assert(f);

  if((int)_buffer.size() >= _size) return false;

  // update flit priority before adding to VC buffer
  if(_pri_type == local_age_based) {
    f->pri = -GetSimTime();
  } else if(_pri_type == hop_count_based) {
    f->pri = f->hops;
  }

  _buffer.push_back(f);
  UpdatePriority();
  return true;
}

Flit *VC::FrontFlit( )
{
  return _buffer.empty() ? NULL : _buffer.front();
}

Flit *VC::RemoveFlit( )
{
  Flit *f = NULL;
  if ( !_buffer.empty( ) ) {
    f = _buffer.front( );
    _buffer.pop_front( );
    UpdatePriority();
  }
  return f;
}



void VC::SetState( eVCState s )
{
  Flit * f = FrontFlit();
  
  if(f && f->watch)
    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		<< "Changing state from " << VC::VCSTATE[_state]
		<< " to " << VC::VCSTATE[s] << "." << endl;
  
  // do not reset state time for speculation-related pseudo state transitions
  if(((_state == vc_alloc) && (s == vc_spec)) ||
     ((_state == vc_spec) && (s == vc_spec_grant))) {
    assert(f);
    if(f->watch)
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		  << "Keeping state time at " << _state_time << "." << endl;
  } else {
    if(f && f->watch)
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		  << "Resetting state time." << endl;
    _state_time = 0;
  }
  
  _state = s;
}

const OutputSet *VC::GetRouteSet( ) const
{
  return _route_set;
}

void VC::SetOutput( int port, int vc )
{
  _out_port = port;
  _out_vc   = vc;
}

void VC::UpdatePriority()
{
  if(_buffer.empty()) return;
  if(_pri_type == queue_length_based) {
    _pri = _buffer.size();
  } else if(_pri_type != none) {
    Flit * f = _buffer.front();
    if((_pri_type != local_age_based) && _priority_donation) {
      Flit * df = f;
      for(int i = 1; i < _buffer.size(); ++i) {
	Flit * bf = _buffer[i];
	if(bf->pri > df->pri) df = bf;
      }
      if((df != f) && (df->watch || f->watch)) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		    << "Flit " << df->id
		    << " donates priority to flit " << f->id
		    << "." << endl;
      }
      f = df;
    }
    if(f->watch)
      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		  << "Flit " << f->id
		  << " sets priority to " << f->pri
		  << "." << endl;
    _pri = f->pri;
  }
}


int VC::GetSize() const
{
  return (int)_buffer.size();
}

void VC::Route( tRoutingFunction rf, const Router* router, const Flit* f, int in_channel )
{  
  rf( router, f, in_channel, _route_set, false );
}

void VC::AdvanceTime( )
{
  if(!Empty()) {
    _state_time++;
  }
  
  total_cycles++;
  switch( _state ) {
  case idle          : _idle_cycles++; break;
  case active        : _active_cycles++; break;
  case vc_spec_grant : _active_cycles++; break;
  case vc_alloc      : _vc_alloc_cycles++; break;
  case vc_spec       : _vc_alloc_cycles++; break;
  case routing       : _routing_cycles++; break;
  }
  state_info[_state].cycles++;
  occupancy += _buffer.size();
}

// ==== Debug functions ====

void VC::SetWatch( bool watch )
{
  _watched = watch;
}

bool VC::IsWatched( ) const
{
  return _watched;
}

void VC::Display( ) const
{
  if ( _state != VC::idle ) {
    cout << FullName() << ": "
	 << " state: " << VCSTATE[_state]
	 << " out_port: " << _out_port
	 << " out_vc: " << _out_vc 
	 << " fill: " << _buffer.size() 
	 << endl ;
  }
}

void VC::DisplayStats( bool print_csv )
{
  if(print_csv) {
    for(eVCState state = state_min; state <= state_max; state = eVCState(state+1)) {
      cout << (float)state_info[state].cycles/(float)total_cycles << ",";
    }
    cout << (float)occupancy/(float)total_cycles << endl;
  }
  cout << "VC state breakdown:" << endl;
  for(eVCState state = state_min; state <= state_max; state = eVCState(state+1)) {
    cout << "  " << VCSTATE[state]
	 << ": " << (float)state_info[state].cycles/(float)total_cycles << endl;
  }
  cout << "  occupancy: " << (float)occupancy/(float)total_cycles << endl;
}
