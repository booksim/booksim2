/*main.cpp
 *
 *The starting point of the network simulator
 *-Include all network header files
 *-initilize the network
 *-initialize the traffic manager and set it to run
 *
 *
 */

#include <string>
#include <stdlib.h>
#include <fstream>

#include "booksim.hpp"
#include "routefunc.hpp"
#include "traffic.hpp"
#include "booksim_config.hpp"
#include "trafficmanager.hpp"
#include "random_utils.hpp"
#include "network.hpp"
#include "injection.hpp"

///////////////////////////////////////////////////////////////////////////////
//include new network here//

#include "singlenet.hpp"
#include "kncube.hpp"
#include "fly.hpp"
#include "isolated_mesh.hpp"
#include "cmo.hpp"
#include "crossbar.hpp"
#include "cmesh.hpp"
#include "cmeshx2.hpp"
#include "flatfly_onchip.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
///////////////////////////////////////////////////////////////////////////////


void AllocatorSim( const Configuration& config )
{
  Network *net;
  string topo;

  config.GetStr( "topology", topo );

  /*To include a new network, must register the network here
   *add an else if statement with the name of the network
   */
  if ( topo == "torus" ) {
    net = new KNCube( config, true );
  } else if ( topo == "mesh" ) {
    net = new KNCube( config, false );
  } else if ( topo == "cmesh" ) {
    net = new CMesh( config );
  } else if ( topo == "cmeshx2" ) {
    net = new CMeshX2( config );
  } else if ( topo == "fly" ) {
    net = new KNFly( config );
  } else if ( topo == "single" ) {
    net = new SingleNet( config );
  } else if ( topo == "isolated_mesh" ) {
    net = new IsolatedMesh( config );
  } else if ( topo == "qtree" ) {
    net = new QTree( config );
  } else if ( topo == "tree4" ) {
    net = new Tree4( config );
  } else if ( topo == "fattree" ) {
    net = new FatTree( config );
  } else if ( topo == "crossbar" ) {
    net = new CrossBar( config ) ;
  } else if ( topo == "flatfly" ) {
    net = new FlatFlyOnChip( config );
  }  else if ( topo == "cmo"){
    net = new CMO(config);
  } else {
    cerr << "Unknown topology " << topo << endl;
    exit(-1);
  }

  /*legacy code that insert random faults in the networks
   *not sure how to use this
   */
  if ( config.GetInt( "link_failures" ) ) {
    net->InsertRandomFaults( config );
  }

  string traffic ;
  config.GetStr( "traffic", traffic ) ;

  /*tcc and characterize are legacy
   *not sure how to use them 
   */
  TrafficManager *trafficManager ;
  trafficManager = new TrafficManager( config, net ) ;

  /*Start the simulation run
   */
  trafficManager->Run() ;

  delete trafficManager ;
  delete net;
}



int main( int argc, char **argv )
{
  BookSimConfig config;

  if ( !ParseArgs( &config, argc, argv ) ) {
    cout << "Usage: " << argv[0] << " configfile" << endl;
    return 0;
  }

  /*print the configuration file at the begining of the reprot
   */
  ifstream in(argv[1]);
  char c;
  cout << "BEGIN Configuration File" << endl;
  cout << "Name: " << argv[1] << endl;
  while (!in.eof()) {
    in.get(c);
    cout << c ;
  }
  cout << "END Configuration File" << endl;

  //FUCKneed to consolidate these routing functions
  CMesh::RegisterRoutingFunctions() ;
  CMeshX2::RegisterRoutingFunctions() ;
  FlatFlyOnChip::RegisterRoutingFunctions();
  CMO::RegisterRoutingFunctions();

  /*initialize routing, traffic, injection functions
   */
  InitializeRoutingMap( );
  InitializeTrafficMap( );
  InitializeInjectionMap( );

  RandomSeed( config.GetInt("seed") );

  /*configure and run the simulator
   */
  AllocatorSim( config );

  return 0;
}
