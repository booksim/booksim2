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

  THIS SOFTWARE IS PROVIDED BY THE CO3YRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
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
#include "reservation.hpp"

int gP, gA, gG;

int g_grp_num_routers=0;
int g_grp_num_nodes=0;
int g_network_size = 0;


//UGAL getcredit vc listings
//!gReservation !ECN
int * vc_min_same;
int * vc_nonmin_same;
//gREservation
int * vc_min_res_same;
int * vc_nonmin_res_same;

extern bool ADAPTIVE_INTM_ALL;

//speculative packet is set to drop if queue depth are too high
bool RESERVATION_ADAPT_SPEC_KILL=false;
//speculative packet decision also based on actual value of the queue depth
bool RESERVATION_ADAPT_SPEC_TIME=true;
//enables control packet adaptive routing
bool RESERVATION_ADAPT_CONTROL=false;
//% of control packets routing nonminimally
float RESERVATION_ADAPT_CONTROL_ratio=0.0;


#define MAX(X,Y) ((X>Y)?(X):(Y))


//global port indexing, given src_grp and dest_grp
int dragonfly_global_index(int src, int dest){
  if(src<dest){
    return dest-1;
  } else {
    return dest;
  }
}

//calculate the hop count between src and estination
int dragonflynew_hopcnt(int src, int dest) 
{
  int hopcnt;
  int dest_grp_ID, src_grp_ID; 
  int src_hopcnt, dest_hopcnt;
  int src_intm, dest_intm;
  int grp_output, dest_grp_output;
  int grp_output_RID;

  
  dest_grp_ID = int(dest/g_grp_num_nodes);
  src_grp_ID = int(src / g_grp_num_nodes);
  
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
    grp_output_RID = ((int) (grp_output / (gP))) + src_grp_ID * g_grp_num_routers;
    src_intm = grp_output_RID * gP;

    grp_output_RID = ((int) (dest_grp_output / (gP))) + dest_grp_ID * g_grp_num_routers;
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

  int out_port = -1;
  int grp_ID = int(rID / g_grp_num_routers); 
  int dest_grp_ID = int(dest/g_grp_num_nodes);
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
    grp_RID = int(grp_output /gP) + grp_ID * g_grp_num_routers;
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
      out_port = (grp_RID % g_grp_num_routers) - 1 + gP;
    }else{
      out_port = (grp_RID % g_grp_num_routers) + gP;
    }
  }  
 
  assert(out_port!=-1);
  return out_port;
}

//Dragonfly VCs are assigned by phase (ph)
//With SRP control vcs, different offsets are added infront of the phase
//Currently control vc DONOT route adaptively
//          Spec vc DO route adaptively
int SRP_VC_CONVERTER(int ph, int res_type){
  int vc= -1;
  switch(res_type){
  case RES_TYPE_NACK:
  case RES_TYPE_GRANT:
  case RES_TYPE_ACK:
    if(gECN){
      return ph;
    } else {
      return (ph+
	      gResVCStart); //offset: ecn_vc + aux
    }
    break;
  case RES_TYPE_RES:
    return (ph+
	    gResVCStart); //reservation first set of vcs
    break;
  case RES_TYPE_SPEC:
    if(gSpecVCStart+1==gNSpecVCStart){//reservation_spec_deadlock=1
      return gSpecVCStart;//impossible deadlock??
    } else{
      return (ph+
	      gSpecVCStart);//offset: ctrl_vc + ctrl_aux
    }
    break;
  case RES_TYPE_NORM:
    if(gECN){
      return (ph+
	      gNSpecVCStart); //offset: ack_vc + ack_aux
    } else if(gReservation){
      return (ph+
	      gNSpecVCStart);
    } else {
      return ph;
    }
    break;
  default:
    assert(false);
  }
  return vc;
}

void  Dragonfly_Common_Setup( const Configuration &config){
  gCRT = config.GetStr("routing_function")=="ugalcrt";
  gPB = (config.GetStr("routing_function")=="ugalpb") ||
    (config.GetStr("routing_function")=="ugalultra");
  
  if(config.GetStr("routing_function")=="ugalprog" ||
     config.GetStr("routing_function")=="ugal" ||
     config.GetStr("routing_function")=="ugalcrt" ||
     config.GetStr("routing_function")=="ugalpb"){
    gAdaptive = true;
  }
  
  RESERVATION_ADAPT_SPEC_KILL=(config.GetInt("reservation_adapt_spec_kill")==1);
  RESERVATION_ADAPT_SPEC_TIME=(config.GetInt("reservation_adapt_spec_time")==1);
  RESERVATION_ADAPT_CONTROL= (config.GetInt("reservation_adapt_control")==1);
  RESERVATION_ADAPT_CONTROL_ratio=config.GetFloat("reservation_adapt_control_ratio");

  gAdaptVCs = config.GetInt("adapt_vc");
  gAuxVCs = config.GetInt("aux_vc");

  //dragonfly can't do more than 1 spec vc
  assert(config.GetInt("hotspot_reservation")!=1 || 
	 config.GetInt( "res_vcs" )==1);

  if(config.GetInt("hotspot_reservation")==1){
    gResVCStart=0;
    if(RESERVATION_ADAPT_CONTROL){
      gGANVCStart =gResVCStart+1+gAdaptVCs+gAuxVCs;
      gSpecVCStart=gGANVCStart+1+gAdaptVCs+gAuxVCs;
    } else {
      gGANVCStart =gResVCStart+1+gAuxVCs;
      gSpecVCStart=gGANVCStart+1+gAuxVCs;
    }
    if(config.GetInt("reservation_spec_deadlock")==1){
      gNSpecVCStart= gSpecVCStart+1;
    } else {
      gNSpecVCStart= gSpecVCStart+1+gAdaptVCs+gAuxVCs;
    }
  } else {
    gNSpecVCStart=0;
  }

  gAdaptiveThreshold = config.GetFloat("adaptive_threshold");
  gAdaptiveThresholdSpec = config.GetFloat("adaptive_threshold_spec");
  if(gAdaptiveThresholdSpec<0)
    gAdaptiveThresholdSpec=gAdaptiveThreshold;

  cout<<"Dragonfly Pre-setup\n";
  cout<<"Res start "<<gResVCStart<<endl;
  cout<<"GAN start "<<gGANVCStart<<endl;
  cout<<"Spe start "<<gSpecVCStart<<endl;
  cout<<"Nsp start "<<gNSpecVCStart<<endl;
  cout<<"Threshold  "<<gAdaptiveThreshold<<endl;
  cout<<"Spec Threshold "<<gAdaptiveThresholdSpec<<endl;
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
  Dragonfly_Common_Setup(config );

  //These arrays are for ugal routing, hardcoded which VC to check for queu length
  //ECN not supported y et
  //assert(!gECN);

  vc_min_same = new int[1];
  //vc_min_same[0] = SRP_VC_CONVERTER(0,RES_TYPE_NORM); //min-min
  vc_min_same[0] = SRP_VC_CONVERTER(4,RES_TYPE_NORM); //unsure-min

  vc_nonmin_same =new int[1];
  vc_nonmin_same[0] = SRP_VC_CONVERTER(2,RES_TYPE_NORM); //nonmin

  vc_min_res_same = new int[6];
  vc_min_res_same[0] = SRP_VC_CONVERTER(4,RES_TYPE_NORM);
  vc_min_res_same[1] = SRP_VC_CONVERTER(4,RES_TYPE_SPEC);
  vc_min_res_same[2] = SRP_VC_CONVERTER(0,RES_TYPE_RES);
  vc_min_res_same[3] = SRP_VC_CONVERTER(0,RES_TYPE_NACK);
  vc_min_res_same[4] = SRP_VC_CONVERTER(0,RES_TYPE_ACK);
  vc_min_res_same[5] = SRP_VC_CONVERTER(0,RES_TYPE_GRANT);

  vc_nonmin_res_same = new int[2];
  vc_nonmin_res_same[0] = SRP_VC_CONVERTER(2,RES_TYPE_NORM);
  vc_nonmin_res_same[1] = SRP_VC_CONVERTER(2,RES_TYPE_SPEC);



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


  PiggyPack::_size=gK;
  gG = _g;
  gP = _p;
  gA = _a;

  _grp_num_routers = gA;
  _grp_num_nodes =_grp_num_routers*gP;

  g_grp_num_routers = gA;
  g_grp_num_nodes =_grp_num_routers*gP;
  g_network_size = _nodes;
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
	//cout<<_output<<" output local "<<endl;
      }
    }

    // add inter-group output channel

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      _output = (2*_p-1 + _p) * node + (2*_p - 1) + cnt;

      //      _chan[_output].global = true;
      _routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );
      _chan[_output]->SetGlobal();
      _chan_cred[_output]->SetGlobal();
      _chan[_output]->SetLatency(_global_channel_latency);
      _chan_cred[_output]->SetLatency(_global_channel_latency);
      //cout<<_output<<" output global "<<endl;
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
	//cout<<_input<<" input local "<<endl;
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
      
      //cout<<_input<<" input global "<<endl;
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
  gRoutingFunctionMap["ugalcrt_dragonflynew"] = &ugal_dragonflynew;
  gRoutingFunctionMap["ugalpb_dragonflynew"] = &ugal_dragonflynew;
  gRoutingFunctionMap["ugalultra_dragonflynew"] = &ugalprog_dragonflynew;
  gRoutingFunctionMap["ugalprog_dragonflynew"] = &ugalprog_dragonflynew;
}




void min_dragonflynew( const Router *r, const Flit *f, int in_channel, 
		       OutputSet *outputs, bool inject )
{
  outputs->Clear( );
  if(inject) {
    //injection && res_type_res means we are assigning VC for a speculative flow buffer
    int inject_vc= SRP_VC_CONVERTER(0,(f->res_type == RES_TYPE_RES)?RES_TYPE_SPEC:f->res_type);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }

  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = int(rID / g_grp_num_routers); 
  int dest_grp_ID = int(dest/g_grp_num_nodes);
  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;

  if ( in_channel < gP ) {
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
  
  out_vc = SRP_VC_CONVERTER(f->ph,f->res_type);

  if (debug)
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
	       << "	through output port : " << out_port 
	       << " out vc: " << out_vc << endl;
  outputs->AddRange( out_port, out_vc, out_vc );
}

int dragonfly_intm_select(int src_grp, int dst_grp, int grps, int grp_size, int net_size){
  if(ADAPTIVE_INTM_ALL){
    return RandomInt(net_size - 1);
  } else {
    int g = RandomInt(grps-1);
    while(g==src_grp || g==dst_grp){
      g = RandomInt(grps-1);
    }
    return (g*grp_size+RandomInt(grp_size-1));
  }
}

int dragonfly_val_intm_select(int src_grp, int dst_grp, int grps, int grp_size, int net_size){
  if(ADAPTIVE_INTM_ALL){
    return RandomInt(net_size - 1);
  } else {
    int g = RandomInt(grps-1);
    while(g==src_grp ){
      g = RandomInt(grps-1);
    }
    return (g*grp_size+RandomInt(grp_size-1));
  }
}

void val_dragonflynew( const Router *r, const Flit *f, int in_channel, 
		       OutputSet *outputs, bool inject )
{

  //ph 0 min 
  //ph 1 dest
  //ph 2 nonmin source
  //ph 3 nonmin intm

  outputs->Clear( );
  if(inject) {
    int inject_vc= SRP_VC_CONVERTER(0,(f->res_type == RES_TYPE_RES)?RES_TYPE_SPEC:f->res_type);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }

 
  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = (int) (rID / g_grp_num_routers);
  int dest_grp_ID = int(dest/g_grp_num_nodes);

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
      f->ph = 1;
      f->minimal = 1;
    } else {
      //select a random node
      f->intm =dragonfly_val_intm_select(grp_ID, dest_grp_ID, 
					 gG, g_grp_num_nodes, g_network_size);//RandomInt(g_network_size - 1);
      intm_grp_ID = (int)(f->intm/g_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }
      //intermediate are in the same group
      if(f->res_type>RES_TYPE_NORM && !RESERVATION_ADAPT_CONTROL){
	f->ph = 0;
	f->minimal = 1;
      } else if( f->res_type>RES_TYPE_NORM && RESERVATION_ADAPT_CONTROL){
	if(RandomFloat()<RESERVATION_ADAPT_CONTROL_ratio){
	  f->ph = 2;
	  f->minimal = 0;
	} else {
	  f->ph = 0;
	  f->minimal = 1;
	}
      } else if(grp_ID == intm_grp_ID || dest_grp_ID == intm_grp_ID){
	f->ph = 0;
	f->minimal = 1;
      } else { 
	//use valiant
	f->ph = 2;
	f->minimal = 0;
      }
    }
  }

  //transition from nonminimal phase to minimal
  if(f->ph==2 || f->ph==3){
    intm_rID= (int)(f->intm/gP);
    intm_grp_ID = (int)(f->intm/g_grp_num_nodes);
    if( rID == intm_rID){
      f->ph = 0;
    }
  }
  //port assignement based on the phase
  if(f->ph == 2 || f->ph==3){
    assert(f->minimal!=1);
    out_port = dragonfly_port(rID, f->src, f->intm);
  } else if(f->ph == 0){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else if(f->ph == 1 ){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if (f->ph == 0 && out_port >=gP + (gA-1)) {
    f->ph = 1;
  }  

  //optical dateline nonmin
  if (f->ph == 2 && out_port >=gP + (gA-1)) {
    f->ph = 3;
  }  
 
  out_vc = SRP_VC_CONVERTER(f->ph, f->res_type);
  outputs->AddRange( out_port, out_vc, out_vc );

}






extern int debug_adaptive_same;
extern int debug_adaptive_same_min;

extern int debug_adaptive_prog_GvL;
extern int debug_adaptive_prog_GvL_min;
extern int debug_adaptive_prog_GvG;
extern int debug_adaptive_prog_GvG_min;

extern int debug_adaptive_pb;
extern int debug_adaptive_pb_tt;
extern int debug_adaptive_pb_tf;
extern int debug_adaptive_pb_ft;
extern int debug_adaptive_pb_ff;

extern int debug_adaptive_LvL;
extern int debug_adaptive_LvL_min;
extern int debug_adaptive_LvG;
extern int debug_adaptive_LvG_min;
extern int debug_adaptive_GvL;
extern int debug_adaptive_GvL_min;
extern int debug_adaptive_GvG;
extern int debug_adaptive_GvG_min;

void ugalprog_dragonflynew( const Router *r, const Flit *f, int in_channel, 
			    OutputSet *outputs, bool inject )
{
  //ph 0 min 
  //ph 1 dest
  //ph 2 nonmin source
  //ph 3 nonmin intm
  //ph 4 unsure source

  outputs->Clear( );
  if(inject) {
    int inject_vc= SRP_VC_CONVERTER(0,(f->res_type == RES_TYPE_RES)?RES_TYPE_SPEC:f->res_type);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }

  int adaptive_threshold;
  if(f->res_type==RES_TYPE_SPEC){
    adaptive_threshold = int(float(f->packet_size)*gAdaptiveThresholdSpec);
  } else {
    adaptive_threshold = int(float(f->packet_size)*gAdaptiveThreshold);
  }
 
  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = (int) (rID / g_grp_num_routers);
  int dest_grp_ID = int(dest/g_grp_num_nodes);

  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;

  int min_router_output, min_queue_size;
  int nonmin_router_output, nonmin_queue_size;

  int intm_grp_ID;
  int intm_rID;

  if(debug){
    cout<<"At router "<<rID<<endl;
  }
  
  if ( in_channel < gP )   {
    //dest are in the same group
    if (dest_grp_ID == grp_ID  ) {
      f->ph = 1;
      f->minimal = 1;
    } else {
      //select a random node
      f->intm =dragonfly_intm_select(grp_ID, dest_grp_ID, 
			   gG, g_grp_num_nodes, g_network_size);
      intm_grp_ID = int(f->intm/g_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }

      //intermediate was useless are in the same group
      if( f->res_type>RES_TYPE_NORM && !RESERVATION_ADAPT_CONTROL){
	f->ph = 0;
	f->minimal = 1;
      } else if( f->res_type>RES_TYPE_NORM && RESERVATION_ADAPT_CONTROL){
	if(RandomFloat()<RESERVATION_ADAPT_CONTROL_ratio){
	  f->ph = 2;
	  f->minimal = 0;
	} else {
	  f->ph = 0;
	  f->minimal = 1;
	}
      } else if(grp_ID == intm_grp_ID ||intm_grp_ID==dest_grp_ID){
	f->ph = 0;
	f->minimal = 1;
      } else {
	min_router_output = dragonfly_port(rID, f->src, f->dest); 
	nonmin_router_output = dragonfly_port(rID, f->src, f->intm);

	//min and non-min output port could be identical, need to distinquish them
	if(nonmin_router_output == min_router_output){
	  if(gReservation){
	    min_queue_size = 
	      r->GetCreditArray(min_router_output,
				vc_min_res_same,6 ,false, true);
	    nonmin_queue_size =
	      r->GetCreditArray(nonmin_router_output,
				vc_nonmin_res_same,2 ,false, true);
	  } else {

	    min_queue_size = 
	      r->GetCreditArray(min_router_output,
				vc_min_same,1,false, true);
	    nonmin_queue_size = 
	      r->GetCreditArray(nonmin_router_output,
				vc_nonmin_same,1,false, true);
	  }
	} else {	  
	  min_queue_size = r->GetCredit(min_router_output); 
	  nonmin_queue_size = r->GetCredit(nonmin_router_output);
	}
	
	if(nonmin_router_output == min_router_output){
	  debug_adaptive_same++;
	} else if( (min_router_output >=gP + gA-1) && 
		   (nonmin_router_output >=gP + gA-1) ){
	  debug_adaptive_GvG++;
	} else if( (min_router_output >=gP + gA-1) && 
		   (nonmin_router_output <gP + gA-1) ){
	  debug_adaptive_GvL++;
	} else if( (min_router_output <gP + gA-1) && 
		   (nonmin_router_output >=gP + gA-1) ){
	  debug_adaptive_LvG++;
	} else if( (min_router_output <gP + gA-1) && 
		   (nonmin_router_output <gP + gA-1) ){
	  debug_adaptive_LvL++;
	} else {
	  assert(false);
	}

	//this is the adaptive decision
	bool minimal = true;

	if(gPB){
	  //prog with pb
	  bool min_global_congest = r->GetCongest(dragonfly_global_index(grp_ID, dest_grp_ID));
	  bool nonmin_global_congest= r->GetCongest(dragonfly_global_index(grp_ID, intm_grp_ID));
	  minimal = (1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold &&
	    !(min_global_congest && !nonmin_global_congest);

	  debug_adaptive_pb++;
	  if((min_global_congest && nonmin_global_congest)){
	    debug_adaptive_pb_tt++;
	  } else if((min_global_congest && !nonmin_global_congest)){
	    debug_adaptive_pb_tf++;
	  } if((!min_global_congest && nonmin_global_congest)){
	    debug_adaptive_pb_ft++;
	  } if((!min_global_congest && !nonmin_global_congest)){
	    debug_adaptive_pb_ff++;
	  }

	} else {
	  //normal prog
	  minimal = (1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold;  
	}

	//handling spec packets specially
	//spec packet has a hard deadline and must be respected
	if(f->res_type==RES_TYPE_SPEC && RESERVATION_ADAPT_SPEC_TIME){
	  if(min_queue_size>f->exptime && nonmin_queue_size<f->exptime){
	    minimal=false;
	  } 
	  //test for pure valiant spec 
	  minimal = false;
	}
	if(f->res_type==RES_TYPE_SPEC && RESERVATION_ADAPT_SPEC_KILL){
	  if(min_queue_size*2>f->exptime && nonmin_queue_size*2>f->exptime){
	    minimal=false;
	    f->payload=-666;
	  } 
	}

	if (minimal) {	  
	  if (debug)  cout << " MINIMAL routing " << endl;
	  f->ph = 4;
	  f->minimal = 1;
	  if(nonmin_router_output == min_router_output){
	    debug_adaptive_same_min++;	  
	  } else if( (min_router_output >=gP + gA-1) && 
		     (nonmin_router_output >=gP + gA-1) ){
	    debug_adaptive_GvG_min++;
	  } else if( (min_router_output >=gP + gA-1) && 
		     (nonmin_router_output <gP + gA-1) ){
	    debug_adaptive_GvL_min++;
	  } else if( (min_router_output <gP + gA-1) && 
		     (nonmin_router_output >=gP + gA-1) ){
	    debug_adaptive_LvG_min++;
	  } else if( (min_router_output <gP + gA-1) && 
		     (nonmin_router_output <gP + gA-1) ){
	    debug_adaptive_LvL_min++;
	  }else {
	    assert(false);
	  }
	} else {

	  if(nonmin_router_output == min_router_output){
	  } else if( (min_router_output >=gP + gA-1) && 
		     (nonmin_router_output >=gP + gA-1) ){
	  } else if( (min_router_output >=gP + gA-1) && 
		     (nonmin_router_output <gP + gA-1) ){


	  } else if( (min_router_output <gP + gA-1) && 
		     (nonmin_router_output >=gP + gA-1) ){

	  } else if( (min_router_output <gP + gA-1) && 
		     (nonmin_router_output <gP + gA-1) ){
	  }else {
	    assert(false);
	  }

	  f->ph = 2;
	  f->minimal = 0;
	}
      }
    }
  } else if( f->ph == 4 && f->minimal==1){ //progressive
    assert(in_channel<gP + gA-1);

    //select a random node, maybe we should keep the old one
    //f->intm =dragonfly_intm_select(grp_ID, dest_grp_ID, 
    //gG, g_grp_num_nodes, g_network_size);

    intm_grp_ID = (int)(f->intm/g_grp_num_nodes);
    if (debug){
      cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
    }

    //intermediate are in the same group
    if(grp_ID == intm_grp_ID ||intm_grp_ID==dest_grp_ID ){
      //shoudl track this stat
    } else {
      //congestion metric using queue length
      min_router_output = dragonfly_port(rID, f->src, f->dest); 
      min_queue_size = r->GetCredit(min_router_output); 
      
      
      nonmin_router_output = dragonfly_port(rID, f->src, f->intm);
      nonmin_queue_size = r->GetCredit(nonmin_router_output);
      
      if( (min_router_output >=gP + gA-1) && 
	  (nonmin_router_output >=gP + gA-1) ){
	debug_adaptive_prog_GvG++;
      } else if( (min_router_output >=gP + gA-1) && 
		 (nonmin_router_output <gP + gA-1) ){
	debug_adaptive_prog_GvL++;
      } else {
	assert(false);
      }
      bool minimal = true;
      
      if(gPB){
	  //prog with pb
	  bool min_global_congest = r->GetCongest(dragonfly_global_index(grp_ID, dest_grp_ID));
	  bool nonmin_global_congest= r->GetCongest(dragonfly_global_index(grp_ID, intm_grp_ID));
	  minimal = (1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold&&
	    !(min_global_congest && !nonmin_global_congest);
      } else {
	//normal prog
	minimal = (1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold ;
      }

      //special spec handling
      if(f->res_type==RES_TYPE_SPEC && RESERVATION_ADAPT_SPEC_TIME){
	if(min_queue_size>f->exptime && nonmin_queue_size<f->exptime){
	  minimal=false;
	} 
      }
      if(f->res_type==RES_TYPE_SPEC && RESERVATION_ADAPT_SPEC_KILL){
	if(min_queue_size>f->exptime && nonmin_queue_size>f->exptime){
	  minimal=false;
	  f->payload=-666;
	} 
      }

      if (minimal) {
	//minimal minimal
	if( (min_router_output >=gP + gA-1) && 
	    (nonmin_router_output >=gP + gA-1) ){
	  debug_adaptive_prog_GvG_min++;
	} else if( (min_router_output >=gP + gA-1) && 
		   (nonmin_router_output <gP + gA-1) ){
	  debug_adaptive_prog_GvL_min++;
	} else {
	  assert(false);
	}
      } else {
	//non minimal
	f->ph = 2;
	f->minimal = 2;
      }
    }    
  }
  
  //transition from nonminimal phase to minimal
  if(f->ph==2 || f->ph == 3){
    intm_rID= (int)(f->intm/gP);
    if( rID == intm_rID){
      f->ph = 0;
    }
  }


  //port assignement based on the phase
  if(f->ph == 2 || f->ph == 3){
    assert(f->minimal!=1);
    out_port = dragonfly_port(rID, f->src, f->intm);
  } else if(f->ph == 0 || f->ph == 4 ){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else if(f->ph == 1){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if ((f->ph == 0||f->ph == 4) && out_port >=gP + gA-1) {
    f->ph = 1;
  }  
  if (f->ph == 2 && out_port >=gP + gA-1) {
    f->ph = 3;
  }  
 
  out_vc = SRP_VC_CONVERTER(f->ph, f->res_type);
  outputs->AddRange( out_port, out_vc, out_vc );
}



//Why would you use this on a dragonfly
//Basic adaptive routign algorithm for the dragonfly
void ugal_dragonflynew( const Router *r, const Flit *f, int in_channel, 
			OutputSet *outputs, bool inject )
{
  //need 4 VCs for deadlock freedom
  //ph 0 min 
  //ph 1 dest
  //ph 2 nonmin source
  //ph 3 nonmin intm

  outputs->Clear( );
  if(inject) {
    int inject_vc= SRP_VC_CONVERTER(0,(f->res_type == RES_TYPE_RES)?RES_TYPE_SPEC:f->res_type);
    outputs->AddRange(0,inject_vc, inject_vc);
    return;
  }
  
  assert(!gReservation);

  //this constant biases the adaptive decision toward minimum routing
  //negative value woudl biases it towards nonminimum routing
  int adaptive_threshold = int(float(f->packet_size)*gAdaptiveThreshold);
 
  int dest  = f->dest;
  int rID =  r->GetID(); 
  int grp_ID = (int) (rID / g_grp_num_routers);
  int dest_grp_ID = int(dest/g_grp_num_nodes);

  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;

  int min_queue_size;;
  int nonmin_queue_size;

  int intm_grp_ID;
  int intm_rID;

  if(debug){
    cout<<"At router "<<rID<<endl;
  }
  int min_router_output, nonmin_router_output;
  if ( in_channel < gP )   {
    //dest are in the same group
    if (dest_grp_ID == grp_ID  ) {
      f->ph = 1;
      f->minimal = 1;
    } else {
      //select a random node
      f->intm =dragonfly_intm_select(grp_ID, dest_grp_ID, 
			   gG, g_grp_num_nodes, g_network_size);
      intm_grp_ID = int(f->intm/g_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }

      //intermediate was useless are in the same group
      if(grp_ID == intm_grp_ID ||intm_grp_ID==dest_grp_ID){
	f->ph = 0;
	f->minimal = 1;
      } else {
	min_router_output = dragonfly_port(rID, f->src, f->dest); 
	nonmin_router_output = dragonfly_port(rID, f->src, f->intm);

	//min and non-min output port could be identical, need to distinquish them
	if(nonmin_router_output == min_router_output){
	  min_queue_size = 
	    r->GetCreditArray(min_router_output,
			      vc_min_same,1,false, true);
	  nonmin_queue_size = 
	    r->GetCreditArray(nonmin_router_output,
			      vc_nonmin_same,1,false, true);
	} else {	  
	  min_queue_size = r->GetCredit(min_router_output); 
	  nonmin_queue_size = r->GetCredit(nonmin_router_output);
	}
	
	if(nonmin_router_output == min_router_output){
	  debug_adaptive_same++;
	} else if( (min_router_output >=gP + gA-1) && 
		   (nonmin_router_output >=gP + gA-1) ){
	  debug_adaptive_GvG++;
	} else if( (min_router_output >=gP + gA-1) && 
		   (nonmin_router_output <gP + gA-1) ){
	  debug_adaptive_GvL++;
	} else if( (min_router_output <gP + gA-1) && 
		   (nonmin_router_output >=gP + gA-1) ){
	  debug_adaptive_LvG++;
	} else if( (min_router_output <gP + gA-1) && 
		   (nonmin_router_output <gP + gA-1) ){
	  debug_adaptive_LvL++;
	} else {
	  assert(false);
	}
	
	bool minimal = true;
	if(gPB){
	  bool min_global_congest = r->GetCongest(dragonfly_global_index(grp_ID, dest_grp_ID));
	  bool nonmin_global_congest= r->GetCongest(dragonfly_global_index(grp_ID, intm_grp_ID));
	  
	  minimal = (1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold &&
	    !(min_global_congest && !nonmin_global_congest);
	  debug_adaptive_pb++;

	  if((min_global_congest && nonmin_global_congest)){
	    debug_adaptive_pb_tt++;
	  } else if((min_global_congest && !nonmin_global_congest)){
	    debug_adaptive_pb_tf++;
	  } if((!min_global_congest && nonmin_global_congest)){
	    debug_adaptive_pb_ft++;
	  } if((!min_global_congest && !nonmin_global_congest)){
	    debug_adaptive_pb_ff++;
	  }

	} else {
	  minimal = (1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold;
	}

	if (minimal) {	  
	  if (debug)  cout << " MINIMAL routing " << endl;
	  f->ph = 0;
	  f->minimal = 1;
	  if(nonmin_router_output == min_router_output){
	    debug_adaptive_same_min++;	  
	  } else if( (min_router_output >=gP + gA-1) && 
		     (nonmin_router_output >=gP + gA-1) ){
	    debug_adaptive_GvG_min++;
	  } else if( (min_router_output >=gP + gA-1) && 
		     (nonmin_router_output <gP + gA-1) ){
	    debug_adaptive_GvL_min++;
	  } else if( (min_router_output <gP + gA-1) && 
		     (nonmin_router_output >=gP + gA-1) ){
	    debug_adaptive_LvG_min++;
	  } else if( (min_router_output <gP + gA-1) && 
		     (nonmin_router_output <gP + gA-1) ){
	    debug_adaptive_LvL_min++;
	  }else {
	    assert(false);
	  }
	} else {

	  if(nonmin_router_output == min_router_output){
	  } else if( (min_router_output >=gP + gA-1) && 
		     (nonmin_router_output >=gP + gA-1) ){
	  } else if( (min_router_output >=gP + gA-1) && 
		     (nonmin_router_output <gP + gA-1) ){


	  } else if( (min_router_output <gP + gA-1) && 
		     (nonmin_router_output >=gP + gA-1) ){

	  } else if( (min_router_output <gP + gA-1) && 
		     (nonmin_router_output <gP + gA-1) ){
	  }else {
	    assert(false);
	  }
	  f->ph = 2;
	  f->minimal = 0;
	}
      }
    }
  }
  

  //transition from nonminimal phase to minimal
  if(f->ph==2 || f->ph == 3){
    intm_rID= (int)(f->intm/gP);
    if( rID == intm_rID){
      f->ph = 0;
    }
  }

  //port assignement based on the phase
  if(f->ph == 2|| f->ph==3){
    assert(f->minimal!=1);
    out_port = dragonfly_port(rID, f->src, f->intm);
  } else if(f->ph == 0){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else if(f->ph == 1){
    out_port = dragonfly_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if (f->ph == 0 && out_port >=gP + (gA-1)) {
    f->ph = 1;
  }  
  //optical dateline
  if (f->ph == 2 && out_port >=gP + (gA-1)) {
    f->ph = 3;
  }  

  //vc assignemnt based on phase
  out_vc = SRP_VC_CONVERTER(f->ph, f->res_type);
  outputs->AddRange( out_port, out_vc, out_vc );
}

