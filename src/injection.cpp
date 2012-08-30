// $Id: injection.cpp 938 2008-12-12 03:06:32Z dub $

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

/*injection.cpp
 *
 *Class of injection methods, bernouli and on_off
 *
 *The rate is packet rate not flit rate. Each time a packet is generated
 *gConstPacketSize number of flits are generated
 */

#include "booksim.hpp"
#include <map>
#include <assert.h>

#include "injection.hpp"
#include "network.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"

extern map<string, tInjectionProcess> gInjectionProcessMap;

//=============================================================

int bernoulli( int /*source*/, double rate )
{
  //this is the packet injection rate, not flit rate
  return ( RandomFloat( ) < ( rate ) ) ? 
    gConstPacketSize : 0;
}

//=============================================================

int on_off( int source, double rate )
{
  double r1;
  bool issue;

  assert( ( source >= 0 ) && ( source < gNodes ) );

  if ( !gNodeStates ) {
    gNodeStates = new int [gNodes];

    for ( int n = 0; n < gNodes; ++n ) {
      gNodeStates[n] = 0;
    }
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

  issue = false;
  if ( gNodeStates[source] ) { // on?
    r1 = rate * ( 1.0 + gBurstBeta / gBurstAlpha ) / 
      (double)gConstPacketSize;

    if ( RandomFloat( ) < r1 ) {
      issue = true;
    }
  }

  return issue ? gConstPacketSize : 0;
}

//=============================================================

void InitializeInjectionMap( )
{
  /* Register injection processes functions here */

  gInjectionProcessMap["bernoulli"] = &bernoulli;
  gInjectionProcessMap["on_off"]    = &on_off;
}

tInjectionProcess GetInjectionProcess( const Configuration& config )
{
  map<string, tInjectionProcess>::const_iterator match;
  tInjectionProcess ip;

  string fn;

  config.GetStr( "injection_process", fn );
  match = gInjectionProcessMap.find( fn );

  if ( match != gInjectionProcessMap.end( ) ) {
    ip = match->second;
  } else {
    cout << "Error: Undefined injection process '" << fn << "'." << endl;
    exit(-1);
  }

  gConstPacketSize = config.GetInt( "const_flits_per_packet" );
  gBurstAlpha      = config.GetFloat( "burst_alpha" );
  gBurstBeta       = config.GetFloat( "burst_beta" );

  return ip;
}
