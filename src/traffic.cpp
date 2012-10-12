// $Id$

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
#include <set>
#include <map>
#include <cstdlib>

#include "booksim.hpp"
#include "booksim_config.hpp"
#include "traffic.hpp"
#include "network.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"

map<string, tTrafficFunction> gTrafficFunctionMap;

static int gResetTraffic = 0;
static int gStepTraffic  = 0;

static int _xr = 1;



//=============================================================

static int _hs_max_val;


static vector<pair<int, int> > _hs_elems;
map<int, vector<int> > gather_dest;
set<int> hs_lookup;
bool hs_send_all = false;
set<int> hs_senders;
int bystander_sender;
int bystander_receiver;

int rand_hotspot_src =0;
int rand_hotspot_dst =0;

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

  for ( int n = 0; n < gN; ++n ) {
    dest += offset *
      ( ( ( source / offset ) % (_xr*gK) + ( (_xr*gK)/2 - 1 ) ) % (_xr*gK) );
    offset *= (_xr*gK);
  }
  //cout<<source<<" "<<dest<<endl;
  return dest;
}

//=============================================================

int neighbor( int source, int total_nodes )
{
  int offset = 1;
  int dest = 0;

  for ( int n = 0; n < gN; ++n ) {
    dest += offset *
      ( ( ( source / offset ) % (_xr*gK) + 1 ) % (_xr*gK) );
    offset *= (_xr*gK);
  }

  //cout<<"Source "<<source<<" destination "<<dest<<endl;
  return dest;
}

//=============================================================
static int gPermSeed;

void GenerateRandomHotspot( int total_nodes, int num_src, int num_dst){

  //seed business
  unsigned long prev_rand = RandomIntLong( );
  RandomSeed(gPermSeed);
  //erase previous hotspots 
  hs_lookup.clear();
  hs_senders.clear();
  _hs_elems.clear();
  hs_send_all = false;

  //assign sources
  cout<<"Src:";
  while(hs_senders.size()<(size_t)num_src){
    hs_senders.insert(RandomInt(total_nodes-1));
  }
  for(set<int>::iterator i = hs_senders.begin(); i != hs_senders.end(); i++){
    cout<<*i<<"\t";
  }
  cout<<endl;
  //assign dests
  set<int> temp_dest;
  while(temp_dest.size()<(size_t)num_dst){
    int temp = RandomInt(total_nodes-1);
    if(hs_senders.count(temp)==0){
      temp_dest.insert(temp);
    }
  }

  //assign dest rate
  _hs_max_val = -1;
  cout<<"Dst:";
  for(set<int>::iterator i = temp_dest.begin(); i != temp_dest.end(); i++){
    //this is fixed at equal rate for all destiantion
    int rate = 1;
    _hs_elems.push_back(make_pair(rate,(*i)));
    _hs_max_val += rate;
    hs_lookup.insert((*i));

    cout<<*i<<"\t";
  }
  cout<<endl;

  RandomSeed( prev_rand );
}


void GenerateRandomGather( int total_nodes, int num_src, int num_dst, int num_split){

  //exact factors only
  assert(float(num_src)/float(num_dst)-int(num_src/num_dst)==0.0);



  //seed business
  unsigned long prev_rand = RandomIntLong( );
  RandomSeed(gPermSeed);
  //erase previous hotspots gather_set
  gather_dest.clear();
  hs_lookup.clear();
  hs_senders.clear();
  _hs_elems.clear();
  hs_send_all = false;

  //Generate soruces
  cout<<"Src:";
  while(hs_senders.size()<(size_t)num_src){
    hs_senders.insert(RandomInt(total_nodes-1));
  }
  for(set<int>::iterator i = hs_senders.begin(); i != hs_senders.end(); i++){
    gather_dest.insert(pair<int, vector<int> >(*i, vector<int>()));
    cout<<*i<<"\t";
  }
  cout<<endl;
  
  //Generate Dests
  set<int> temp_dest;
  while(temp_dest.size()<(size_t)num_dst){
    int temp = RandomInt(total_nodes-1);
    if(hs_senders.count(temp)==0){
      temp_dest.insert(temp);
    }
  }
  vector<int> dests_a;
  vector<int> dests_b;
  cout<<"Dst:";
  for(set<int>::iterator i = temp_dest.begin(); i != temp_dest.end(); i++){
    dests_a.push_back(*i);
    cout<<*i<<"\t";
  }
  cout<<endl;
  
  int max_factor = num_src*num_split/num_dst;
  cout<<"assign "<<max_factor<<" per dest\n";
  int dest_remain = dests_a.size();
  vector<int> assigned_a;assigned_a.resize(dests_a.size(),0);
  vector<int> assigned_b;
  vector<int>* dests = &dests_a;
  vector<int>* dests_backup = &dests_b;
  vector<int>* assigned = &assigned_a;
  vector<int>* assigned_backup = &assigned_b;
  map<int, vector<int> >::iterator source_iter = gather_dest.begin(); 
  int retries = 0; //too many retries we reset the array
  //assign sources to dest until it all runs out
  while(dest_remain && source_iter!=gather_dest.end()){
    while(source_iter->second.size()<num_split){
      int index = RandomInt(dest_remain-1);
      if((*assigned)[index]>=max_factor){//Full
	retries++;
	continue;
      }
      //if(source_iter->second.count((*dests)[index])==0){
	//insert occurs when dest is not full, and source does not have dest yet
	source_iter->second.push_back((*dests)[index]);
	(*assigned)[index]++;
	//}
    }
    //curret source is filled next
    source_iter++;
    
    //array reorg
    if(retries>10){
      for(size_t i = 0; i<dests->size(); i++){
	if((*assigned)[i]<max_factor){
	  dests_backup->push_back((*dests)[i]);
	  assigned_backup->push_back((*assigned)[i]);
	}
      }
      cout<<dests->size()-dests_backup->size()<<" filled\n";
      dests->clear();
      assigned->clear();
      //swap
      vector<int>* temp = dests;
      dests=  dests_backup;
      dests_backup = temp;
      temp = assigned;
      assigned = assigned_backup;
      assigned_backup = temp;
      //loop conditio
      dest_remain = dests->size();
      retries = 0;
    }
  }
  
  
  //sanity check
  cout<<"source -> dest check\n";
  map<int, int> check_map;
  for(map<int, vector<int> >::iterator i = gather_dest.begin(); 
      i!=gather_dest.end();
      i++){
    set<int> check_set;
    //cout<<i->first<<"\t"<<i->second.size()<<endl;
    for(vector<int>::iterator j=i->second.begin(); j!=i->second.end(); j++){
      check_set.insert(*j);
      if(check_map.count(*j)==0){
	check_map[*j]=1;
      } else {
	check_map[*j]+=1;
      }
    }
    cout<<i->first<<"\t"<<check_set.size()<<endl;
  }
  cout<<"dest -> source check\n";
  for(map<int, int>::iterator i = check_map.begin();
      i!=check_map.end(); 
      i++){
    cout<<i->first<<"\t"<<i->second<<endl;
  }


  cout<<"done gather generation"<<endl;

  RandomSeed( prev_rand );
}

static vector<int> gPerm;


void GenerateRandomPerm( int total_nodes )
{
  int ind;
  int i,j;
  int cnt;
  unsigned long prev_rand;
  
  prev_rand = RandomIntLong( );
  gPerm.resize(total_nodes);
  gPerm.assign(total_nodes, -1);

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
  if ( gResetTraffic || gPerm.empty() ) {
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

int badperm_dflynew( int source, int total_nodes )
{
  int grp_size_routers = gA;
  int grp_size_nodes = grp_size_routers * (gK);

  int temp;
  int dest;

  temp = (int) (source / grp_size_nodes);
  dest =  (RandomInt(grp_size_nodes - 1) + (temp+1)*grp_size_nodes ) %  total_nodes;
  return dest;
}
///each group of 4 groups send to another 4 groups such that
//all traffic froma group access the same router
int badprog_dflynew( int source, int total_nodes )
{
  int grp_size_routers = 2*(gK);
  int grp_size_nodes = grp_size_routers * (gK);

  int grp_id = source / grp_size_nodes;//current group
  int grp_grp= grp_id/gK;//current group's group
  int dest;

  
  if(grp_id==gG-1){ //fuck this extra gropu on the dragonfly
    //cout<<grp_id<<"\t"<<(gG-gK-1)<<endl;
    return (RandomInt(grp_size_nodes - 1) + (gG-gK-1)*grp_size_nodes ) %  total_nodes;
  }

  int dest_grp_base = 0;
  if(grp_grp==0){
    dest_grp_base=((gG/gK)+grp_grp-1)*gK+1;
  } else{
    dest_grp_base=((gG/gK+grp_grp)-1)%(gG/gK)*gK;
  }
  
  //cout<<grp_id<<"\t"<<dest_grp_base<<endl;
  dest =  (RandomInt(grp_size_nodes - 1) + (dest_grp_base+RandomInt(gK-1))*grp_size_nodes ) %  total_nodes;

  return dest;
}


int badhot_dflynew( int source, int total_nodes )
{
  //hotspot + bad dragonfly traffic
  int grp_size_routers = 2*(gK);
  int grp_size_nodes = grp_size_routers * (gK);
  int group;
  int dest;
  int hot_index = RandomInt(_hs_elems.size() - 1);
  group = (int) (source / grp_size_nodes);
  dest =  ((_hs_elems[hot_index].second)%grp_size_nodes + (group+1)*grp_size_nodes ) %  total_nodes;

  return dest;
}


int badperm_yarc(int source, int total_nodes){
  int row = (int)(source/(_xr*gK));
  
  return RandomInt((_xr*gK)-1)*(_xr*gK)+row;
}

int background_uniform(int source, int total_nodes){
  int e = RandomInt(total_nodes-1);
  while(hs_lookup.count(e)!=0){
    e = RandomInt(total_nodes-1);
  }
  return e;
}

int badhot_background_uniform(int source, int total_nodes){
  int e = RandomInt(total_nodes-1);
  int grp_size_routers = 2*(gK);
  int grp_size_nodes = grp_size_routers * (gK);
  while(hs_lookup.count(e%grp_size_nodes)){
    e = RandomInt(total_nodes-1);
  }
  return e;
}

int hotspot(int source, int total_nodes){
  if(!hs_send_all && hs_senders.count(source) == 0){
    return background_uniform( source,  total_nodes);
  } else {
    int pct = RandomInt(_hs_max_val);
    for(size_t i = 0; i < (_hs_elems.size()-1); ++i) {
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
}
int rand_gather(int source, int total_nodes){
  assert(gather_dest.count(source)!=0);
  int index = RandomInt(gather_dest[source].size()-1);
  return gather_dest[source][index];
}

int noself_hotspot(int source, int total_nodes){
  assert( hs_send_all || hs_senders.count(source)!=0);
  if(hs_lookup.count(source)!=0){//this should not trigger if inject = hotspot_test
    return background_uniform( source,  total_nodes);
  } else {
    return hotspot( source,  total_nodes);
  }
}

//this is a specialized traffic for congestion. A few hotspot sends
//and a bystander flow tries to utilize the same congested channel
int traffic_congestion_test(int source, int total_nodes){
  assert(source== bystander_sender || hs_senders.count(source)!=0);
  if(source == bystander_sender){
    return bystander_receiver;
  }else {
    return hotspot(source,total_nodes);
  }
}

//=============================================================

static int _cp_max_val;
static vector<pair<int, tTrafficFunction> > _cp_elems;

int combined(int source, int total_nodes){
  int pct = RandomInt(_cp_max_val);
  for(size_t i = 0; i < (_cp_elems.size()-1); ++i) {
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
void InitializeTrafficMap( const Configuration & config )
{

  _xr = config.GetInt("xr");


  gPermSeed = config.GetInt( "perm_seed" );


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
  gTrafficFunctionMap["badhot_dragon"]   = &badhot_dflynew;
  gTrafficFunctionMap["badprog_dragon"]   = &badprog_dflynew;
  gTrafficFunctionMap["badperm_yarc"] = &badperm_yarc;

  gTrafficFunctionMap["hotspot"]  = &hotspot;
  gTrafficFunctionMap["noself_hotspot"]  = &noself_hotspot;
  gTrafficFunctionMap["rand_noself_hotspot"]  = &noself_hotspot;
  gTrafficFunctionMap["rand_gather"]  = &rand_gather;

  gTrafficFunctionMap["combined"] = &combined;
  gTrafficFunctionMap["background_uniform"] = &background_uniform;
  gTrafficFunctionMap["badhot_background_uniform"] = &badhot_background_uniform;

  gTrafficFunctionMap["congestion_test"] = &traffic_congestion_test;

 
  bystander_sender = config.GetInt("bystander_sender");
  bystander_receiver = config.GetInt("bystander_receiver");
  


  vector<int> hotspot_nodes = config.GetIntArray("hotspot_nodes");
  vector<int> hotspot_rates = config.GetIntArray("hotspot_rates");
  vector<int> hotspot_senders = config.GetIntArray("hotspot_senders");
  if(hotspot_senders.empty()){
    hs_send_all = true;
  } else {
    for(size_t i = 0; i<hotspot_senders.size(); i++){
      hs_senders.insert(hotspot_senders[i]);
    }
  }
  hotspot_rates.resize(hotspot_nodes.size(), hotspot_rates.empty() ? 1 : hotspot_rates.back());
  _hs_max_val = -1;
  for(size_t i = 0; i < hotspot_nodes.size(); ++i) {
    int rate = hotspot_rates[i];
    _hs_elems.push_back(make_pair(rate, hotspot_nodes[i]));
    _hs_max_val += rate;
    hs_lookup.insert(hotspot_nodes[i]);
    cout<<(hotspot_nodes[i])<<endl;
  }
  //random hotspot is handled by trafficmanager

  //combined 
  map<string, tTrafficFunction>::const_iterator match;
  vector<string> combined_patterns = config.GetStrArray("combined_patterns");
  vector<int> combined_rates = config.GetIntArray("combined_rates");
  combined_rates.resize(combined_patterns.size(), combined_rates.empty() ? 1 : combined_rates.back());
  _cp_max_val = -1;
  for(size_t i = 0; i < combined_patterns.size(); ++i) {
    match = gTrafficFunctionMap.find(combined_patterns[i]);
    if(match == gTrafficFunctionMap.end()) {
      cout << "Error: Undefined traffic pattern '" << combined_patterns[i] << "'." << endl;
      exit(-1);
    }
    int rate = combined_rates[i];
    _cp_elems.push_back(make_pair(rate, match->second));
    _cp_max_val += rate;
  }


}

void ResetTrafficFunction( )
{
  gResetTraffic++;
}

void StepTrafficFunction( )
{
  gStepTraffic++;
}
