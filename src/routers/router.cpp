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
#include <assert.h>
#include "router.hpp"

//////////////////Sub router types//////////////////////
#include "iq_router_baseline.hpp"
#include "iq_router_combined.hpp"
#include "iq_router_split.hpp"
#include "event_router.hpp"
#include "chaos_router.hpp"
#include "MECSRouter.hpp"
///////////////////////////////////////////////////////

Router::Router( const Configuration& config,
		Module *parent, const string & name, int id,
		int inputs, int outputs ) :
  Module( parent, name ),
  _id( id ),
  _inputs( inputs ),
  _outputs( outputs )
{
  _st_prepare_delay = config.GetInt( "st_prepare_delay" );
  _st_final_delay   = config.GetInt( "st_final_delay" );
  _credit_delay     = config.GetInt( "credit_delay" );
  _input_speedup    = config.GetInt( "input_speedup" );
  _output_speedup   = config.GetInt( "output_speedup" );

  _input_channels = new vector<FlitChannel *>;
  _input_credits  = new vector<CreditChannel *>;

  _output_channels = new vector<FlitChannel *>;
  _output_credits  = new vector<CreditChannel *>;

  _channel_faults  = new vector<bool>;

 

}

Router::~Router( )
{
  delete _input_channels;
  delete _input_credits;
  delete _output_channels;
  delete _output_credits;
  delete _channel_faults;
}

Credit *Router::_NewCredit( int vcs )
{
  Credit *c;

  c = new Credit( vcs );
  return c;
}

void Router::_RetireCredit( Credit *c )
{
  delete c;
}

void Router::AddInputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  _input_channels->push_back( channel );
  _input_credits->push_back( backchannel );
  channel->SetSink( this ) ;
}

void Router::AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  _output_channels->push_back( channel );
  _output_credits->push_back( backchannel );
  _channel_faults->push_back( false );
  channel->SetSource( this ) ;
}

int Router::GetID( ) const
{
  return _id;
}


void Router::OutChannelFault( int c, bool fault )
{
  assert( ( c >= 0 ) && ( (unsigned int)c < _channel_faults->size( ) ) );

  (*_channel_faults)[c] = fault;
}

bool Router::IsFaultyOutput( int c ) const
{
  assert( ( c >= 0 ) && ( (unsigned int)c < _channel_faults->size( ) ) );

  return (*_channel_faults)[c];
}

/*Router constructor*/
Router *Router::NewRouter( const Configuration& config,
			   Module *parent, string name, int id,
			   int inputs, int outputs )
{
  Router *r;
  string type;
  string topo;

  config.GetStr( "router", type );
  config.GetStr( "topology", topo);

  if ( type == "iq" ) {
    if(topo == "MECS"){
      r = new MECSRouter( config, parent, name, id, inputs, outputs);
    } else {
      r = new IQRouterBaseline( config, parent, name, id, inputs, outputs );
    }
  } else if ( type == "iq_combined" ) {
    r = new IQRouterCombined( config, parent, name, id, inputs, outputs );
  } else if ( type == "iq_split" ) {
    r = new IQRouterSplit( config, parent, name, id, inputs, outputs );
  } else if ( type == "event" ) {
    r = new EventRouter( config, parent, name, id, inputs, outputs );
  } else if ( type == "chaos" ) {
    r = new ChaosRouter( config, parent, name, id, inputs, outputs );
  } else {
    cout << "Unknown router type " << type << endl;
  }
  /*For additional router, add another else if statement*/
  /*Original booksim specifies the router using "flow_control"
   *we now simply call these types. 
   */

  return r;
}





