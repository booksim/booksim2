// $Id: router.cpp 4296 2012-02-15 18:37:41Z qtedq $

/*
 Copyright (c) 2007-2011, Trustees of The Leland Stanford Junior University
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

/*router.cpp
 *
 *The base class of either iq router or event router
 *contains a list of channels and other router configuration variables
 *
 *The older version of the simulator uses an array of flits and credit to 
 *simulate the channels. Newer version ueses flitchannel and credit channel
 *which can better model channel delay
 *
 *The older version of the simulator also uses vc_router and chaos router
 *which are replaced by iq rotuer and event router in the present form
 */

#include "booksim.hpp"
#include <iostream>
#include <cassert>
#include "router.hpp"

//////////////////Sub router types//////////////////////
#include "iq_router.hpp"
///////////////////////////////////////////////////////

Router::Router( const Configuration& config,
		Module *parent, const string & name, int id,
		int inputs, int outputs) :
TimedModule( parent, name ), _id( id ), _inputs( inputs ), _outputs( outputs ),
   _partial_internal_cycles(0.0), _received_flits(inputs, 0), 
   _sent_flits(inputs, 0), _already_connected_output(false), _already_connected_input(false)
{
  _crossbar_delay   = ( config.GetInt( "st_prepare_delay" ) + 
			config.GetInt( "st_final_delay" ) );
  _credit_delay     = config.GetInt( "credit_delay" );
  _input_speedup    = config.GetInt( "input_speedup" );
  _output_speedup   = config.GetInt( "output_speedup" );
  _internal_speedup = config.GetFloat( "internal_speedup" );
  _is_transition_router = false;
  _destination_this_serves = -1;
}

void Router::AddInputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  assert(channel != 0 && backchannel != 0);
  _input_channels.push_back( channel );
  _input_credits.push_back( backchannel );
  channel->SetSink( this, _input_channels.size() - 1 ) ;
}

void Router::AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  assert(channel != 0 && backchannel != 0);
  _output_channels.push_back( channel );
  _output_credits.push_back( backchannel );
  _channel_faults.push_back( false );
  channel->SetSource( this, _output_channels.size() - 1 ) ;
}

void Router::AddTransitionInputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  assert(_already_connected_input == false);
  assert(channel != 0 && backchannel != 0);
  assert((int)_input_channels.size() == _inputs - 1);
  assert(_is_transition_router == true && _inputs == _max_inputs);
  _input_channels.push_back( channel );
  _input_credits.push_back( backchannel );
  channel->SetSink( this, _input_channels.size() - 1 ) ;
  _channel_faults.push_back( false );
  _already_connected_input = true;
  assert((int)_input_channels.size() == _max_inputs);
}

void Router::AddTransitionOutputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  assert(_already_connected_output == false);
  assert(channel != 0 && backchannel != 0);
  assert((int)_output_channels.size() == _outputs - 1);
  assert(_is_transition_router == true && _outputs == _max_outputs);
  _output_channels.push_back( channel );
  _output_credits.push_back( backchannel );
  _channel_faults.push_back( false );
  channel->SetSource( this, _output_channels.size() - 1 ) ;
  _channel_faults.push_back( false );
  _already_connected_output = true;
  assert((int)_output_channels.size() == _max_outputs);
}

void Router::DestinationThisServes(int dest)
{
  _destination_this_serves = dest;
}

int Router::GetDestinationThisServes() const
{
  assert(_destination_this_serves != -1);
  return _destination_this_serves;
}

void Router::Evaluate( )
{
  _partial_internal_cycles += _internal_speedup;
  while( _partial_internal_cycles >= 1.0 ) {
    _InternalStep( );
    _partial_internal_cycles -= 1.0;
  }
}

void Router::OutChannelFault( int c, bool fault )
{
  assert( ( c >= 0 ) && ( (size_t)c < _channel_faults.size( ) ) );

  _channel_faults[c] = fault;
}

bool Router::IsFaultyOutput( int c ) const
{
  assert( ( c >= 0 ) && ( (size_t)c < _channel_faults.size( ) ) );

  return _channel_faults[c];
}

/*Router constructor*/
Router *Router::NewRouter( const Configuration& config,
			   Module *parent, const string & name, int id,
			   int inputs, int outputs )
{
  const string type = config.GetStr( "router" );
  Router *r = NULL;
  if ( type == "iq" ) {
    r = new IQRouter( config, parent, name, id, inputs, outputs);
  } else {
    cerr << "Unknown router type: " << type << endl;
  }
  /*For additional router, add another else if statement*/
  /*Original booksim specifies the router using "flow_control"
   *we now simply call these types. 
   */

  return r;
}

void Router::SetTransitionRouter()
{
  assert(_is_transition_router == false);
  _is_transition_router = true;
  _inputs++;
  _outputs++;
  assert(_max_inputs >= _inputs && _max_outputs >= _outputs);
}





