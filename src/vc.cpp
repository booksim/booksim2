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

/*vc.cpp
 *
 *this class describes a virtual channel in a router
 *it includes buffers and virtual channel state and controls
 *
 *This class calls the routing functions
 */

#include <limits>
#include <sstream>

#include "reservation.hpp"
#include "globals.hpp"
#include "booksim.hpp"
#include "vc.hpp"


stack<VC::VCData*> VC::VCData::_all;
stack<VC::VCData*> VC::VCData::_free;

void VC::VCData::Free(){
  assert(_use);
  _use=false;
  _free.push(this);
}
VC::VCData* VC::VCData::New(){
  VCData* vcd;
  if(_free.empty()){
    vcd = new VCData();
    _all.push(vcd);
  } else {
   vcd = _free.top();
   _free.pop();
  }
  assert(!vcd->_use);
  vcd->_use= true;
  return vcd;
}



int VC::total_cycles = 0;
const char * const VC::VCSTATE[] = {"idle",
				    "routing",
				    "vc_alloc",
				    "active"};

VC::state_info_t VC::state_info[] = {{0},
				     {0},
				     {0},
				     {0}};
int VC::occupancy = 0;

VC::VC( const Configuration& config, int outputs, 
	Module *parent, const string& name, bool special )
  : Module( parent, name ), 
    _cur_size(0),_buffer(NULL),_state(idle), _state_time(0), _out_port(-1), _out_vc(-1), _total_cycles(0),
    _vc_alloc_cycles(0), _active_cycles(0), _idle_cycles(0), _routing_cycles(0),
    _pri(0), _watched(false), _expected_pid(-1), _last_id(-1), _last_pid(-1),_drop(false),_special_vc(special)
{
  if(_special_vc){
    _max_size = config.GetInt( "vc_buf_size" ) + config.GetInt( "shared_buf_size" );
  } else {
    _max_size = config.GetInt( "vc_buf_size" ) + config.GetInt( "shared_buf_size" );
  }


  string priority = config.GetStr( "priority" );
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

  _priority_donation = config.GetInt("vc_priority_donation");
}

VC::~VC()
{
}

bool VC::SubstituteFrontFlit( Flit *f ){
  if(_buffer==NULL){
    _buffer = VCData::New();
  }
  if(f->head){
    _buffer->_route_set.push_front(OutputSet::New());
    _buffer->_time_stamp.push_front(GetSimTime());
  }
  _cur_size++;
  _buffer->_flit.push_front(f);
  UpdatePriority();
  return true;
}

bool VC::AddFlit( Flit *f , OutputSet* o)
{
  assert(f);

  if(_expected_pid >= 0) {
    if(f->pid != _expected_pid) {
      ostringstream err;
      err << "Received flit " << f->id << " with unexpected packet ID: " << f->pid 
	  << " (expected: " << _expected_pid << ")";
      Error(err.str());
      return false;
    } else if(f->tail) {
      _expected_pid = -1;
    }
  } else if(!f->tail) {
    _expected_pid = f->pid;
  }
    
  if(_buffer==NULL){
    _buffer = VCData::New();
  }
  if((int)_cur_size >= _max_size) {
    Error("Flit buffer overflow.");
    return false;
  }

  // update flit priority before adding to VC buffer
  if(_pri_type == local_age_based) {
    f->pri = numeric_limits<int>::max() - GetSimTime();
    assert(f->pri >= 0);
  } else if(_pri_type == hop_count_based) {
    f->pri = f->hops;
    assert(f->pri >= 0);
  }

  if(f->head){
    assert(o);
    _buffer->_time_stamp.push_back(GetSimTime());
    _buffer->_route_set.push_back(o);
  } 
  _cur_size++;
  if(!f->head && !f->tail && ! _buffer->_flit.empty()){
    assert(f->packet_size==0);
    Flit* last = _buffer->_flit.back();
    if(last && !last->head){//compress body packets in a flit
      last->packet_size++;
      //Free is ok here because it is single threaded
      //f still holds the correct value and is no gone until next Flit::new() call
      f->Free();
    } else {
      _buffer->_flit.push_back(f);
    }
  } else {
    _buffer->_flit.push_back(f);
  }
  UpdatePriority();
  return true;
}

Flit *VC::RemoveFlit( )
{
  Flit *f = NULL;
  if ( !_buffer->_flit.empty( ) ) {
    _cur_size--;
    f = _buffer->_flit.front( );
    _buffer->_flit.pop_front( );
    if(f->head){
      _buffer->_time_stamp.pop_front();
      _buffer->_route_set.front()->Free();
      _buffer->_route_set.pop_front();
    } else if(!f->head && !f->tail && f->packet_size!=0){
      assert(_cur_size);
      _buffer->_flit.push_front(Flit::Replicate(f));
    }
    _last_id = f->id;
    _last_pid = f->pid;
    UpdatePriority();
    if(_buffer->_flit.empty( )){
      _buffer->Free();
      _buffer=NULL;
    }
  } else {
    Error("Trying to remove flit from empty buffer->");
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
  _state_time = 0;
}


const OutputSet *VC::GetRouteSet( ) const
{
  if(_buffer==NULL){
    return NULL;
  } else {
    return _buffer->_route_set.front();
  }
}

void VC::SetOutput( int port, int vc )
{
  _out_port = port;
  _out_vc   = vc;
}

void VC::UpdatePriority()
{
  _drop=false;
  if(_buffer==NULL || _buffer->_flit.empty()) return;
  if(_pri_type == queue_length_based) {
    _pri = _cur_size;
  } else if(_pri_type != none) {
    Flit * f = _buffer->_flit.front();
    if((_pri_type != local_age_based) && _priority_donation) {
      Flit * df = f;
      for(int i = 1; i < _cur_size; ++i) {
	Flit * bf = _buffer->_flit[i];
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


void VC::Route( tRoutingFunction rf, const Router* router, const Flit* f, int in_channel )
{
  assert(_buffer);
  rf( router, f, in_channel, _buffer->_route_set.front(), false );
  _out_port = -1;
  _out_vc = -1;
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
  case vc_alloc      : _vc_alloc_cycles++; break;
  case routing       : _routing_cycles++; break;
  }
  state_info[_state].cycles++;
  occupancy += _cur_size;
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
    os << " fill: " << _cur_size;
    if(!_buffer->_flit.empty()) {
      os << " front: " << _buffer->_flit.front()->id;
    }
    os << " pri: " << _pri;
    os << endl;
  }
}

void VC::DisplayStats( bool print_csv, ostream & os )
{
  if(print_csv) {
    for(eVCState state = state_min; state <= state_max; state = eVCState(state+1)) {
      os << (double)state_info[state].cycles/(double)total_cycles << ",";
    }
    os << (double)occupancy/(double)total_cycles << endl;
  }
  os << "VC state breakdown:" << endl;
  for(eVCState state = state_min; state <= state_max; state = eVCState(state+1)) {
    os << "  " << VCSTATE[state]
       << ": " << (double)state_info[state].cycles/(double)total_cycles << endl;
  }
  os << "  occupancy: " << (double)occupancy/(double)total_cycles << endl;
}
