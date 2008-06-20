#include "booksim.hpp"
#include <string>
#include <stdlib.h>
#include <fstream>

#include "routefunc.hpp"
#include "traffic.hpp"
#include "booksim_config.hpp"
#include "trafficmanager.hpp"
#include "random_utils.hpp"
#include "network.hpp"
#include "singlenet.hpp"
#include "kncube.hpp"
#include "fly.hpp"
#include "injection.hpp"
#include "isolated_mesh.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
#include "power.hpp"
#include "cmesh.hpp"
#include "cmeshx2.hpp"
#include "flatfly_onchip.hpp"
#include "crossbar.hpp"
#include "tcctrafficmanager.hpp"
#include "characterize.hpp"
#include "cmo.hpp"
//god help me

void AllocatorSim( const Configuration& config )
{
  Network *net;
  string topo;


  SHORT_FLIT_WIDTH = 144;
  LONG_FLIT_WIDTH =  144;
//   switch ( config.GetInt("read_reply_size") ){
//     case 2: LONG_FLIT_WIDTH = 288; break;
//     case 3: LONG_FLIT_WIDTH = 192; break;
//     case 4: LONG_FLIT_WIDTH = 144; break;
//     case 5: LONG_FLIT_WIDTH = 116; break;
//     case 6: LONG_FLIT_WIDTH =  96; break;
//     case 7: LONG_FLIT_WIDTH =  83; break;
//     case 8: LONG_FLIT_WIDTH =  72; break;
//     case 9: LONG_FLIT_WIDTH =  64; break;
//     default:
//       cout << "WARNING: flit width set to 64-bit default" << endl ;
//       LONG_FLIT_WIDTH = 64 ;
//   }

  config.GetStr( "topology", topo );

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

  if ( config.GetInt( "link_failures" ) ) {
    net->InsertRandomFaults( config );
  }

  cout << "fClk = " << fClk << endl
       << "tClk = " << tClk << endl;

  string traffic ;
  config.GetStr( "traffic", traffic ) ;

  TrafficManager *trafficManager ;
  if ( traffic == "tcc" )
    trafficManager = new TccTrafficManager( config, net ) ;
  else if ( traffic == "characterize" )
    trafficManager = new CharacterizeTrafficManager( config, net ) ;
  else
    trafficManager = new TrafficManager( config, net ) ;

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

  ifstream in(argv[1]);
  char c;
  cout << "BEGIN Configuration File" << endl;
  cout << "Name: " << argv[1] << endl;
  while (!in.eof()) {
    in.get(c);
    cout << c ;
  }
  cout << "END Configuration File" << endl;

  CMesh::RegisterRoutingFunctions() ;
  CMeshX2::RegisterRoutingFunctions() ;
  FlatFlyOnChip::RegisterRoutingFunctions();
  CMO::RegisterRoutingFunctions();
  InitializeRoutingMap( );
  InitializeTrafficMap( );
  InitializeInjectionMap( );

  RandomSeed( config.GetInt("seed") );

  AllocatorSim( config );

  return 0;
}
