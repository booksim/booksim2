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
#include "cmesh.hpp"
#include "cmeshx2.hpp"
#include "flatfly_onchip.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
///////////////////////////////////////////////////////////////////////////////
//Global declarations
//////////////////////
/* printing activity factor*/
bool _print_activity = false;

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

//generate nocviewer trace
bool _trace = false;
/////////////////////////////////////////////////////////////////////////////

void AllocatorSim( const Configuration& config )
{
  Network *net;
  string topo;

  config.GetStr( "topology", topo );

  /*To include a new network, must register the network here
   *add an else if statement with the name of the network
   */
  if ( topo == "torus" ) {
    KNCube::RegisterRoutingFunctions() ;
    net = new KNCube( config, true );
  } else if ( topo == "mesh" ) {
    KNCube::RegisterRoutingFunctions() ;
    net = new KNCube( config, false );
  } else if ( topo == "cmesh" ) {
    CMesh::RegisterRoutingFunctions() ;
    net = new CMesh( config );
  } else if ( topo == "cmeshx2" ) {
    CMeshX2::RegisterRoutingFunctions() ;
    net = new CMeshX2( config );
  } else if ( topo == "fly" ) {
    KNFly::RegisterRoutingFunctions() ;
    net = new KNFly( config );
  } else if ( topo == "single" ) {
    SingleNet::RegisterRoutingFunctions() ;
    net = new SingleNet( config );
  } else if ( topo == "isolated_mesh" ) {
    IsolatedMesh::RegisterRoutingFunctions() ;
    net = new IsolatedMesh( config );
  } else if ( topo == "qtree" ) {
    QTree::RegisterRoutingFunctions() ;
    net = new QTree( config );
  } else if ( topo == "tree4" ) {
    Tree4::RegisterRoutingFunctions() ;
    net = new Tree4( config );
  } else if ( topo == "fattree" ) {
    FatTree::RegisterRoutingFunctions() ;
    net = new FatTree( config );
  }  else if ( topo == "flatfly" ) {
    FlatFlyOnChip::RegisterRoutingFunctions() ;
    net = new FlatFlyOnChip( config );
  }  else if ( topo == "cmo"){
    CMO::RegisterRoutingFunctions() ;
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

  /*initialize routing, traffic, injection functions
   */
  InitializeRoutingMap( );
  InitializeTrafficMap( );
  InitializeInjectionMap( );
  RandomSeed( config.GetInt("seed") );

  _print_activity = (config.GetInt("print_activity")==1);
  _trace = (config.GetInt("viewer trace")==1);

  /*configure and run the simulator
   */
  AllocatorSim( config );

  return 0;
}
