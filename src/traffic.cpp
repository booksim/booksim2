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

#include <map>
#include <cstdlib>

#include "booksim.hpp"
#include "booksim_config.hpp"
#include "traffic.hpp"
#include "network.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"

static map<string, tTrafficFunction> gTrafficFunctionMap;

static int gResetTraffic = 0;
static int gStepTraffic  = 0;

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

static int *gPerm = 0;
static int gPermSeed;

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

int badperm_yarc(int source, int total_nodes){
  int row = (int)(source/realgk);
  
  return RandomInt(realgk-1)*realgk+row;
}

//=============================================================

static int _hs_max_val;
static vector<pair<int, int> > _hs_elems;

int hotspot(int source, int total_nodes){
  int pct = RandomInt(_hs_max_val);
  for(int i = 0; i < (_hs_elems.size()-1); ++i) {
    int limit = _hs_elems[i].first;
    if(limit > pct) {
      return _hs_elems[i].second;
    } else {
      pct -= limit;
    }
  }
  assert(_hs_elems.back().first > pct);
  return _hs_elems.back().second;
}

//=============================================================

static int _cp_max_val;
static vector<pair<int, tTrafficFunction> > _cp_elems;

int combined(int source, int total_nodes){
  int pct = RandomInt(_cp_max_val);
  for(int i = 0; i < (_cp_elems.size()-1); ++i) {
    int limit = _cp_elems[i].first;
    if(limit > pct) {
      return _cp_elems[i].second(source, total_nodes);
    } else {
      pct -= limit;
    }
  }
  assert(_cp_elems.back().first > pct);
  return _cp_elems.back().second(source, total_nodes);
}

//=============================================================

void InitializeTrafficMap( )
{


  /* Register Traffic functions here */

  gTrafficFunctionMap["uniform"] = &uniform;

  // "Bit" patterns

  gTrafficFunctionMap["bitcomp"]   = &bitcomp;
  gTrafficFunctionMap["bitrev"]    = &bitrev;
  gTrafficFunctionMap["transpose"] = &transpose;
  gTrafficFunctionMap["shuffle"]   = &shuffle;

  // "Digit" patterns

  gTrafficFunctionMap["tornado"]  = &tornado;
  gTrafficFunctionMap["neighbor"] = &neighbor;

  // Other patterns

  gTrafficFunctionMap["randperm"] = &randperm;

  gTrafficFunctionMap["diagonal"]   = &diagonal;
  gTrafficFunctionMap["asymmetric"] = &asymmetric;
  gTrafficFunctionMap["taper64"]    = &taper64;

  gTrafficFunctionMap["bad_dragon"]   = &badperm_dflynew;
  gTrafficFunctionMap["badperm_yarc"] = &badperm_yarc;

  gTrafficFunctionMap["hotspot"]  = &hotspot;
  gTrafficFunctionMap["combined"] = &combined;

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

  string hotspot_nodes_str = config.GetStr("hotspot_nodes");
  vector<string> hotspot_nodes = BookSimConfig::tokenize(hotspot_nodes_str);
  string hotspot_rates_str = config.GetStr("hotspot_rates");
  vector<string> hotspot_rates = BookSimConfig::tokenize(hotspot_rates_str);
  _hs_max_val = -1;
  for(int i = 0; i < hotspot_nodes.size(); ++i) {
    int rate = hotspot_rates.empty() ? 1 : atoi(hotspot_rates[i].c_str());
    _hs_elems.push_back(make_pair(rate, atoi(hotspot_nodes[i].c_str())));
    _hs_max_val += rate;
  }
  
  map<string, tTrafficFunction>::const_iterator match;

  string combined_patterns_str = config.GetStr("combined_patterns");
  vector<string> combined_patterns = BookSimConfig::tokenize(combined_patterns_str);
  string combined_rates_str = config.GetStr("combined_rates");
  vector<string> combined_rates = BookSimConfig::tokenize(combined_rates_str);
  _cp_max_val = -1;
  for(int i = 0; i < combined_patterns.size(); ++i) {
    match = gTrafficFunctionMap.find(combined_patterns[i]);
    if(match == gTrafficFunctionMap.end()) {
      cout << "Error: Undefined traffic pattern '" << combined_patterns[i] << "'." << endl;
      exit(-1);
    }
    int rate = combined_rates.empty() ? 1 : atoi(combined_rates[i].c_str());
    _cp_elems.push_back(make_pair(rate, match->second));
    _cp_max_val += rate;
  }

  tTrafficFunction tf;

  string fn = config.GetStr( "traffic", "none" );
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

