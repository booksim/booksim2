// $Id$

/*
Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
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

#include "booksim.hpp"
#include <map>
#include <stdlib.h>

#include "traffic.hpp"
#include "network.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"

map<string, tTrafficFunction> gTrafficFunctionMap;

int gResetTraffic = 0;
int gStepTraffic  = 0;

void src_dest_bin( int source, int dest, int lg )
{
  int b, t;

  cout << "from: ";
  t = source;
  for ( b = 0; b < lg; ++b ) {
    cout << ( ( t >> ( lg - b - 1 ) ) & 0x1 );
  }
  
  cout << " to ";
  t = dest;
  for ( b = 0; b < lg; ++b ) {
    cout << ( ( t >> ( lg - b - 1 ) ) & 0x1 );
  }
  cout << endl;
}

/* Add Traffic functions here */







//=============================================================

int uniform( int source, int total_nodes )
{
  return RandomInt( total_nodes - 1 );
}

//=============================================================

int bitcomp( int source, int total_nodes )
{
  int lg   = log_two( total_nodes );
  int mask = total_nodes - 1;
  int dest;

  if ( ( 1 << lg ) != total_nodes ) {
    cout << "Error: The 'bitcomp' traffic pattern requires the number of"
	 << " nodes to be a power of two!" << endl;
    exit(-1);
  }

  dest = ( ~source ) & mask;

  return dest;
}

//=============================================================

int transpose( int source, int total_nodes )
{
  int lg      = log_two( total_nodes );
  int mask_lo = (1 << (lg/2)) - 1;
  int mask_hi = mask_lo << (lg/2);
  int dest;

  if ( ( ( 1 << lg ) != total_nodes ) || ( lg & 0x1 ) ) {
    cout << "Error: The 'transpose' traffic pattern requires the number of"
	 << " nodes to be an even power of two!" << endl;
    exit(-1);
  }

  dest = ( ( source >> (lg/2) ) & mask_lo ) |
    ( ( source << (lg/2) ) & mask_hi );

  return dest;
}

//=============================================================

int bitrev( int source, int total_nodes )
{
  int lg = log_two( total_nodes );
  int dest;

  if ( ( 1 << lg ) != total_nodes  ) {
    cout << "Error: The 'bitrev' traffic pattern requires the number of"
	 << " nodes to be a power of two!" << endl;
    exit(-1);
  }

  // If you were fancy you could do this in O(log log total_nodes)
  // instructions, but I'm not

  dest = 0;
  for ( int b = 0; b < lg; ++b  ) {
    dest |= ( ( source >> b ) & 0x1 ) << ( lg - b - 1 );
  }

  return dest;
}

//=============================================================

int shuffle( int source, int total_nodes )
{
  int lg = log_two( total_nodes );
  int dest;

  if ( ( 1 << lg ) != total_nodes  ) {
    cout << "Error: The 'shuffle' traffic pattern requires the number of"
	 << " nodes to be a power of two!" << endl;
    exit(-1);
  }

  dest = ( ( source << 1 ) & ( total_nodes - 1 ) ) | 
    ( ( source >> ( lg - 1 ) ) & 0x1 );

  return dest;
}

//=============================================================

int tornado( int source, int total_nodes )
{
  int offset = 1;
  int dest = 0;

  for ( int n = 0; n < realgn; ++n ) {
    dest += offset *
      ( ( ( source / offset ) % realgk + ( realgk/2 - 1 ) ) % realgk );
    offset *= realgk;
  }
  //cout<<source<<" "<<dest<<endl;
  return dest;
}

//=============================================================

int neighbor( int source, int total_nodes )
{
  int offset = 1;
  int dest = 0;

  for ( int n = 0; n < realgn; ++n ) {
    dest += offset *
      ( ( ( source / offset ) % realgk + 1 ) % realgk );
    offset *= realgk;
  }

  //cout<<"Source "<<source<<" destination "<<dest<<endl;
  return dest;
}

//=============================================================

int *gPerm = 0;
int gPermSeed;

void GenerateRandomPerm( int total_nodes )
{
  int ind;
  int i,j;
  int cnt;
  unsigned long prev_rand;
  
  prev_rand = RandomIntLong( );
  RandomSeed( gPermSeed );

  if ( !gPerm ) {
    gPerm = new int [total_nodes];
  }

  for ( i = 0; i < total_nodes; ++i ) {
    gPerm[i] = -1;
  }

  for ( i = 0; i < total_nodes; ++i ) {
    ind = RandomInt( total_nodes - 1 - i );
    
    j   = 0;
    cnt = 0;
    while( ( cnt < ind ) ||
	   ( gPerm[j] != -1 ) ) {
      if ( gPerm[j] == -1 ) { ++cnt; }
      ++j;

      if ( j >= total_nodes ) {
	cout << "ERROR: GenerateRandomPerm( ) internal error" << endl;
	exit(-1);
      }
    }
    
    gPerm[j] = i;
  }

  RandomSeed( prev_rand );
}

int randperm( int source, int total_nodes )
{
  if ( gResetTraffic || !gPerm ) {
    GenerateRandomPerm( total_nodes );
    gResetTraffic = 0;
  }

  return gPerm[source];
}

//=============================================================

int diagonal( int source, int total_nodes )
{
  int t = RandomInt( 2 );
  int d;

  // 2/3 of traffic goes from source->source
  // 1/3 of traffic goes from source->(source+1)%total_nodes

  if ( t == 0 ) {
    d = ( source + 1 ) % total_nodes;
  } else {
    d = source;
  }

  return d;
}

//=============================================================

int asymmetric( int source, int total_nodes )
{
  int d;
  int half = total_nodes / 2;
  
  d = ( source % half ) + RandomInt( 1 ) * half;

  return d;
}

//=============================================================

int taper64( int source, int total_nodes )
{
  int d;

  if ( total_nodes != 64 ) {
    cout << "Error: The 'taper64' traffic pattern requires the number of"
	 << " nodes to be 64!" << endl;
    exit(-1);
  }

  if (RandomInt(1)) {
    d = (64 + source + 8*(RandomInt(2) - 1) + (RandomInt(2) - 1)) % 64;

  } else {
    d = RandomInt( total_nodes - 1 );
  }

  return d;
}

//=============================================================

int badperm_dfly( int source, int total_nodes )
{
  int grp_size_routers = powi(realgk, realgn - 1);
  int grp_size_nodes = grp_size_routers * realgk;

  int temp;
  int dest;

  temp = (int) (source / grp_size_nodes);
  dest =  (RandomInt(grp_size_nodes - 1) + (temp+1)*grp_size_nodes ) %  total_nodes;

  return dest;
}

int badperm_dflynew( int source, int total_nodes )
{
  int grp_size_routers = 2*realgk;
  int grp_size_nodes = grp_size_routers * realgk;

  int temp;
  int dest;

  temp = (int) (source / grp_size_nodes);
  dest =  (RandomInt(grp_size_nodes - 1) + (temp+1)*grp_size_nodes ) %  total_nodes;

  return dest;
}

void InitializeTrafficMap( )
{


  /* Register Traffic functions here */

  gTrafficFunctionMap["uniform"]  = &uniform;

  // "Bit" patterns

  gTrafficFunctionMap["bitcomp"]   = &bitcomp;
  gTrafficFunctionMap["bitrev"]    = &bitrev;
  gTrafficFunctionMap["transpose"] = &transpose;
  gTrafficFunctionMap["shuffle"]   = &shuffle;

  // "Digit" patterns

  gTrafficFunctionMap["tornado"]   = &tornado;
  gTrafficFunctionMap["neighbor"]  = &neighbor;

  // Other patterns

  gTrafficFunctionMap["randperm"]   = &randperm;

  gTrafficFunctionMap["diagonal"]   = &diagonal;
  gTrafficFunctionMap["asymmetric"] = &asymmetric;
  gTrafficFunctionMap["taper64"]    = &taper64;

  gTrafficFunctionMap["bad_dragon"]    = &badperm_dflynew;

}

void ResetTrafficFunction( )
{
  gResetTraffic++;
}

void StepTrafficFunction( )
{
  gStepTraffic++;
}


tTrafficFunction GetTrafficFunction( const Configuration& config )
{

  if(config.GetInt( "c" )!=1){
    int temp =  config.GetInt("xr");
    realgk = temp*gK;
    realgn = gN;
  } else {
    realgk = gK;
    realgn = gN;
  }

  string topo;
  config.GetStr( "topology", topo );

  map<string, tTrafficFunction>::const_iterator match;
  tTrafficFunction tf;

  string fn;

  config.GetStr( "traffic", fn, "none" );
  match = gTrafficFunctionMap.find( fn );

  if ( match != gTrafficFunctionMap.end( ) ) {
    tf = match->second;
  } else {
    cout << "Error: Undefined traffic pattern '" << fn << "'." << endl;
    exit(-1);
  }

  gPermSeed = config.GetInt( "perm_seed" );

  //seed the network
  RandomSeed(config.GetInt("seed"));
  return tf;
}

