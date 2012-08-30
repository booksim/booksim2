// $Id: injection.cpp 1969 2010-05-10 11:09:22Z dub $

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
 *The rate is packet rate not flit rate.
 *
 */

#include "booksim.hpp"
#include <map>
#include <cassert>

#include "injection.hpp"
#include "network.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"

map<string, tInjectionProcess> gInjectionProcessMap;

//=============================================================

bool bernoulli( int source, double rate )
{

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

//=============================================================

void InitializeInjectionMap( const Configuration & config )
{

  gBurstAlpha = config.GetFloat( "burst_alpha" );
  gBurstBeta  = config.GetFloat( "burst_beta" );

  /* Register injection processes functions here */

  gInjectionProcessMap["bernoulli"] = &bernoulli;
  gInjectionProcessMap["on_off"]    = &on_off;

}
