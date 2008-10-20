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
#include "power_module.hpp"

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
#include "mecs.hpp"
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

/*false means all packet types are the same length "gConstantsize"
 *All packets uses all VCS
 *packet types are generated randomly, essentially making it only 1 type
 *of packet in the network
 *
 *True means only request packets are generated and replies are generated
 *as a response to the requests, packets are now difference length, correspond
 *to "read_request_size" etc. 
 */
bool _use_read_write = false;

//injection functions
map<string, tInjectionProcess> gInjectionProcessMap;

//burst rates
double gBurstAlpha;
double gBurstBeta;

/*number of flits per packet, set by the configuration file*/
int    gConstPacketSize;

//for on_off injections
int *gNodeStates = 0;

//flits to watch
string watch_file;

//latency type, noc or conventional network
bool _use_noc_latency;
/////////////////////////////////////////////////////////////////////////////

void AllocatorSim( const Configuration& config )
{
  Network **net;
  string topo;

  config.GetStr( "topology", topo );
  short networks = config.GetInt("physical_subnetworks");
  /*To include a new network, must register the network here
   *add an else if statement with the name of the network
   */
  net = new Network*[networks];
  for (int i = 0; i < networks; ++i) {
    if ( topo == "torus" ) {
      KNCube::RegisterRoutingFunctions() ;
      net[i] = new KNCube( config, true );
    } else if ( topo == "mesh" ) {
      KNCube::RegisterRoutingFunctions() ;
      net[i] = new KNCube( config, false );
    } else if ( topo == "cmesh" ) {
      CMesh::RegisterRoutingFunctions() ;
      net[i] = new CMesh( config );
    } else if ( topo == "cmeshx2" ) {
      CMeshX2::RegisterRoutingFunctions() ;
      net[i] = new CMeshX2( config );
    } else if ( topo == "fly" ) {
      KNFly::RegisterRoutingFunctions() ;
      net[i] = new KNFly( config );
    } else if ( topo == "single" ) {
      SingleNet::RegisterRoutingFunctions() ;
      net[i] = new SingleNet( config );
    } else if ( topo == "isolated_mesh" ) {
      IsolatedMesh::RegisterRoutingFunctions() ;
      net[i] = new IsolatedMesh( config );
    } else if ( topo == "qtree" ) {
      QTree::RegisterRoutingFunctions() ;
      net[i] = new QTree( config );
    } else if ( topo == "tree4" ) {
      Tree4::RegisterRoutingFunctions() ;
      net[i] = new Tree4( config );
    } else if ( topo == "fattree" ) {
      FatTree::RegisterRoutingFunctions() ;
      net[i] = new FatTree( config );
    } else if ( topo == "flatfly" ) {
      FlatFlyOnChip::RegisterRoutingFunctions() ;
      net[i] = new FlatFlyOnChip( config );
    } else if ( topo == "cmo"){
      CMO::RegisterRoutingFunctions() ;
      net[i] = new CMO(config);
    } else if ( topo == "MECS"){
      MECS::RegisterRoutingFunctions() ;
      net[i] = new MECS(config);
    } else {
      cerr << "Unknown topology " << topo << endl;
      exit(-1);
    }

  /*legacy code that insert random faults in the networks
   *not sure how to use this
   */
  if ( config.GetInt( "link_failures" ) ) {
      net[i]->InsertRandomFaults( config );
    }
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

  ///Power analysis

  Power_Module * pnet = new Power_Module(net[0], trafficManager, config);
  pnet->run();


  delete trafficManager ;
  for (int i=0; i<networks; ++i)
    delete net[i];
  delete [] net;
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

  _print_activity = (config.GetInt("print_activity")==1);
  _trace = (config.GetInt("viewer trace")==1);
  
  _use_read_write = (config.GetInt("use_read_write")==1);
  
  config.GetStr( "watch_file", watch_file );

  _use_noc_latency = (config.GetInt("use_noc_latency")==1);
  /*configure and run the simulator
   */
  AllocatorSim( config );

  return 0;
}
