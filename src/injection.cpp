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

/*injection.cpp
 *
 *Class of injection methods, bernouli and on_off
 *
 *The rate is packet rate not flit rate.
 *
 */

#include "booksim.hpp"
#include <map>
#include <cassert>
#include <set>

#include "injection.hpp"
#include "network.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"

map<string, tInjectionProcess> gInjectionProcessMap;
bool permanent_flow;
extern set<int> hs_lookup;
map<int, double> congestion_rate_lookup;
extern int bystander_sender;
extern bool hs_send_all;
extern set<int> hs_senders;

bool background( int source, double rate )
{

  if(hs_senders.count(source)!=0 || hs_lookup.count(source)!=0){
    return false;
  } else {
    return (RandomFloat( ) < rate);
  }
}
//=============================================================


bool bernoulli( int source, double rate )
{

  if(permanent_flow){
    if(GetSimTime()==0)
      rate = 1.0;
    else 
      rate = 0.0;
  }

  assert( ( source >= 0 ) && ( source < gNodes ) );
  assert( rate <= 1.0 );

  //this is the packet injection rate, not flit rate
  return (RandomFloat( ) < rate);
}

//=============================================================

//burst rates
static double gBurstAlpha;
static double gBurstBeta;

static std::vector<int> gNodeStates;

bool on_off( int source, double rate )
{
  assert( ( source >= 0 ) && ( source < gNodes ) );
  assert( rate <= 1.0 );

  if ( gNodeStates.size() != (size_t)gNodes ) {
    gNodeStates.resize(gNodes, 0);
  }

  // advance state

  if ( gNodeStates[source] == 0 ) {
    if ( RandomFloat( ) < gBurstAlpha ) { // from off to on
      gNodeStates[source] = 1;
    }
  } else if ( RandomFloat( ) < gBurstBeta ) { // from on to off
    gNodeStates[source] = 0;
  }

  // generate packet

  if ( gNodeStates[source] ) { // on?
    double r1 = rate * (gBurstAlpha + gBurstBeta) / gBurstAlpha;
    return (RandomFloat( ) < r1);
  }

  return false;
}



//special injection function fo congestion test, the rate argumetn does nothing
//rate is overloaded onthe "hotspot"rate option
bool injection_congestion_test( int source, double rate ){
  if(congestion_rate_lookup.count(source)!= 0){
    return bernoulli( source, congestion_rate_lookup[source] );
  } else if(source == bystander_sender){
    return bernoulli( source, rate );
  }else {
    return false;
  }
}


bool injection_burst(int source, double rate){
  if(hs_senders.count(source)!=0){
    return true;
  } else {
    return false;
  }
}

bool injection_hotspot_test(int source, double rate){
  if(hs_lookup.count(source)!=0){
    return false;
  } else {
    if( hs_send_all || hs_senders.count(source)!=0){
      return bernoulli(source,rate);
    } else {
      return false;
    }
  }
}


//=============================================================

void InitializeInjectionMap( const Configuration & config )
{

  gBurstAlpha = config.GetFloat( "burst_alpha" );
  gBurstBeta  = config.GetFloat( "burst_beta" );

  /* Register injection processes functions here */

  gInjectionProcessMap["bernoulli"] = &bernoulli;
  gInjectionProcessMap["on_off"]    = &on_off;
  gInjectionProcessMap["burst"] = &injection_burst;
  gInjectionProcessMap["congestion_test"]    = &injection_congestion_test;
  gInjectionProcessMap["hotspot_test"]    = &injection_hotspot_test;
  gInjectionProcessMap["background"]    = &background;

  permanent_flow = (config.GetInt("create_permanent_flows")==1);
  vector<int> hotspot_senders = config.GetIntArray("hotspot_senders");
  vector<double> hotspot_rates = config.GetFloatArray("hotspot_rates");

  if(config.GetStr("injection_process") == "congestion_test"){
    congestion_rate_lookup.clear();
    int _flow_size = config.GetInt("flow_size");
    int  _packet_size = config.GetInt("const_flits_per_packet");
    for(size_t i = 0; i<hotspot_senders.size(); i++){
      double rate = 0.0;
      if(i<hotspot_rates.size()){
	rate =  hotspot_rates[i];
      } else {
	rate =  hotspot_rates.back();
      }
      
      if(config.GetInt("injection_rate_uses_flits")) {
	rate /= (double)_packet_size;
	rate /=_flow_size;
      }
      congestion_rate_lookup.insert(pair<int, double>(hotspot_senders[i],rate));
    }
  }
}
