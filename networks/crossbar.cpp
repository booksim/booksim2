// ----------------------------------------------------------------------
//
//  CrossBar: Network comprising a single crossbar
//
// ----------------------------------------------------------------------
#include "booksim.hpp"
#include <vector>
#include <sstream>
#include <assert.h>

#include "random_utils.hpp"
#include "misc_utils.hpp"

#include "crossbar.hpp"
#include "power.hpp"
#include "iq_router.hpp"
#include "channelfile.hpp"

// ----------------------------------------------------------------------
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/06/26 22:49:23 $
//  $Id: crossbar.cpp,v 1.2 2007/06/26 22:49:23 jbalfour Exp $
// ----------------------------------------------------------------------

CrossBar::CrossBar( const Configuration& config ) : Network( config ) {

  _ComputeSize( config ) ;
  _Alloc() ;
  _BuildNet( config ) ;

}

void CrossBar::_ComputeSize( const Configuration& config ) {

  int k = config.GetInt( "k" );
  int n = config.GetInt( "n" );
  int c = config.GetInt( "c" );

  bool c_is_power_of_two = false ;
  for ( int i = 0 ; i < 8 ; ++i ) {
    if ( c == 1 << i ) 
      c_is_power_of_two = true ;
  } 
  assert( c_is_power_of_two ) ;

  gK = k ;  
  gN = n ;
  gC = c ;

  realgk = k;
  realgn = n;

  _sources  = k ;         // number of crossbar injection ports
  _dests    = _sources ;  // number of crossbar ejection ports
  _size     = 1 ;         // number of routers (single crossbar)
  _channels = 0 ;         //only injection ejection ports
  _nodes    = k * c ;     // number of processors with concentration

} 

void CrossBar::RegisterRoutingFunctions() {

}
void CrossBar::_BuildNet( const Configuration& config ) {
  
  // Parse channel listing file
  ChannelFile channelFile ;
  string fileName ;
  config.GetStr("channel_file", fileName ) ;
  bool useChannelFile = channelFile.Open( fileName ) ;
  if ( useChannelFile ) {
    channelFile.Dump() ;
  }

  // Create the central router acting as the crossbar
  ostringstream name ;
  name << "router_0" ;
  _routers[0] = Router::NewRouter(  config, this, name.str(), 0,
				    _sources, _dests ) ;
  name.seekp( 0, ios::beg ) ;

  // Connect injection and ejection ports to the router acting as
  //  the central crossbar instance
  for ( int port = 0 ; port < _sources ; ++port ) {

    _routers[0]->AddInputChannel( &_inject[port], &_inject_cred[port] ) ;
    _routers[0]->AddOutputChannel( &_eject[port], &_eject_cred[port] ) ;

    // The latency of the injection and ejection links needs to be
    //  calculated in a more sensible way, possible from input file
    if ( useChannelFile ) {
      _inject[port].SetLatency( channelFile.InjectLatency(port) ) ;
      _eject[port].SetLatency( channelFile.EjectLatency(port) ) ;
    } else {
      _inject[port].SetLatency( 1 ) ;
      _eject[port].SetLatency( 1 ) ;
    }

    
  }
}
