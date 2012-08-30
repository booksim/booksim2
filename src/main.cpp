// $Id: main.cpp 1138 2009-03-02 02:08:15Z qtedq $

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
#include <time.h>
#include <sys/time.h>
#include "booksim.hpp"
#include "routefunc.hpp"
#include "traffic.hpp"
#include "booksim_config.hpp"
#include "trafficmanager.hpp"
#include "ptrafficmanager.hpp"
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
#include "anynet.hpp"
#include "dragonfly.hpp"
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
 *under NOCs since NOCS are inheriently 2 dimension
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

/*number of flits per packet, when _use_read_write is false*/

int    gConstPacketSize;

//for on_off injections
int *gNodeStates = 0;

//flits to watch
string watch_file;

//latency type, noc or conventional network
bool _use_noc_latency;

int _threads =1;
int _thread_id =0; 
/////////////////////////////////////////////////////////////////////////////

MPI_Datatype MPI_Flit;
MPI_Datatype MPI_Credit;


int global_time = 0;

bool AllocatorSim( const Configuration& config )
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
    } else if ( topo == "anynet"){
      AnyNet::RegisterRoutingFunctions() ;
      net[i] = new AnyNet(config);
    } else if ( topo == "dragonflynew"){
      DragonFlyNew::RegisterRoutingFunctions() ;
      net[i] = new DragonFlyNew(config);
    }else {
      cerr << "Unknown topology " << topo << endl;
      exit(-1);
    }


    net[i]->Divide(_thread_id,_threads);
  }

  

  /*tcc and characterize are legacy
   *not sure how to use them 
   */
//   TrafficManager *trafficManager ;
//   trafficManager = new TrafficManager( config, net ) ;

  PTrafficManager *trafficManager ;
  trafficManager = new PTrafficManager( config, net ) ;

  /*Start the simulation run
   */

  if(_thread_id == 0){
    double total_time; /* Amount of time we've run */
    total_time = 0.0;
    double start_time = MPI_Wtime();
    bool result = trafficManager->Run() ;
    double end_time = MPI_Wtime();
    total_time = end_time - start_time;
    cout<<"Total run time "<<total_time<<endl;
  }else {
    bool result = trafficManager->Run() ;
    
  }
  cout<<_thread_id<<" done\n";
  MPI_Barrier(MPI_COMM_WORLD);
  ///Power analysis
  if(config.GetInt("sim_power")==1){
    Power_Module * pnet = new Power_Module(net[0], trafficManager, config);
    pnet->run();
    delete pnet;
  }

  delete trafficManager ;
  for (int i=0; i<networks; ++i)
    delete net[i];
  delete [] net;

  return 1;
}




#include "flit.hpp"
int main( int argc, char **argv )
{

  int rc = MPI_Init(&argc, &argv);
  if (rc != MPI_SUCCESS) {
    cout<<"can't initialize mpi\n";
    MPI_Abort(MPI_COMM_WORLD, rc);
  }
  MPI_Type_contiguous(sizeof(Flit),MPI_BYTE,&MPI_Flit);
  MPI_Type_commit(&MPI_Flit);
  MPI_Type_contiguous(sizeof(Credit),MPI_BYTE,&MPI_Credit);
  MPI_Type_commit(&MPI_Credit);

  MPI_Comm_size(MPI_COMM_WORLD, &_threads);
  MPI_Comm_rank(MPI_COMM_WORLD, &_thread_id);


  cout<<"Process "<<_thread_id<<" of "<<_threads<<" started"<<endl;
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
      //cout << c ;
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
    bool result = AllocatorSim( config );
    
    MPI_Finalize();
    return result ? -1 : 0;
}
