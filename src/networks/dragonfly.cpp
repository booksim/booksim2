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

#include "booksim.hpp"
#include <vector>
#include <sstream>

#include "dragonfly.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "globals.hpp"


int gP, gA, gG;

double roc_threshold = 0.000;
int gVersion_of_ugal;

#define MAX(X,Y) ((X>Y)?(X):(Y))

//calculate the hop count between src and estination
int dragonflynew_hopcnt(int src, int dest) 
{
  int hopcnt;
  int dest_grp_ID, src_grp_ID; 
  int src_hopcnt, dest_hopcnt;
  int src_intm, dest_intm;
  int grp_output, dest_grp_output;
  int grp_output_RID;

  int _grp_num_routers= gA;
  int _grp_num_nodes =_grp_num_routers*gP;
  
  dest_grp_ID = int(dest/_grp_num_nodes);
  src_grp_ID = int(src / _grp_num_nodes);
  
  //source and dest are in the same group, either 0-1 hop
  if (dest_grp_ID == src_grp_ID) {
    if ((int)(dest / gP) == (int)(src /gP))
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
    grp_output_RID = ((int) (grp_output / (gP))) + src_grp_ID * _grp_num_routers;
    src_intm = grp_output_RID * gP;

    grp_output_RID = ((int) (dest_grp_output / (gP))) + dest_grp_ID * _grp_num_routers;
    dest_intm = grp_output_RID * gP;

    //hop count in source group
    if ((int)( src_intm / gP) == (int)( src / gP ) )
      src_hopcnt = 0;
    else
      src_hopcnt = 1; 

    //hop count in destination group
    if ((int)( dest_intm / gP) == (int)( dest / gP ) ){
      dest_hopcnt = 0;
    }else{
      dest_hopcnt = 1;
    }

    //tally
    hopcnt = src_hopcnt + 1 + dest_hopcnt;
  }

  return hopcnt;  
}


//packet output port based on the source, destination and current location
int dragonfly_port(int rID, int source, int dest){
  int _grp_num_routers= gA;
  int _grp_num_nodes =_grp_num_routers*gP;

  int out_port = -1;
  int grp_ID = int(rID / _grp_num_routers); 
  int dest_grp_ID = int(dest/_grp_num_nodes);
  int grp_output=-1;
  int grp_RID=-1;
  int group_dest=-1;
  
  //which router within this group the packet needs to go to
  if (dest_grp_ID == grp_ID) {
    grp_RID = int(dest / gP);
  } else {
    if (grp_ID > dest_grp_ID) {
      grp_output = dest_grp_ID;
    } else {
      grp_output = dest_grp_ID - 1;
    }
    grp_RID = int(grp_output /gP) + grp_ID * _grp_num_routers;
    group_dest = grp_RID * gP;
  }

  //At the last hop
  if (dest >= rID*gP && dest < (rID+1)*gP) {    
    out_port = dest%gP;
  } else if (grp_RID == rID) {
    //At the optical link
    out_port = gP + (gA-1) + grp_output %(gP);
  } else {
    //need to route within a group
    assert(grp_RID!=-1);

    if (rID < grp_RID){
      out_port = (grp_RID % _grp_num_routers) - 1 + gP;
    }else{
      out_port = (grp_RID % _grp_num_routers) + gP;
    }
  }  
 
  assert(out_port!=-1);
  return out_port;
}


DragonFlyNew::DragonFlyNew( const Configuration &config, const string & name ) :
  Network( config, name )
{

  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}

void DragonFlyNew::_ComputeSize( const Configuration &config )
{

  // LIMITATION
  //  -- only one dimension between the group
  // _n == # of dimensions within a group
  // _p == # of processors within a router
  // inter-group ports : _p
  // terminal ports : _p
  // intra-group ports : 2*_p - 1
  _p = config.GetInt( "k" );	// # of ports in each switch
  _n = config.GetInt( "n" );
  
  _global_channel_latency=config.GetInt("dragonfly_global_latency");
  _local_channel_latency=config.GetInt("dragonfly_local_latency");
  assert(_global_channel_latency>0 &&
	 _local_channel_latency>0);
  
  gVersion_of_ugal = config.GetInt( "ugal_version" );
 

  assert(_n==1);
  // dimension

  if (_n == 1)
    _k = _p + _p + 2*_p  - 1;
  else
    _k = _p + _p + 2*_p;

  
  // FIX...
  gK = _p; gN = _n;

  // with 1 dimension, total of 2p routers per group
  // N = 2p * p * (2p^2 + 1)
  // a = # of routers per group
  //   = 2p (if n = 1)
  //   = p^(n) (if n > 2)
  //  g = # of groups
  //    = a * p + 1
  // N = a * p * g;
  
  if (_n == 1)
    _a = 2 * _p;
  else
    _a = powi(_p, _n);

  _g = _a * _p + 1;
  _nodes   = _a * _p * _g;

  _num_of_switch = _nodes / _p;
  _channels = _num_of_switch * (_k - _p); 
  _size = _num_of_switch;


  
  gG = _g;
  gP = _p;
  gA = _a;
  _grp_num_routers = gA;
  _grp_num_nodes =_grp_num_routers*gP;

}

void DragonFlyNew::_BuildNet( const Configuration &config )
{

  int _output;
  int _input;
  int c;
  int _dim_ID;
  int _num_ports_per_switch;
  int _dim_size;

  ostringstream router_name;



  cout << " Dragonfly " << endl;
  cout << " p = " << _p << " n = " << _n << endl;
  cout << " each switch - total radix =  "<< _k << endl;
  cout << " # of switches = "<<  _num_of_switch << endl;
  cout << " # of channels = "<<  _channels << endl;
  cout << " # of nodes ( size of network ) = " << _nodes << endl;
  cout << " # of groups (_g) = " << _g << endl;
  cout << " # of routers per group (_a) = " << _a << endl;

  for ( int node = 0; node < _num_of_switch; ++node ) {
    // ID of the group
    int grp_ID;
    grp_ID = (int) (node/_a);
    router_name << "router";
    
    router_name << "_" <<  node ;

    _routers[node] = Router::NewRouter( config, this, router_name.str( ), 
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

    // add OUPUT channels
    // _k == # of processor per router
    //  need 2*_k routers  --thus, 
    //  2_k-1 outputs channels within group
    //  _k-1 outputs for intra-group

    //

    if (_n > 1 )  { cout << " ERROR: n>1 dimension NOT supported yet... " << endl; exit(-1); }

    //********************************************
    //   connect OUTPUT channels
    //********************************************
    // add intra-group output channel
    for ( int dim = 0; dim < _n; ++dim ) {
      for ( int cnt = 0; cnt < (2*_p -1); ++cnt ) {
	_output = (2*_p-1 + _p) * _n  * node + (2*_p-1) * dim  + cnt;

	_routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );


	_chan[_output]->SetLatency(_local_channel_latency);
	_chan_cred[_output]->SetLatency(_local_channel_latency);

      }
    }

    // add inter-group output channel

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      _output = (2*_p-1 + _p) * node + (2*_p - 1) + cnt;

      //      _chan[_output].global = true;
      _routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );

      _chan[_output]->SetLatency(_global_channel_latency);
      _chan_cred[_output]->SetLatency(_global_channel_latency);

    }


    //********************************************
    //   connect INPUT channels
    //********************************************
    // # of non-local nodes 
    _num_ports_per_switch = (_k - _p);


    // intra-group GROUP channels
    for ( int dim = 0; dim < _n; ++dim ) {

      _dim_size = powi(_k,dim);

      _dim_ID = ((int) (node / ( powi(_p, dim))));



      // NODE ID withing group
      _dim_ID = node % _a;




      for ( int cnt = 0; cnt < (2*_p-1); ++cnt ) {

	if ( cnt < _dim_ID)  {

	  _input = 	grp_ID  * _num_ports_per_switch * _a - 
	    (_dim_ID - cnt) *  _num_ports_per_switch + 
	    _dim_ID * _num_ports_per_switch + 
	    (_dim_ID - 1);
	}
	else {

	  _input =  grp_ID * _num_ports_per_switch * _a + 
	    _dim_ID * _num_ports_per_switch + 
	    (cnt - _dim_ID + 1) * _num_ports_per_switch + 
	    _dim_ID;
			
	}

	if (_input < 0) {
	  cout << " ERROR: _input less than zero " << endl;
	  exit(-1);
	}


	_routers[node]->AddInputChannel( _chan[_input], _chan_cred[_input] );
      }
    }


    // add INPUT channels -- "optical" channels connecting the groups
    int _grp_num_routers;
    int grp_output;
    int grp_ID2;

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      //	   _dim_ID
      grp_output = _dim_ID* _p + cnt;

      _grp_num_routers = powi(_k, _n-1);
      grp_ID2 = (int) ((grp_ID - 1) / (_k - 1));

      if ( grp_ID > grp_output)   {

	_input = (grp_output) * _num_ports_per_switch * _a    +   		// starting point of group
	  (_num_ports_per_switch - _p) * (int) ((grp_ID - 1) / _p) +      // find the correct router within grp
	  (_num_ports_per_switch - _p) + 					// add offset within router
	  grp_ID - 1;	
      } else {

	_input = (grp_output + 1) * _num_ports_per_switch * _a    + 
	  (_num_ports_per_switch - _p) * (int) ((grp_ID) / _p) +      // find the correct router within grp
	  (_num_ports_per_switch - _p) +
	  grp_ID;	
      }

      _routers[node]->AddInputChannel( _chan[_input], _chan_cred[_input] );
    }

  }

  cout<<"Done links"<<endl;
}


int DragonFlyNew::GetN( ) const
{
  return _n;
}

int DragonFlyNew::GetK( ) const
{
  return _k;
}

void DragonFlyNew::InsertRandomFaults( const Configuration &config )
{
 
}

double DragonFlyNew::Capacity( ) const
{
  return (double)_k / 8.0;
}

void DragonFlyNew::RegisterRoutingFunctions(){

  gRoutingFunctionMap["min_dragonflynew"] = &min_dragonflynew;
  gRoutingFunctionMap["val_dragonflynew"] = &val_dragonflynew;
  gRoutingFunctionMap["ugal_dragonflynew"] = &ugal_dragonflynew;
  gRoutingFunctionMap["ugalprog_dragonflynew"] = &ugalprog_dragonflynew;
}


void min_dragonflynew( const Router *r, const Flit *f, int in_channel, 
		       OutputSet *outputs, bool inject )
{
  outputs->Clear( );

  if(inject) {
    int inject_vc= RandomInt(gNumVCs-1);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }

  int _grp_num_routers= gA;

  int dest  = f->dest;
  int rID =  r->GetID(); 

  int grp_ID = int(rID / _grp_num_routers); 
  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;
  int dest_grp_ID=-1;

  if ( in_channel < gP ) {
    out_vc = 0;
    f->ph = 0;
    if (dest_grp_ID == grp_ID) {
      f->ph = 1;
    }
  } 


  out_port = dragonfly_port(rID, f->src, dest);

  //optical dateline
  if (out_port >=gP + (gA-1)) {
    f->ph = 1;
  }  
  
  out_vc = f->ph;
  if (debug)
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
	       << "	through output port : " << out_port 
	       << " out vc: " << out_vc << endl;
  outputs->AddRange( out_port, out_vc, out_vc );
}



void val_dragonflynew( const Router *r, const Flit *f, int in_channel, 
			OutputSet *outputs, bool inject )
{
  assert(gNumVCs==4);
  outputs->Clear( );
  if(inject) {
    int inject_vc= RandomInt(gNumVCs-1);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }

  int _grp_num_routers= gA;
  int _grp_num_nodes =_grp_num_routers*gP;
  int _network_size =  gA * gP * gG;

 
  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = (int) (rID / _grp_num_routers);
  int dest_grp_ID = int(dest/_grp_num_nodes);

  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;
  int intm_grp_ID;
  int intm_rID;

  if(debug){
    cout<<"At router "<<rID<<endl;
  }
  if ( in_channel < gP )   {
    //dest are in the same group
    if (dest_grp_ID == grp_ID) {
      f->ph = 2;
      f->minimal = 1;
    } else {
      //select a random node
      f->intm =RandomInt(_network_size - 1);
      intm_grp_ID = (int)(f->intm/_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }
      //intermediate are in the same group
      if(grp_ID == intm_grp_ID){
	f->ph = 1;
	f->minimal = 1;
      } else { 
	//use valiant
	f->ph = 0;
	f->minimal = 0;
	
      }
    }
  }

  //transition from nonminimal phase to minimal
  if(f->ph==0 || f->ph==3){
    intm_rID= (int)(f->intm/gP);
    intm_grp_ID = (int)(f->intm/_grp_num_nodes);
    if( rID == intm_rID){
      f->ph = 1;
    }

  }
  //port assignement based on the phase
  if(f->ph == 0 || f->ph==3){
    assert(f->minimal!=1);
    out_port = dragonfly_port(rID, f->src, f->intm);
  } else if(f->ph == 1){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else if(f->ph == 2){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if (f->ph == 1 && out_port >=gP + (gA-1)) {
    f->ph = 2;
  }  

 //optical dateline
  if (f->ph == 0 && out_port >=gP + (gA-1)) {
    f->ph = 3;
  }  
 
  out_vc = f->ph;
  outputs->AddRange( out_port, out_vc, out_vc );
}


//Basic adaptive routign algorithm for the dragonfly
void ugal_dragonflynew( const Router *r, const Flit *f, int in_channel, 
			OutputSet *outputs, bool inject )
{
  //need 4 VCs for deadlock freedom

  assert(gNumVCs==4);
  outputs->Clear( );
  if(inject) {
    int inject_vc= RandomInt(gNumVCs-1);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }
  
  //this constant biases the adaptive decision toward minimum routing
  //negative value woudl biases it towards nonminimum routing
  int adaptive_threshold = 30;

  int _grp_num_routers= gA;
  int _grp_num_nodes =_grp_num_routers*gP;
  int _network_size =  gA * gP * gG;

 
  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = (int) (rID / _grp_num_routers);
  int dest_grp_ID = int(dest/_grp_num_nodes);

  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;
  int min_queue_size, min_hopcnt;
  int nonmin_queue_size, nonmin_hopcnt;
  double min_roc;
  double nonmin_roc;
  int intm_grp_ID;
  int intm_rID;

  if(debug){
    cout<<"At router "<<rID<<endl;
  }
  int min_router_output, nonmin_router_output;
  
  //at the source router, make the adaptive routing decision
  if ( in_channel < gP )   {
    //dest are in the same group, only use minimum routing
    if (dest_grp_ID == grp_ID) {
      f->ph = 2;
      f->minimal = 1;
    } else {
      //select a random node
      f->intm =RandomInt(_network_size - 1);
      intm_grp_ID = (int)(f->intm/_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }
      
      //random intermediate are in the same group, use minimum routing
      if(grp_ID == intm_grp_ID){
	f->ph = 1;
	f->minimal = 1;
      } else {

	min_hopcnt = dragonflynew_hopcnt(f->src, f->dest);
	min_router_output = dragonfly_port(rID, f->src, f->dest); 
	nonmin_hopcnt = dragonflynew_hopcnt(f->src, f->intm) +
	  dragonflynew_hopcnt(f->intm,f->dest);
	nonmin_router_output = dragonfly_port(rID, f->src, f->intm);

	//min and non-min output port could be identical, need to distinquish them
	if(nonmin_router_output == min_router_output){
	  min_queue_size = MAX(r->GetUsedCredit(min_router_output,1,1),0) ; 
	  min_roc = r->GetROC(min_router_output,1,1);
	  nonmin_queue_size = MAX(r->GetUsedCredit(nonmin_router_output,0,0),0);
	  nonmin_roc = r->GetROC(nonmin_router_output,0,0);	  
	} else {	  
	  min_queue_size = MAX(r->GetUsedCredit(min_router_output),0) ; 
	  min_roc = r->GetROC(min_router_output);
	  nonmin_queue_size = MAX(r->GetUsedCredit(nonmin_router_output),0);
	  nonmin_roc = r->GetROC(nonmin_router_output);
	}


	switch(gVersion_of_ugal){
	case 0:

	  if ((1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold ) {	  
	    if (debug)  cout << " MINIMAL routing " << endl;
	    f->ph = 1;
	    f->minimal = 1;
	  } else {
	    f->ph = 0;
	    f->minimal = 0;
	  }
	  break;
	case 1:
	  
	  if (min_roc<= 0 || min_roc <= 2*nonmin_roc) {	  
	    if (debug)  cout << " MINIMAL routing " << endl;
	    f->ph = 1;
	    f->minimal = 1;
	  } else {
	    f->ph = 0;
	    f->minimal = 0;
	  }
	  break;
	case 2:
	  if (min_roc<= 0 || ((1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold )) {	  
	    if (debug)  cout << " MINIMAL routing " << endl;
	    f->ph = 1;
	    f->minimal = 1;
	  } else {
	    f->ph = 0;
	    f->minimal = 0;
	  }
	  break;
	default:
	  assert(false);
	  break;
	}
      }
    }
  }
  
  //transition from nonminimal phase to minimal
  if(f->ph==0  || f->ph==3){
    intm_rID= (int)(f->intm/gP);
    if( rID == intm_rID){
      f->ph = 1;
    }
  }

  //port assignement based on the phase
  if(f->ph == 0|| f->ph==3){
    assert(f->minimal!=1);
    out_port = dragonfly_port(rID, f->src, f->intm);
  } else if(f->ph == 1){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else if(f->ph == 2){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if (f->ph == 1 && out_port >=gP + (gA-1)) {
    f->ph = 2;
  }  
  //optical dateline
  if (f->ph == 0 && out_port >=gP + (gA-1)) {
    f->ph = 3;
  }  

  //vc assignemnt based on phase
  out_vc = f->ph;

  outputs->AddRange( out_port, out_vc, out_vc );
}



void ugalprog_dragonflynew( const Router *r, const Flit *f, int in_channel, 
			OutputSet *outputs, bool inject )
{
  assert(gNumVCs==5);
  outputs->Clear( );
  if(inject) {
    int inject_vc= RandomInt(gNumVCs-1);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }

  int adaptive_threshold = 3;
  int _grp_num_routers= gA;
  int _grp_num_nodes =_grp_num_routers*gP;
  int _network_size =  gA * gP * gG;

 
  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = (int) (rID / _grp_num_routers);
  int dest_grp_ID = int(dest/_grp_num_nodes);

  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;
  int min_queue_size, min_hopcnt;
  int nonmin_queue_size, nonmin_hopcnt;
  double min_roc;
  double nonmin_roc;
  int intm_grp_ID;
  int intm_rID;

  if(debug){
    cout<<"At router "<<rID<<endl;
  }
  int min_router_output, nonmin_router_output;
  
  if ( in_channel < gP )   {
    //dest are in the same group
    if (dest_grp_ID == grp_ID) {
      f->ph = 2;
      f->minimal = 1;
    } else {
      //select a random node
      f->intm =RandomInt(_network_size - 1);
      intm_grp_ID = (int)(f->intm/_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }
      //intermediate are in the same group
      if(grp_ID == intm_grp_ID){
	f->ph = 1;
	f->minimal = 1;
      } else {

	min_hopcnt = dragonflynew_hopcnt(f->src, f->dest);
	min_router_output = dragonfly_port(rID, f->src, f->dest); 
	nonmin_hopcnt = dragonflynew_hopcnt(f->src, f->intm) +
	  dragonflynew_hopcnt(f->intm,f->dest);
	nonmin_router_output = dragonfly_port(rID, f->src, f->intm);

	//min and non-min output port could be identical, need to distinquish them
	if(nonmin_router_output == min_router_output){
	  min_queue_size = MAX(r->GetUsedCredit(min_router_output,1,1),0) ; 
	  min_roc = r->GetROC(min_router_output,1,1);
	  nonmin_queue_size = MAX(r->GetUsedCredit(nonmin_router_output,0,0),0);
	  nonmin_roc = r->GetROC(nonmin_router_output,0,0);	  
	} else {	  
	  min_queue_size = MAX(r->GetUsedCredit(min_router_output,1,1),0) ; 
	  min_roc = r->GetROC(min_router_output);
	  nonmin_queue_size = MAX(r->GetUsedCredit(nonmin_router_output,0,0),0);
	  nonmin_roc = r->GetROC(nonmin_router_output);
	}


	switch(gVersion_of_ugal){
	case 0:

	  if ((1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold ) {	  
	    if (debug)  cout << " MINIMAL routing " << endl;
	    f->ph = 4;
	    f->minimal = 1;
	  } else {
	    f->ph = 0;
	    f->minimal = 0;
	  }
	  break;
	case 1:
	  
	  if (min_roc<= 0 || min_roc <= 2*nonmin_roc) {	  
	    if (debug)  cout << " MINIMAL routing " << endl;
	    f->ph = 4;
	    f->minimal = 1;
	  } else {
	    f->ph = 0;
	    f->minimal = 0;
	  }
	  break;
	case 2:
	  if (min_roc<= 0 || ((1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold )) {	  
	    if (debug)  cout << " MINIMAL routing " << endl;
	    f->ph = 4;
	    f->minimal = 1;
	  } else {
	    f->ph = 0;
	    f->minimal = 0;
	  }
	  break;
	default:
	  assert(false);
	  break;
	}
      }
    }
  } else if(f->ph == 4 && f->minimal==1){ //progressive
    assert(in_channel<gP + gA-1);
    //select a random node
    f->intm =RandomInt(_network_size - 1);
    intm_grp_ID = (int)(f->intm/_grp_num_nodes);
    if (debug){
      cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
    }
    //intermediate are in the same group
    if(grp_ID == intm_grp_ID){
    } else {
      //congestion metric using queue length
      min_hopcnt = dragonflynew_hopcnt(f->src, f->dest);
      min_router_output = dragonfly_port(rID, f->src, f->dest); 
      min_queue_size = MAX(r->GetUsedCredit(min_router_output) ,0); 
      min_roc = r->GetROC(min_router_output);
      
      nonmin_hopcnt = dragonflynew_hopcnt(f->src, f->intm) +
	dragonflynew_hopcnt(f->intm,f->dest);
      nonmin_router_output = dragonfly_port(rID, f->src, f->intm);
      nonmin_queue_size = MAX(r->GetUsedCredit(nonmin_router_output),0);
      nonmin_roc = r->GetROC(nonmin_router_output);

      switch(gVersion_of_ugal){
      case 0:

	if ((1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold ) {
	} else {
	  f->ph = 0;
	  f->minimal = 2;
	}
	break;
      case 1:
	  
	if (min_roc<= 0 || min_roc <= 2*nonmin_roc) {	  
	} else {
	  f->ph = 0;
	  f->minimal = 2;
	}
	break;
      case 2:
	if (min_roc<= 0 || ((1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold )) {	  
	} else {

	  f->ph = 0;
	  f->minimal = 2;
	}
	break;
      default:
	assert(false);
	break;
      }
    }    
  }

  //transition from nonminimal phase to minimal
  if(f->ph==0 || f->ph == 3){
    intm_rID= (int)(f->intm/gP);
    if( rID == intm_rID){
      f->ph = 1;
    }
  }


  //port assignement based on the phase
  if(f->ph == 0 || f->ph == 3){
    assert(f->minimal!=1);
    out_port = dragonfly_port(rID, f->src, f->intm);
  } else if(f->ph == 1 || f->ph == 4 ){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else if(f->ph == 2){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if ((f->ph == 1||f->ph == 4) && out_port >=gP + gA-1) {
    f->ph = 2;
  }  
  if (f->ph == 0 && out_port >=gP + gA-1) {
    f->ph = 3;
  }  
 
  out_vc = f->ph;
  outputs->AddRange( out_port, out_vc, out_vc );
}



//Basic adaptive routign algorithm for the dragonfly
void ugal_roc_dragonflynew( const Router *r, const Flit *f, int in_channel, 
			OutputSet *outputs, bool inject )
{
  //need 3 VCs for deadlock freedom

  assert(gNumVCs==3);
  outputs->Clear( );
  if(inject) {
    int inject_vc= RandomInt(gNumVCs-1);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }
  
  int _grp_num_routers= gA;
  int _grp_num_nodes =_grp_num_routers*gP;
  int _network_size =  gA * gP * gG;

 
  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = (int) (rID / _grp_num_routers);
  int dest_grp_ID = int(dest/_grp_num_nodes);

  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;
  double min_roc;
  double nonmin_roc;
  int intm_grp_ID;
  int intm_rID;

  if(debug){
    cout<<"At router "<<rID<<endl;
  }
  int min_router_output, nonmin_router_output;
  
  //at the source router, make the adaptive routing decision
  if ( in_channel < gP )   {
    //dest are in the same group, only use minimum routing
    if (dest_grp_ID == grp_ID) {
      f->ph = 2;
      f->minimal = 1;
    } else {
      //select a random node
      f->intm =RandomInt(_network_size - 1);
      intm_grp_ID = (int)(f->intm/_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }
      
      //random intermediate are in the same group, use minimum routing
      if(grp_ID == intm_grp_ID){
	f->ph = 1;
	f->minimal = 1;
      } else {
	//congestion metrics using queue length, obtained by GetCredit()
	min_router_output = dragonfly_port(rID, f->src, f->dest); 
      	min_roc = r->GetROC(min_router_output);

      
	nonmin_router_output = dragonfly_port(rID, f->src, f->intm);
	nonmin_roc = r->GetROC(nonmin_router_output);

	//congestion comparison, could use hopcnt instead of 1 and 2
	//	if (((1 * min_roc ) <= (2.0* nonmin_roc))||min_roc<0.0) {	  
	if (min_roc<0.0 || (min_roc < 2.0*nonmin_roc+roc_threshold )) {	  
	  if (debug)  cout << " MINIMAL routing " << endl;
	  f->ph = 1;
	  f->minimal = 1;
	} else {
	  f->ph = 0;
	  f->minimal = 0;
	}
      }
    }
  }

  //transition from nonminimal phase to minimal
  if(f->ph==0){
    intm_rID= (int)(f->intm/gP);
    if( rID == intm_rID){
      f->ph = 1;
    }
  }

  //port assignement based on the phase
  if(f->ph == 0){
    assert(f->minimal!=1);
    out_port = dragonfly_port(rID, f->src, f->intm);
  } else if(f->ph == 1){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else if(f->ph == 2){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if (f->ph == 1 && out_port >=gP + (gA-1)) {
    f->ph = 2;
  }  

  //vc assignemnt based on phase
  out_vc = f->ph;

  outputs->AddRange( out_port, out_vc, out_vc );
}



void ugalprog_roc_dragonflynew( const Router *r, const Flit *f, int in_channel, 
			OutputSet *outputs, bool inject )
{
  outputs->Clear( );
  if(inject) {
    int inject_vc= RandomInt(gNumVCs-1);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }

  int _grp_num_routers= gA;
  int _grp_num_nodes =_grp_num_routers*gP;
  int _network_size =  gA * gP * gG;

 
  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = (int) (rID / _grp_num_routers);
  int dest_grp_ID = int(dest/_grp_num_nodes);

  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;
  double min_roc;
  double nonmin_roc;
  int intm_grp_ID;
  int intm_rID;

  if(debug){
    cout<<"At router "<<rID<<endl;
  }
  int min_router_output, nonmin_router_output;
  
  if ( in_channel < gP )   {
    //dest are in the same group
    if (dest_grp_ID == grp_ID) {
      f->ph = 2;
      f->minimal = 1;
    } else {
      //select a random node
      f->intm =RandomInt(_network_size - 1);
      intm_grp_ID = (int)(f->intm/_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }
      //intermediate are in the same group
      if(grp_ID == intm_grp_ID){
	f->ph = 1;
	f->minimal = 1;
      } else {
	min_router_output = dragonfly_port(rID, f->src, f->dest); 
      	min_roc = r->GetROC(min_router_output);

      
	nonmin_router_output = dragonfly_port(rID, f->src, f->intm);
	nonmin_roc = r->GetROC(nonmin_router_output);

	//congestion comparison
	if (min_roc<0.0 || (min_roc < nonmin_roc+roc_threshold )) {
	  if (debug)  cout << " MINIMAL routing " << endl;
	  f->ph = 1;
	  f->minimal = 1;
	} else {
	  f->ph = 0;
	  f->minimal = 0;
	}
      }
    }
  } else if(f->ph == 1 && f->minimal==1){ //progressive
    assert(in_channel<gP + gA-1);
    //select a random node
    f->intm =RandomInt(_network_size - 1);
    intm_grp_ID = (int)(f->intm/_grp_num_nodes);
    if (debug){
      cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
    }
    //intermediate are in the same group
    if(grp_ID == intm_grp_ID){
    } else {
      min_router_output = dragonfly_port(rID, f->src, f->dest); 
      min_roc = r->GetROC(min_router_output);
      
      
      nonmin_router_output = dragonfly_port(rID, f->src, f->intm);
      nonmin_roc = r->GetROC(nonmin_router_output);
      

      //congestion comparison
      if (min_roc<0.0 || (min_roc < nonmin_roc+roc_threshold )) {
      } else {
	f->ph = 3;
	f->minimal = 2;
      }
    }    
  }

  //transition from nonminimal phase to minimal
  if(f->ph==0 || f->ph == 3){
    intm_rID= (int)(f->intm/gP);
    if( rID == intm_rID){
      f->ph = 1;
    }
  }


  //port assignement based on the phase
  if(f->ph == 0 || f->ph == 3){
    assert(f->minimal!=1);
    out_port = dragonfly_port(rID, f->src, f->intm);
  } else if(f->ph == 1){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else if(f->ph == 2){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if (f->ph == 1 && out_port >=gP + gA-1) {
    f->ph = 2;
  }  
 
  out_vc = f->ph;
  outputs->AddRange( out_port, out_vc, out_vc );
}
