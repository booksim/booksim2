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

/*vc.cpp
 *
 *this class describes a virtual channel in a router
 *it includes buffers and virtual channel state and controls
 *
 *This class calls the routing functions
 */

#include <limits>
#include <sstream>

#include "globals.hpp"
#include "booksim.hpp"
#include "vc.hpp"

const char * const VC::VCSTATE[] = {"idle",
				    "routing",
				    "vc_alloc",
				    "active"};
int VC::invert_cycles = 0;
int VC::total_cycles = 0;
VC::VC( const Configuration& config, int outputs, 
	Module *parent, const string& name )
  : Module( parent, name ), 
    _state(idle), _out_port(-1), _out_vc(-1), _pri(0),_note(0), _watched(false), 
    _expected_pid(-1), _last_id(-1), _last_pid(-1)
{
  _route_set = new OutputSet( );

  string priority = config.GetStr( "priority" );
  if ( priority == "local_age" ) {
    _pri_type = local_age_based;
  } else if ( priority == "queue_length" ) {
    _pri_type = queue_length_based;
  } else if ( priority == "hop_count" ) {
    _pri_type = hop_count_based;
  } else if ( priority == "none" ) {
    if(config.GetStr("vc_alloc_arb_type")=="prob" || config.GetStr("sw_alloc_arb_type")=="prob"){
      _pri_type= none_prob;
    } else  {
      _pri_type = none;
    }
  } else if ( priority == "notification" ) {
    _pri_type = forward_note;
  } else {
    _pri_type = other;
  }

  _priority_donation = config.GetInt("vc_priority_donation");
  _queuing_age = (config.GetInt("queuing_age")==1) && priority == "age";
  _pri_granularity = config.GetInt("age_granularity");
  _pri_cap = config.GetInt("age_cap");
  _hop_offset = config.GetInt("hop_offset");
}

VC::~VC()
{
  delete _route_set;
}

void VC::AddFlit( Flit *f )
{
  assert(f);

  if(_expected_pid >= 0) {
    if(f->pid != _expected_pid) {
      ostringstream err;
      err << "Received flit " << f->id << " with unexpected packet ID: " << f->pid 
	  << " (expected: " << _expected_pid << ")";
      Error(err.str());
    } else if(f->tail) {
      _expected_pid = -1;
    }
  } else if(!f->tail) {
    _expected_pid = f->pid;
  }
    
  // update flit priority before adding to VC buffer
  if(_pri_type == local_age_based) {
    f->pri = numeric_limits<int>::max() - GetSimTime();
    assert(f->pri >= 0);
  } else if(_pri_type == hop_count_based) {
    f->pri = f->hops;
    assert(f->pri >= 0);
  }

  _buffer.push_back(f);
  UpdatePriority();
}

Flit *VC::RemoveFlit( )
{
  Flit *f = NULL;
  if ( !_buffer.empty( ) ) {
    f = _buffer.front( );
    _buffer.pop_front( );
    _last_id = f->id;
    _last_pid = f->pid;
    UpdatePriority();
  } else {
    Error("Trying to remove flit from empty buffer.");
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
  total_cycles++;
  if(_buffer.empty()) return;
  if(_pri_type == queue_length_based) {
    _pri = _buffer.size();
  } else if(_pri_type == none_prob){
    _pri = 1;
  } else if(_pri_type == forward_note){
    //forward notifcation
    Flit * f = _buffer.front();
    if(f->head){
      if( _priority_donation) {
	Flit * df = f;
	//search for top priority
	for(size_t i = 1; i < _buffer.size(); ++i) {
	  Flit * bf = _buffer[i];
	  if(bf->head && bf->notification > df->notification) 
	    df = bf;
	}
	f = df;
      }
      
      _pri= (f->notification == 0)?1:f->notification;
      _pri= (_pri_cap!=-1 && _pri>_pri_cap )?_pri_cap:_pri;
      _note = f->notification;
      
      int top_note = _note;
      for(size_t i = 1; i < _buffer.size(); ++i) {
	if(_buffer[i]->head && _buffer[i]->notification >top_note){
	  invert_cycles++;
	  break;
	}
      }

      if(f->watch)
	*gWatchOut << GetSimTime() << " | " << FullName() << " | "
		   << "Flit " << f->id
		   << " sets priority to " << _pri
		   << " sets notification to "<<_note
		   << "." << endl;
    } else {
      int max_note = _note;
      if( _priority_donation) {
	//search for top priority
	for(size_t i = 1; i < _buffer.size(); ++i) {
	  Flit * bf = _buffer[i];
	  if(bf->head && bf->notification > max_note) {
	    max_note  = bf->notification;
	  }
	}
      }
      
      _pri= (max_note== 0)?1:max_note;
      _pri= (_pri_cap!=-1 && _pri>_pri_cap )?_pri_cap:_pri;
      _note = max_note;
      
      int top_note = _note;
      for(size_t i = 1; i < _buffer.size(); ++i) {
	if(_buffer[i]->head && _buffer[i]->notification >top_note){
	  invert_cycles++;
	  break;
	}
      }
    }
  }else if(_pri_type != none) {
    Flit * f = _buffer.front();
    if((_pri_type != local_age_based) && _priority_donation) {
      Flit * df = f;
      for(size_t i = 1; i < _buffer.size(); ++i) {
	Flit * bf = _buffer[i];
	if(_queuing_age){
	  if(((bf->pri -bf->hops*_hop_offset)>>_pri_granularity) > ((df->pri-df->hops*_hop_offset)>>_pri_granularity)) 
	    df = bf;
	} else {
	  if((bf->pri>>_pri_granularity) > (df->pri>>_pri_granularity)) 
	    df = bf;
	}
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
    //_pri = f->pri;
    _pri = (f->pri - (_queuing_age?_hop_offset*f->hops:0))>>_pri_granularity;
    int current_p = (numeric_limits<int>::max() -GetSimTime())>>_pri_granularity;;
    if(_pri_cap!=-1 && current_p+_pri_cap<_pri){
      _pri = current_p+_pri_cap;
    }
    int top_pri =  (_buffer.front()->pri  - (_queuing_age?_hop_offset*_buffer.front()->hops:0))>>_pri_granularity;
    for(size_t i = 1; i < _buffer.size(); ++i) {
      if((_buffer[i]->pri-(_queuing_age?_hop_offset*_buffer[i]->hops:0))>>_pri_granularity >top_pri){
        invert_cycles++;
        break;
      }
    }
  }
}


void VC::Route( tRoutingFunction rf, const Router* router, const Flit* f, int in_channel )
{
  rf( router, f, in_channel, _route_set, false );
  _out_port = -1;
  _out_vc = -1;
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

void VC::Display( ostream & os ) const
{
  if ( _state != VC::idle ) {
    os << FullName() << ": "
       << " state: " << VCSTATE[_state];
    if(_state == VC::active) {
      os << " out_port: " << _out_port
	 << " out_vc: " << _out_vc;
    }
    os << " fill: " << _buffer.size();
    if(!_buffer.empty()) {
      os << " front: " << _buffer.front()->id;
    }
    os << " pri: " << _pri;
    os << endl;
  }
}
