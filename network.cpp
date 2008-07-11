/*network.cpp
 *
 *This class is the basis of the entire network, it contains, all the routers
 *channels in the network, and is extended by all the network topologies
 *
 */

#include "booksim.hpp"
#include <assert.h>

#include "network.hpp"

// --------------------
//  Globals
// ------------------
int gK = 0;//radix
int gN = 0;//dimension
int gC = 0;//concentration

/*These extra variables are necessary for correct traffic pattern generation
 *The difference is due to concentration, radix 4 with concentration of 4 is
 *equivalent to radix 8 with no concentration. Though this only really applies
 *Under NOC since NOCS are inheriently 2 dimension
 */
int realgk;
int realgn;

int gNodes = 0;

/*These variables are used by NOCS to specify the node concentration per 
 *router. Technically the realdgk realgn can be calculated from these 
 *global variables, thus they maybe removed later
 */
int xrouter = 0;
int yrouter = 0;
int xcount  = 0;
int ycount  = 0;

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
    if ( _chan[c].InUse() ) {
      _chan_use[c]++;
    }
  }
  _chan_use_cycles++;
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
