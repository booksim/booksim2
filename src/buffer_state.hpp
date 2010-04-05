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

#ifndef _BUFFER_STATE_HPP_
#define _BUFFER_STATE_HPP_

#include "module.hpp"
#include "flit.hpp"
#include "credit.hpp"
#include "config_utils.hpp"

class BufferState : public Module {

  int  _wait_for_tail_credit;
  int  _vc_busy_when_full;
  int  _buf_size;
  int  _vcs;

  bool *_in_use;
  bool *_tail_sent;
  int  *_cur_occupied;

  int  _vc_range_begin[Flit::NUM_FLIT_TYPES];
  int  _vc_range_size[Flit::NUM_FLIT_TYPES];
  int  _vc_sel_last[Flit::NUM_FLIT_TYPES];

public:
  BufferState( ) { };
  void _Init( const Configuration& config );

  BufferState( const Configuration& config );
  BufferState( const Configuration& config, 
	       Module *parent, const string& name );
  ~BufferState( );

  void ProcessCredit( Credit *c );
  void SendingFlit( Flit *f );

  void TakeBuffer( int vc = 0 );

  bool IsFullFor( int vc = 0 ) const;
  bool IsAvailableFor( int vc = 0 ) const;

  int FindAvailable( Flit::FlitType type = Flit::ANY_TYPE );
  int Size (int vc = 0) const;
  void Display( ) const;
};

#endif 
