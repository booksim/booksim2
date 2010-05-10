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

/*network.cpp
 *
 *This class is the basis of the entire network, it contains, all the routers
 *channels in the network, and is extended by all the network topologies
 *
 */

#include "booksim.hpp"
#include <assert.h>
#include "network.hpp"



Network::Network( const Configuration &config, const string & name ) :
  Module( 0, name )
{
  _size     = -1; 
  _sources  = -1; 
  _dests    = -1;
  _channels = -1;
}

Network::~Network( )
{
  for ( int r = 0; r < _size; ++r ) {
    if ( _routers[r] ) delete _routers[r];
  }
  for ( int s = 0; s < _sources; ++s ) {
    if ( _inject[s] ) delete _inject[s];
    if ( _inject_cred[s] ) delete _inject_cred[s];
  }
  for ( int d = 0; d < _dests; ++d ) {
    if ( _eject[d] ) delete _eject[d];
    if ( _eject_cred[d] ) delete _eject_cred[d];
  }
  for ( int c = 0; c < _channels; ++c ) {
    if ( _chan[c] ) delete _chan[c];
    if ( _chan_cred[c] ) delete _chan_cred[c];
  }
}

void Network::_Alloc( )
{
  assert( ( _size != -1 ) && 
	  ( _sources != -1 ) && 
	  ( _dests != -1 ) && 
	  ( _channels != -1 ) );

  _routers.resize(_size);
  gNodes = _sources;

  /*booksim used arrays of flits as the channels which makes have capacity of
   *one. To simulate channel latency, flitchannel class has been added
   *which are fifos with depth = channel latency and each cycle the channel
   *shifts by one
   *credit channels are the necessary counter part
   */
  _inject.resize(_sources);
  _inject_cred.resize(_sources);
  for ( int s = 0; s < _sources; ++s ) {
    _inject[s] = new FlitChannel;
    _inject_cred[s] = new CreditChannel;
  }
  _eject.resize(_dests);
  _eject_cred.resize(_dests);
  for ( int d = 0; d < _dests; ++d ) {
    _eject[d] = new FlitChannel;
    _eject_cred[d] = new CreditChannel;
  }
  _chan.resize(_channels);
  _chan_cred.resize(_channels);
  for ( int c = 0; c < _channels; ++c ) {
    _chan[c] = new FlitChannel;
    _chan_cred[c] = new CreditChannel;
  }

  _chan_use.resize(_channels, 0);

  _chan_use_cycles = 0;
}

int Network::NumSources( ) const
{
  return _sources;
}

int Network::NumDests( ) const
{
  return _dests;
}

void Network::ReadInputs( )
{
  for ( int r = 0; r < _size; ++r ) {
    _routers[r]->ReadInputs( );
  }
}

void Network::InternalStep( )
{
  for ( int r = 0; r < _size; ++r ) {
    _routers[r]->InternalStep( );
  }
}

void Network::WriteOutputs( )
{
  for ( int r = 0; r < _size; ++r ) {
    _routers[r]->WriteOutputs( );
  }

  for ( int c = 0; c < _channels; ++c ) {
    if ( _chan[c]->InUse() ) {
      _chan_use[c]++;
    }
  }
  _chan_use_cycles++;
}

void Network::WriteFlit( Flit *f, int source )
{
  assert( ( source >= 0 ) && ( source < _sources ) );
  _inject[source]->Send(f);
}

Flit *Network::ReadFlit( int dest )
{
  assert( ( dest >= 0 ) && ( dest < _dests ) );
  return _eject[dest]->Receive();
}

/* new functions added for NOC
 */
Flit* Network::PeekFlit( int dest ) 
{
  assert( ( dest >= 0 ) && ( dest < _dests ) );
  return _eject[dest]->Peek( );
}

void Network::WriteCredit( Credit *c, int dest )
{
  assert( ( dest >= 0 ) && ( dest < _dests ) );
  _eject_cred[dest]->Send(c);
}

Credit *Network::ReadCredit( int source )
{
  assert( ( source >= 0 ) && ( source < _sources ) );
  return _inject_cred[source]->Receive();
}

/* new functions added for NOC
 */
Credit *Network::PeekCredit( int source ) 
{
  assert( ( source >= 0 ) && ( source < _sources ) );
  return _inject_cred[source]->Peek( );
}

void Network::InsertRandomFaults( const Configuration &config )
{
  Error( "InsertRandomFaults not implemented for this topology!" );
}

void Network::OutChannelFault( int r, int c, bool fault )
{
  assert( ( r >= 0 ) && ( r < _size ) );
  _routers[r]->OutChannelFault( c, fault );
}

double Network::Capacity( ) const
{
  return 1.0;
}

/* this function can be heavily modified to display any information
 * neceesary of the network, by default, call display on each router
 * and display the channel utilization rate
 */
void Network::Display( ) const
{
  for ( int r = 0; r < _size; ++r ) {
    _routers[r]->Display( );
  }
  double average = 0;
  for ( int c = 0; c < _channels; ++c ) {
    cout << "channel " << c << " used " 
	 << 100.0 * (double)_chan_use[c] / (double)_chan_use_cycles 
	 << "% of the time" << endl;
    average += 100.0 * (double)_chan_use[c] / (double)_chan_use_cycles ;
  }
  average = average/_channels;
  cout<<"Average channel: "<<average<<endl;
}
