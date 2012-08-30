// $Id$

/*
Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this 
list of conditions and the following disclaimer in the documentation and/or 
other materials provided with the distribution.
Neither the name of the Stanford University nor the names of its contributors 
may be used to endorse or promote products derived from this software without 
specific prior written permission.

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

int gK;//radix
int gN;//dimension
int gC;//concentration

int gNodes;

//generate nocviewer trace
bool gTrace;

ostream * gWatchOut;

#ifdef USE_GUI
bool gGUIMode = false;
#endif

/////////////////////////////////////////////////////////////////////////////

bool Simulate( BookSimConfig const & config )
{
  vector<Booksim_Network *> net;

  int subnets = config.GetInt("subnets");
  /*To include a new network, must register the network here
   *add an else if statement with the name of the network
   */
  net.resize(subnets);
  for (int i = 0; i < subnets; ++i) {
    ostringstream name;
    name << "network_" << i;
    net[i] = Booksim_Network::NewNetwork( config, name.str() );
  }

  /*tcc and characterize are legacy
   *not sure how to use them 
   */

  assert(trafficManager == NULL);
  trafficManager = TrafficManager::NewTrafficManager( config, net ) ;

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

  for (int i=0; i<subnets; ++i) {

    ///Power analysis
    if(config.GetInt("sim_power") > 0){
      Power_Module pnet(net[i], config);
      pnet.run();
    }
  }

  delete trafficManager;
  trafficManager = NULL;

  return result;
}

void test();

#ifdef BOOKSIM_STANDALONE
int main( int argc, char **argv )
{


  BookSimConfig config;

#ifdef USE_GUI
  for(int i = 1; i < argc; ++i) {
    string arg(argv[i]);
    if(arg=="-g"){
      gGUIMode = true;
      break;
    }
  }
#endif
  if ( !ParseArgs( &config, argc, argv ) ) {
#ifdef USE_GUI
    if(gGUIMode){
      cout<< "No config file found"<<endl;
      cout<< "Usage: " << argv[0] << " configfile... [param=value...]" << endl;
      cout<< "GUI is using default parameters instead"<<endl;
    } else {
#endif
    cerr << "Usage: " << argv[0] << " configfile... [param=value...]" << endl;
    return 0;
 
#ifdef USE_GUI
    }
#endif
 } 

  
  /*initialize routing, traffic, injection functions
   */
  InitializeRoutingMap( config );

  gPrintActivity = (config.GetInt("print_activity") > 0);
  gTrace = (config.GetInt("viewer_trace") > 0);
  
  string watch_out_file = config.GetStr( "watch_out" );
  if(watch_out_file == "") {
    gWatchOut = NULL;
  } else if(watch_out_file == "-") {
    gWatchOut = &cout;
  } else {
    gWatchOut = new ofstream(watch_out_file.c_str());
  }
  

  /*configure and run the simulator
   */

#ifdef USE_GUI
  if(gGUIMode){
    cout<<"GUI Mode\n";
    QApplication app(argc, argv);
    BooksimGUI * bs = new BooksimGUI();
    //transfer all the contorl and data to the gui, go to bgui.cpp for the rest
    bs->RegisterSimFunc(&Simulate,&config);
    bs->setGeometry(100, 100, 1200, 355);
    bs->show();
    return app.exec();
  }
#endif

  bool result = Simulate( config );
  return result ? -1 : 0;
}


#include "weighted_rr_arb.hpp"
void test(){

  int ports = 10;
  int trials = 100000;
  float rates[] = {
    0.2,
    0.2,
    0.2,
    0.2,
    0.3,
    0.3,
    0.3,
    0.2,
    0.2,
    0.2
  };
  int pri[] = {
    4,
    1,
    1,
    3,
    3,
    5,
    3,
    1,
    5,
    1
  };
  int* buffer = new int[ports];
  int* results = new int[ports];
  WeightedRRArbiter * wrr = new WeightedRRArbiter(NULL, "lol",ports, false);

  for(int j = 0; j<ports; j++){
    results[j]=0;
    buffer[j]=0;
  }
  for(int i = 0; i<trials; i++){
    int req = 0;
    for(int j = 0; j<ports; j++){
      if(RandomFloat()<rates[j]/3.6){
	buffer[j]++;
      }
      if(buffer[j]>0){
	wrr->AddRequest(j,0,pri[j]);
	req++;
      }
    }
    if(req>0){
      int result = wrr->Arbitrate();
      wrr->UpdateState();
      wrr->Clear();
      results[result]++;
      buffer[result]--;
    }
  }

  for(int j = 0; j<ports; j++){
    cout<<float(results[j])/trials*100<<"\t";
  }
  cout<<"\n";
}
#endif
