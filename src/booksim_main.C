// $Id: main.cpp 2842 2010-11-12 03:13:28Z dub $

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
#include <sys/time.h>

#include <string>
#include <cstdlib>
#include <iostream>
#include <fstream>

#ifdef USE_GUI
#include <QApplication>
#include "bgui.hpp"
#endif

#include <sstream>
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
#include "cmesh.hpp"
#include "flatfly_onchip.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
#include "anynet.hpp"
#include "dragonfly.hpp"
///////////////////////////////////////////////////////////////////////////////
//Global declarations
//////////////////////

 /* the current traffic manager instance */
TrafficManager * trafficManager = NULL;

int GetSimTime() {
  return trafficManager->getTime();
}

class Stats;
Stats * GetStats(const std::string & name) {
  Stats* test =  trafficManager->getStats(name);
  if(test == 0){
    cout<<"warning statistics "<<name<<" not found"<<endl;
  }
  return test;
}

/* printing activity factor*/
bool gPrintActivity;

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
bool gTrace;

/*number of flits per packet, when _use_read_write is false*/
int    gConstPacketSize;

ostream * gWatchOut;

#ifdef USE_GUI
bool gGUIMode = false;
#endif

/////////////////////////////////////////////////////////////////////////////

bool AllocatorSim( const Configuration& config )
{
  vector<BSNetwork *> net;
  string topo;

  topo = config.GetStr( "topology");
  int networks = config.GetInt("physical_subnetworks");
  /*To include a new network, must register the network here
   *add an else if statement with the name of the network
   */
  net.resize(networks);
  for (int i = 0; i < networks; ++i) {
    ostringstream name;
    name << "network_" << i;
    if ( topo == "torus" ) {
      KNCube::RegisterRoutingFunctions() ;
      net[i] = new KNCube( config, name.str(), false );
    } else if ( topo == "mesh" ) {
      KNCube::RegisterRoutingFunctions() ;
      net[i] = new KNCube( config, name.str(), true );
    } else if ( topo == "cmesh" ) {
      CMesh::RegisterRoutingFunctions() ;
      net[i] = new CMesh( config, name.str() );
    }  else if ( topo == "fly" ) {
      KNFly::RegisterRoutingFunctions() ;
      net[i] = new KNFly( config, name.str() );
    } else if ( topo == "single" ) {
      SingleNet::RegisterRoutingFunctions() ;
      net[i] = new SingleNet( config, name.str() );
    } else if ( topo == "qtree" ) {
      QTree::RegisterRoutingFunctions() ;
      net[i] = new QTree( config, name.str() );
    } else if ( topo == "tree4" ) {
      Tree4::RegisterRoutingFunctions() ;
      net[i] = new Tree4( config, name.str() );
    } else if ( topo == "fattree" ) {
      FatTree::RegisterRoutingFunctions() ;
      net[i] = new FatTree( config, name.str() );
    } else if ( topo == "flatfly" ) {
      FlatFlyOnChip::RegisterRoutingFunctions() ;
      net[i] = new FlatFlyOnChip( config, name.str() );
    } else if ( topo == "anynet"){
      AnyNet::RegisterRoutingFunctions() ;
      net[i] = new AnyNet(config, name.str());
    } else if ( topo == "dragonflynew"){
      DragonFlyNew::RegisterRoutingFunctions() ;
      net[i] = new DragonFlyNew(config, name.str());
    }else {
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
  traffic = config.GetStr( "traffic" ) ;

  /*tcc and characterize are legacy
   *not sure how to use them 
   */

  assert(trafficManager == NULL);
  trafficManager = new TrafficManager( config, net ) ;

  /*Start the simulation run
   */

  double total_time; /* Amount of time we've run */
  struct timeval start_time, end_time; /* Time before/after user code */
  total_time = 0.0;
  gettimeofday(&start_time, NULL);

  bool result = trafficManager->Run() ;


  gettimeofday(&end_time, NULL);
  total_time = ((double)(end_time.tv_sec) + (double)(end_time.tv_usec)/1000000.0)
            - ((double)(start_time.tv_sec) + (double)(start_time.tv_usec)/1000000.0);

  cout<<"Total run time "<<total_time<<endl;


  ///Power analysis
  if(config.GetInt("sim_power")==1){
    Power_Module * pnet = new Power_Module(net[0], trafficManager, config);
    pnet->run();
    delete pnet;
  }

  for (int i=0; i<networks; ++i)
    delete net[i];

  delete trafficManager;
  trafficManager = NULL;

  return result;
}

