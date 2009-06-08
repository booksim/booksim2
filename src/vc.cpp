// $Id$

/*
Copyright (c) 2007, Trustees of Leland Stanford Junior University
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
VC::state_info_t VC::state_info[] = {{"idle", 0},
				     {"routing", 0},
				     {"vc_alloc", 0},
				     {"active", 0},
				     {"vc_spec", 0},
				     {"vc_spec_grant", 0}};
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

  _occupied_cnt = 0;

  _total_cycles    = 0;
  _vc_alloc_cycles = 0;
  _active_cycles   = 0;
  _idle_cycles     = 0;
  _routing_cycles     = 0;

  _pri = 0;

  _out_port = 0 ;
  _out_vc = 0 ;

  _watched = false;
}

bool VC::AddFlit( Flit *f )
{
  bool success = false;

  if ( (int)_buffer.size( ) != _size ) {
    _buffer.push( f );
    success = true;
  } 

  return success;
}

Flit *VC::FrontFlit( )
{
  Flit *f;

  if ( !_buffer.empty( ) ) {
    f = _buffer.front( );
  } else {
    f = 0;
  }

  return f;
}

Flit *VC::RemoveFlit( )
{
  Flit *f;

  if ( !_buffer.empty( ) ) {
    f = _buffer.front( );
    _buffer.pop( );
  } else {
    f = 0;
  }

  return f;
}

bool VC::Empty( ) const
{
  return _buffer.empty( );
}

bool VC::Full( ) const
{
  return (int)_buffer.size( ) == _size;
}

VC::eVCState VC::GetState( ) const
{
  return _state;
}

int VC::GetStateTime( ) const
{
  return _state_time;
}

void VC::SetState( eVCState s )
{
  Flit * f = FrontFlit();

  if(f && f->watch)
    cout << "VC " << _fullname << " changed state"
	 << " at time " << GetSimTime() << endl
	 << "  Old: " << state_info[_state].name
	 << " New: " << state_info[s].name << endl;
  
  // do not reset state time for speculation-related pseudo state transitions
  if(!((_state == vc_spec) && (s == vc_spec_grant)) &&
     !((_state == vc_spec_grant) && (s == active)))
    _state_time = 0;
  
  if ( (_state == idle) && (s != idle) ) {
    if ( f ) {
      _pri = f->pri;
    }

    _occupied_cnt++;
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

int VC::GetOutputPort( ) const
{
  return _out_port;
}

int VC::GetOutputVC( ) const
{
  return _out_vc;
}

int VC::GetPriority( ) const
{
  return _pri;
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
  _state_time++;

  _total_cycles++; total_cycles++;
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
//  cout << _fullname << " : "
//       << "idle " << 100.0 * (double)_idle_cycles / (double)_total_cycles << "% "
//       << "vc_alloc " << 100.0 * (double)_vc_alloc_cycles / (double)_total_cycles << "% "
//       << "active " << 100.0 * (double)_active_cycles / (double)_total_cycles << "% "
//       << endl;
  if ( _state != VC::idle ) {
    cout << _fullname << ": "
	 << " state: " << state_info[_state].name
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
    cout << "  " << state_info[state].name
	 << ": " << (float)state_info[state].cycles/(float)total_cycles << endl;
  }
  cout << "  occupancy: " << (float)occupancy/(float)total_cycles << endl;
}
