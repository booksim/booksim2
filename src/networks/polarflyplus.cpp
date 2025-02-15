/*
  Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
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
#include <vector>
#include <sstream>

#include "polarflyplus.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "globals.hpp"
#include "polarfly_tables.hpp"

#define POLAR_LATENCY 
#define Polarflysize POLARFLY_TABLE_ROWS

int gP_polar, gA_polar, gG_polar;
bool fault_table[total_node][node_port] = {false};


//Hypercube : Local (group)
//Polarfly  : Global
vector<vector<string>> dbg;

//calculate the hop count between src and destination
int polarflyplusnew_hopcnt(int src, int dest) 
{
  int hopcnt;
  int dest_grp_ID, src_grp_ID; 
  int src_hopcnt, dest_hopcnt;
  int src_intm, dest_intm;
  int grp_output, dest_grp_output;
  int grp_output_RID;

  int _grp_num_routers= gA_polar;
  int _grp_num_nodes =_grp_num_routers*gP_polar;
  
  dest_grp_ID = int(dest/_grp_num_nodes);
  src_grp_ID = int(src / _grp_num_nodes);
  
  //source and dest are in the same group, either 0-1 hop
  if (dest_grp_ID == src_grp_ID) {
    if ((int)(dest / gP_polar) == (int)(src /gP_polar))
      hopcnt = 0;
    else
      hopcnt = 1;
    
  } else {
    //source and dest are in the same group
    //find the number of hops in the source group
    //find the number of hops in the dest group
    if (src_grp_ID > dest_grp_ID)  {
      grp_output = dest_grp_ID;
      dest_grp_output = src_grp_ID - 1;
    }
    else {
      grp_output = dest_grp_ID - 1;
      dest_grp_output = src_grp_ID;
    }
    grp_output_RID = ((int) (grp_output / (gP_polar))) + src_grp_ID * _grp_num_routers;
    src_intm = grp_output_RID * gP_polar;

    grp_output_RID = ((int) (dest_grp_output / (gP_polar))) + dest_grp_ID * _grp_num_routers;
    dest_intm = grp_output_RID * gP_polar;

    //hop count in source group
    if ((int)( src_intm / gP_polar) == (int)( src / gP_polar ) )
      src_hopcnt = 0;
    else
      src_hopcnt = 1; 

    //hop count in destination group
    if ((int)( dest_intm / gP_polar) == (int)( dest / gP_polar ) ){
      dest_hopcnt = 0;
    }else{
      dest_hopcnt = 1;
    }

    //tally
    hopcnt = src_hopcnt + 1 + dest_hopcnt;
  }

  return hopcnt;  
}


PolarFlyplusNew::PolarFlyplusNew( const Configuration &config, const string & name ) :
  Network( config, name )
{

  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}

void PolarFlyplusNew::_ComputeSize( const Configuration &config )
{

  // LIMITATION
  // _n == # of dimensions within a group
  // _p == # of processors within a router
  //_p = config.GetInt( "k" );// # of nodes in each switch=1
  //_n = config.GetInt( "n" );
  int Hypercubeport = config.GetInt( "k" );
  int Polarflyport = config.GetInt( "n" );

  _p=1;
  _n=1;
  assert(_n==1);

  _k = Polarflyport + Hypercubeport + 1; // Polarfly + Hyoercube+  CPU  

  // FIX...
  gK = _p; gN = _n;

  //group : Hypercube
  _a = powi(2,Hypercubeport);
  _g = Polarflysize; 
  _nodes   = _a * _p * _g;
 
  _num_of_switch = _nodes / _p;
  _channels = _num_of_switch * (Polarflyport + Hypercubeport); 
  _size = _num_of_switch;
  
  gG_polar = _g;
  gP_polar = _p;
  gA_polar = _a;
  _grp_num_routers = gA_polar;
  _grp_num_nodes =_grp_num_routers*gP_polar;

}

void PolarFlyplusNew::_BuildNet( const Configuration &config )
{

  int _output=-1;
  int _input=-1;
  //int _dim_ID=-1;
  //int _num_ports_per_switch=Polarflyport+Hypercubeport+1;
  int c;
  int Hypercubeport = config.GetInt( "k" );
  int Polarflyport = config.GetInt( "n" );
  ostringstream router_name;

  cout << " Polarflyplus " << endl;
  cout << " p = " << _p << " n = " << _n << endl;
  cout << " each switch - total radix =  "<< _k << endl;
  cout << " # of switches = "<<  _num_of_switch << endl;
  cout << " # of channels = "<<  _channels << endl;
  cout << " # of nodes ( size of network ) = " << _nodes << endl;
  cout << " # of groups (_g) = " << _g << endl;
  cout << " # of routers per group (_a) = " << _a << endl;
  int ch_count=0;
  for ( int node = 0; node < _num_of_switch; ++node ) {
    // ID of the group
    int grp_ID;
    grp_ID = (int) (node/_a);
    router_name << "router";
    
    //router_name << "_" <<  node ;
    for(int i = 0 ; i < Hypercubeport; i++){
     router_name << "_" << ((node >> i)%2) ;
    }
    router_name << "_" << (node >> Hypercubeport);
    //cout << router_name.str( ) << endl;
    _routers[node] = Router::NewRouter( config, this, router_name.str(), 
					node, _k, _k );
    _timed_modules.push_back(_routers[node]);
    router_name.str("");

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      c = _p * node +  cnt;
      _routers[node]->AddInputChannel( _inject[c], _inject_cred[c] );
    }

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      c = _p * node +  cnt;
      _routers[node]->AddOutputChannel( _eject[c], _eject_cred[c] );
    }

    if (_n > 1 )  { cout << " ERROR: n>1 dimension NOT supported yet... " << endl; exit(-1); }

    //********************************************
    //   connect OUTPUT channels
    //********************************************
    // add hypercube output channel
    //
    //_chan[output] : {{src hypercube ports}, {src polarfly ports}}
    
      for ( int cnt = 0; cnt < Hypercubeport; ++cnt ) {
	_output = (Polarflyport+Hypercubeport) * node + cnt;
        dbg.push_back({ to_string(_output), "Hypercube","node"+to_string(node)+"-port"+to_string(cnt) });
	_routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );

#ifdef POLAR_LATENCY
	_chan[_output]->SetLatency(1);
	_chan_cred[_output]->SetLatency(1);
#endif
      }
    //add polarfly output channel
    for ( int cnt = 0; cnt < Polarflyport; ++cnt ) {
      _output = (Polarflyport+Hypercubeport) * node + Hypercubeport + cnt;
      //_chan[_output].global = true;
      //if(grp_ID < Polarflyport  && cnt==Polarflyport-1) continue; //red group : no self-connection
      dbg.push_back({ to_string(_output), "Polarfly","node"+to_string(node)+"-port"+to_string(cnt+Hypercubeport) });
      _routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );
#ifdef POLAR_LATENCY
      _chan[_output]->SetLatency(1);
      _chan_cred[_output]->SetLatency(1);
#endif
    }

    //********************************************
    //   connect INPUT channels
    //********************************************
    //_chan[input] :  {{dest hypercube ports}, {dest polarfly ports}}
    //_num_ports_per_switch = Polarflyport + Hypercubeport;
   
    //hypercube calculation
    for ( int cnt = 0; cnt < Hypercubeport; ++cnt ) {
	_input = (node ^ (1<<cnt)) * (Polarflyport+Hypercubeport) + cnt;
       dbg[ch_count].push_back (to_string(_input)+" node"+to_string(node ^ (1<<cnt))+"-port"+to_string(cnt));
       ch_count++;
	_routers[node]->AddInputChannel( _chan[_input], _chan_cred[_input] );
      }
    //Polarfly  table refer
    int dest_polarport;
    for ( int cnt = 0; cnt < Polarflyport; ++cnt ) {
      for(int i = 0 ; i < 8; i++){
          if (polarfly_connection_table[polarfly_connection_table[grp_ID][cnt]][i]==grp_ID){
                dest_polarport=i+Hypercubeport;
		break;
	  }
      }
      int hyperadd = node%(powi(2,Hypercubeport));
      int dest_node_add = polarfly_connection_table[grp_ID][cnt]*powi(2,Hypercubeport)+hyperadd;
      //cout << hyperadd << " " << polarfly_connection_table[grp_ID][cnt] << endl; 
      _input = dest_node_add * (Polarflyport+Hypercubeport) + dest_polarport;
      //if(grp_ID < Polarflyport && cnt== Polarflyport-1) continue; //red group : no self-connection
       dbg[ch_count].push_back (to_string(_input)+" node"+to_string(dest_node_add)+"-port"+to_string(dest_polarport));
       ch_count++;
      _routers[node]->AddInputChannel( _chan[_input], _chan_cred[_input] );
    }

  }

  for(int i = 0; i < ch_count; i++){
     for(int j = 0 ; j < 4; j++){
         cout << dbg[i][j] << " ";
     } cout << endl;
  }
  cout<<"Done links"<<endl;
}

void PolarFlyplusNew::InsertRandomFaults( const Configuration &config )
{
  int num_fails = config.GetInt( "link_failures" );
  int Hypercubeport = config.GetInt( "k" );
  int Polarflyport = config.GetInt( "n" );
  if ( _size && num_fails ) {
    vector<long> save_x;
    vector<double> save_u;
    SaveRandomState( save_x, save_u );
    int fail_seed;
    if ( config.GetStr( "fail_seed" ) == "time" ) {
      fail_seed = int( time( NULL ) );
      cout << "SEED: fail_seed=" << fail_seed << endl;
    } else {
      fail_seed = config.GetInt( "fail_seed" );
    }
    RandomSeed( fail_seed );

    vector<bool> fail_nodes(_size);

    for ( int i = 0; i < num_fails; i++ ) {
      int node = RandomInt( _size - 1 );
      int chan = RandomInt( _size * ( Hypercubeport + Polarflyport ) ) % ( Hypercubeport + Polarflyport ) + 1;
      OutChannelFault( node, chan );
      fault_table[node][chan]=true;
      cout << "failure at node" << node << " port" << chan << endl;
    }
    RestoreRandomState( save_x, save_u );
  }
  for(int i = 0 ; i < 7*(1<<Hypercubeport); i++){
      cout << "fault table: node" << i << " " ;
      for(int j = 0 ; j < Polarflyport+Hypercubeport+1 ; j++){
        if(!fault_table[i][j]){cout << "O";}
        else {cout << "X";}
      }cout << endl;
   }
}

double PolarFlyplusNew::Capacity( ) const
{
  return (double)_k / 8.0;
}

void PolarFlyplusNew::RegisterRoutingFunctions(){

}
