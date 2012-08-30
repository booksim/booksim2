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

/*network.cpp
 *
 *This class is the basis of the entire network, it contains, all the routers
 *channels in the network, and is extended by all the network topologies
 *
 */

#include "booksim.hpp"
#include <assert.h>
#include "network.hpp"



Network::Network( const Configuration &config ) :
  Module( 0, "network" )
{
  _size     = -1; 
  _sources  = -1; 
  _dests    = -1;
  _channels = -1;

  _routers = 0;
  _inject = 0;
  _eject = 0;
  _chan = 0;
  _chan_use = 0;
  _inject_cred = 0;
  _eject_cred = 0;
  _chan_cred = 0;

}

Network::~Network( )
{
  if ( _routers ) {
    for ( int r = 0; r < _size; ++r )
      if ( _routers[r] ) delete _routers[r];
    delete [] _routers;
  }
  
  if ( _inject ) delete [] _inject;
  if ( _eject )  delete [] _eject;
  if ( _chan )   delete [] _chan;

  if ( _chan_use ) delete [] _chan_use;

  if ( _inject_cred ) delete [] _inject_cred;
  if ( _eject_cred )  delete [] _eject_cred;
  if ( _chan_cred )   delete [] _chan_cred;
}

void Network::_Alloc( )
{
  assert( ( _size != -1 ) && 
	  ( _sources != -1 ) && 
	  ( _dests != -1 ) && 
	  ( _channels != -1 ) );

  _routers = new Router * [_size];
  gNodes = _sources;

  /*booksim used arrays of flits as the channels which makes have capacity of
   *one. To simulate channel latency, flitchannel class has been added
   *which are fifos with depth = channel latency and each cycle the channel
   *shifts by one
   */
  _inject = new FlitChannel[_sources];
  _eject  = new FlitChannel[_dests];
  _chan   = new FlitChannel[_channels];

  _chan_use = new int [_channels];

  for ( int i = 0; i < _channels; ++i ) {
    _chan_use[i] = 0;
  }

  _chan_use_cycles = 0;

  /*same as flit channel, credit channels are the necessary counter part
   */
  _inject_cred = new CreditChannel[_sources];
  _eject_cred  = new CreditChannel[_dests];
  _chan_cred   = new CreditChannel[_channels];
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

void Network::ReadInputs(int tid){
  for ( int r = 0; r < _size; ++r ) {
    if(_routers[r]->GetTID() == tid)
      _routers[r]->ReadInputs( );
  }
}

void Network::DOALL(int tid){
  for ( int r = 0; r < _size; ++r ) {
    if(_routers[r]->GetTID() == tid){
      _routers[r]->ReadInputs( );
      _routers[r]->InternalStep( );
      _routers[r]->WriteOutputs( );
    }
  }
}


void Network::InternalStep( )
{
  for ( int r = 0; r < _size; ++r ) {
    _routers[r]->InternalStep( );
  }
}

void Network::InternalStep( int tid)
{
  for ( int r = 0; r < _size; ++r ) {
    if(_routers[r]->GetTID() == tid)
      _routers[r]->InternalStep( );
  }
}


void Network::WriteOutputs( )
{
  for ( int r = 0; r < _size; ++r ) {
    _routers[r]->WriteOutputs( );
  }

  for ( int c = 0; c < _channels; ++c ) {
    if ( _chan[c].InUse() ) {
      _chan_use[c]++;
    }
  }
  _chan_use_cycles++;
}

void Network::WriteOutputs(int tid )
{
  for ( int r = 0; r < _size; ++r ) {
    if(_routers[r]->GetTID() == tid)
      _routers[r]->WriteOutputs( );
  }

//   for ( int c = 0; c < _channels; ++c ) {
//     if ( _chan[c].InUse() ) {
//       _chan_use[c]++;
//     }
//   }
//   _chan_use_cycles++;
}

void Network::WriteFlit( Flit *f, int source )
{
  assert( ( source >= 0 ) && ( source < _sources ) );
  _inject[source].SendFlit(f);
}

Flit *Network::ReadFlit( int dest )
{
  assert( ( dest >= 0 ) && ( dest < _dests ) );
  return _eject[dest].ReceiveFlit();
}

/* new functions added for NOC
 */
Flit* Network::PeekFlit( int dest ) 
{
  assert( ( dest >= 0 ) && ( dest < _dests ) );
  return _eject[dest].PeekFlit( );
}

void Network::WriteCredit( Credit *c, int dest )
{
  assert( ( dest >= 0 ) && ( dest < _dests ) );
  _eject_cred[dest].SendCredit(c);
}

Credit *Network::ReadCredit( int source )
{
  assert( ( source >= 0 ) && ( source < _sources ) );
  return _inject_cred[source].ReceiveCredit();
}

/* new functions added for NOC
 */
Credit *Network::PeekCredit( int source ) 
{
  assert( ( source >= 0 ) && ( source < _sources ) );
  return _inject_cred[source].PeekCredit( );
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

//multithreading
void Network::Divide(int t){
  int shared_chan = 0;
  //basic division based on thread id
  /*if(_routers == 0){
    cout<<"you must configure the network before dividing"<<endl;
    exit(-1);
  }
  for(int i = 0; i<_size; i++){
    _routers[i]->SetTID(i%t);
  }
  
  for(int i = 0; i<_channels; i++){
    _chan[i].SetShared();
    _chan_cred[i].SetShared();
    shared_chan ++;
    }*/
    
  if(_routers == 0){
    cout<<"you must configure the network before dividing"<<endl;
    exit(-1);
  }
  for(int tt = 0; tt<_threads; tt++){
    int startr = (int)(_size/_threads)*tt;
    int endr = startr+(int)(_size/_threads);
    if (tt+1 == _threads){
      endr = _size;
    }
    for(int i = startr; i<endr; i++){
      _routers[i]->SetTID(tt);
    }
  }
 
  for(int i = 0; i<_channels; i++){
    if(_routers[_chan[i].GetSink()]->GetTID()!= _routers[_chan[i].GetSource()]->GetTID()){
      _chan[i].SetShared();
      _chan_cred[i].SetShared();
      shared_chan ++;
    }
    }

  cout<<"Number of shared channels: "<<shared_chan<<endl;
}

void Network::GetNodes(int *** nodelist, int** nodecount){

  (*nodecount) = (int*)calloc(_threads,sizeof(int));
  (*nodelist) = (int**)malloc(_threads*sizeof(int*));
  for(int i = 0; i<_sources; i++){
    (*nodecount)[_routers[_inject[i].GetSink()]->GetTID()]++;
    
  }
  for(int i = 0; i<_threads; i++){
    (*nodelist)[i] = (int*)malloc((*nodecount)[i]*sizeof(int));
    int j = 0;
    for(int k = 0; k<_sources; k++){
      if(_routers[_inject[k].GetSink()]->GetTID()==i){
	(*nodelist)[i][j] = k;
	j++;
      }
    }
  }
  
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
