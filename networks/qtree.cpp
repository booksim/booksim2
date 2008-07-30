////////////////////////////////////////////////////////////////////////
//
// QTree: A Quad-Tree Indirect Network.
//
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:14:07 $
//  $Id: qtree.cpp,v 1.1 2007/05/17 17:14:07 jbalfour Exp $
// 
////////////////////////////////////////////////////////////////////////

#include "booksim.hpp"
#include <vector>
#include <sstream>

#include "qtree.hpp"
#include "misc_utils.hpp"

QTree::QTree( const Configuration& config )
  : Network ( config )
{
  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}


void QTree::_ComputeSize( const Configuration& config )
{

  _k = config.GetInt( "k" );
  _n = config.GetInt( "n" );

  assert( _k == 4 && _n == 3 );

  gK = _k; gN = _n;

  _sources = powi( _k, _n );
  _dests   = powi( _k, _n );

  _size = 0;
  for (int i = 0; i < _n; i++)
    _size += powi( _k, i );

  _channels = 0;
  for (int j = 1; j < _n; j++)
    _channels += 2 * powi( _k, j );

}

void QTree::RegisterRoutingFunctions(){

}

void QTree::_BuildNet( const Configuration& config )
{

  ostringstream routerName;
  int h, r, pos, port;

  for (h = 0; h < _n; h++) {
    for (pos = 0 ; pos < powi( _k, h ) ; ++pos ) {
      
      int id = h * 256 + pos;  
      r = _RouterIndex( h, pos );

      routerName << "router_" << h << "_" << pos;

      int d = ( h == 0 ) ? _k : _k + 1;
      _routers[r] = Router::NewRouter( config, this,
				       routerName.str( ),
				       id, d, d);
      routerName.seekp( 0, ios::beg );
    }
  }
  
  // Injection & Ejection Channels
  for ( pos = 0 ; pos < powi( _k, _n-1 ) ; ++pos ) {
    r = _RouterIndex( _n-1, pos );
    for ( port = 0 ; port < _k ; port++ ) {

      _routers[r]->AddInputChannel( &_inject[_k*pos+port],
				    &_inject_cred[_k*pos+port]);

      _routers[r]->AddOutputChannel( &_eject[_k*pos+port],
				     &_eject_cred[_k*pos+port]);
    }
  }

  int c;
  for ( h = 0 ; h < _n ; ++h ) {
    for ( pos = 0 ; pos < powi( _k, h ) ; ++pos ) {
      for ( port = 0 ; port < _k ; port++ ) {

	r = _RouterIndex( h, pos );

	if ( h < _n-1 ) {
	  // Channels to Children Nodes
	  c = _InputIndex( h , pos, port );
	  _routers[r]->AddInputChannel( &_chan[c], 
					&_chan_cred[c] );

	  c = _OutputIndex( h, pos, port );
	  _routers[r]->AddOutputChannel( &_chan[c], 
					 &_chan_cred[c] );

	}
      }
      if ( h > 0 ) {
	// Channels to Parent Nodes
	c = _OutputIndex( h - 1, pos / _k, pos % _k );
	_routers[r]->AddInputChannel( &_chan[c],
				      &_chan_cred[c] );

	c = _InputIndex( h - 1, pos / _k, pos % _k );
	_routers[r]->AddOutputChannel( &_chan[c],
				       &_chan_cred[c]);
      }
    }
  }
}
 
int QTree::_RouterIndex( int height, int pos ) 
{
  int r = 0;
  for ( int h = 0; h < height; h++ ) 
    r += powi( _k, h );
  return (r + pos);
}

int QTree::_InputIndex( int height, int pos, int port )
{
  assert( height >= 0 && height < powi( _k,_n-1 ) );
  int c = 0;
  for ( int h = 0; h < height; h++) 
    c += powi( _k, h+1 );
  return ( c + _k * pos + port );
}

int QTree::_OutputIndex( int height, int pos, int port )
{
  assert( height >= 0 && height < powi( _k,_n-1 ) );
  int c = _channels / 2;
  for ( int h = 0; h < height; h++) 
    c += powi( _k, h+1 );
  return ( c + _k * pos + port );
}


int QTree::HeightFromID( int id ) 
{
  return id / 256;
}

int QTree::PosFromID( int id )
{
  return id % 256;
}
