// $Id: outputset.cpp 938 2008-12-12 03:06:32Z dub $

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

/*outputset.cpp
 *
 *output set assigns a flit which output to go to in a router
 *used by the VC class
 *the output assignment is done by the routing algorithms..
 *
 */

#include "booksim.hpp"
#include <assert.h>

#include "outputset.hpp"

OutputSet::OutputSet( int num_outputs )
{
  _num_outputs = num_outputs;

  _outputs = new list<sSetElement> [_num_outputs];
}

OutputSet::~OutputSet( )
{
  delete [] _outputs;
}

void OutputSet::Clear( )
{
  for ( int i = 0; i < _num_outputs; ++i ) {
    _outputs[i].clear( );
  }
}

void OutputSet::Add( int output_port, int vc, int pri  )
{
  AddRange( output_port, vc, vc, pri );
}

void OutputSet::AddRange( int output_port, int vc_start, int vc_end, int pri )
{
  assert( ( output_port >= 0 ) && 
	  ( output_port < _num_outputs ) &&
	  ( vc_start <= vc_end ) );

  sSetElement s;

  s.vc_start = vc_start;
  s.vc_end   = vc_end;
  s.pri      = pri;

  _outputs[output_port].push_back( s );
}

int OutputSet::Size( ) const
{
  return _num_outputs;
}

bool OutputSet::OutputEmpty( int output_port ) const
{
  assert( ( output_port >= 0 ) && 
	  ( output_port < _num_outputs ) );
  
  return _outputs[output_port].empty( );
}

int OutputSet::NumVCs( int output_port ) const
{
  assert( ( output_port >= 0 ) && 
	  ( output_port < _num_outputs ) );

  int total = 0;

  for ( list<sSetElement>::const_iterator i = _outputs[output_port].begin( );
	i != _outputs[output_port].end( ); i++ ) {
    total += i->vc_end - i->vc_start + 1;
  }

  return total;
}

int OutputSet::GetVC( int output_port, int vc_index, int *pri ) const
{
  assert( ( output_port >= 0 ) && 
	  ( output_port < _num_outputs ) );

  int range;
  int remaining = vc_index;
  int vc = -1;
  
  if ( pri ) { *pri = -1; }

  for ( list<sSetElement>::const_iterator i = _outputs[output_port].begin( );
	i != _outputs[output_port].end( ); i++ ) {

    range = i->vc_end - i->vc_start + 1;
    if ( remaining >= range ) {
      remaining -= range;
    } else {
      vc = i->vc_start + remaining;
      if ( pri ) {
	*pri = i->pri;
      }
      break;
    }
  }

  return vc;
}

bool OutputSet::GetPortVC( int *out_port, int *out_vc ) const
{
  bool single_output = false;
  int  used_outputs  = 0;

  for ( int output = 0; output < _num_outputs; ++output ) {

    list<sSetElement>::const_iterator i = _outputs[output].begin( );
    
    if ( i != _outputs[output].end( ) ) {
      ++used_outputs;

      if ( i->vc_start == i->vc_end ) {
	*out_vc   = i->vc_start;
	*out_port = output;
	single_output = true;
      } else {
	// multiple vc's selected
	break;
      }
    }

    if ( used_outputs > 1 ) {
      // multiple outputs selected
      single_output = false;
      break;
    }
  }

  return single_output;
}
