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

#include "dragonfull.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "globals.hpp"
#include "reservation.hpp"

int gH=0;

extern int g_grp_num_routers;
extern int g_grp_num_nodes;
extern int g_network_size;

//UGAL getcredit vc listings
//!gReservation !ECN
extern int * vc_min_same;
extern int * vc_nonmin_same;
//gREservation
extern int * vc_min_res_same;
extern int * vc_nonmin_res_same;

extern bool ADAPTIVE_INTM_ALL;
extern bool RESERVATION_ADAPT_CONTROL;

#define MAX(X,Y) ((X>Y)?(X):(Y))

//packet output port based on the source, destination and current location
int dragonfull_port(int rID, int source, int dest){

  int out_port = -1;
  int grp_ID = int(rID / g_grp_num_routers); 
  int dest_grp_ID = int(dest/g_grp_num_nodes);
  int grp_output=-1;
  int grp_RID=-1;
  
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
  }

  //At the last hop
  if (dest >= rID*gP && dest < (rID+1)*gP) {    
    out_port = dest%gP;
  } else if (grp_RID == rID) {
    //At the optical link
    int pick_one = RandomInt(1);
    out_port = gP + 2*(2*gP-1) + 2*(grp_output %(gP))+pick_one ;
  } else {
    //need to route within a group
    assert(grp_RID!=-1);
    int pick_one = RandomInt(1);
    if (rID < grp_RID){
      out_port = pick_one + 2*((grp_RID % g_grp_num_routers) - 1) + gP;
    }else{
      out_port = pick_one + 2*(grp_RID % g_grp_num_routers) + gP;
    }
  }  
 
  assert(out_port!=-1);
  return out_port;
}

//test if two channels arrive at the same desitnation
int dragonfull_equal_channel( int output1, int output2){ 
  return  ((output1-gP)>>1) == ((output2-gP)>>1);
}


//same VC assignment as dragonfly
extern int SRP_VC_CONVERTER(int ph, int res_type);
extern void Dragonfly_Common_Setup( const Configuration &config);

//bandwidth ratio of 1:4:2
DragonFull::DragonFull( const Configuration &config, const string & name ) :
  Network( config, name )
{

  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}

void DragonFull::_ComputeSize( const Configuration &config )
{

  Dragonfly_Common_Setup(config);
  
  assert(!gCRT);//iq_router depends on gK
  assert(!gPB);//iq_router depends on gA


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
  // inter-group ports : 2_p
  // terminal ports : _p
  // intra-group ports : 4*_p - 1
  _p = config.GetInt( "k" );	// # of ports in each switch
  _n = config.GetInt( "n" );
  
  _global_channel_latency=config.GetInt("dragonfly_global_latency");
  _local_channel_latency=config.GetInt("dragonfly_local_latency");
  assert(_global_channel_latency>0 &&
	 _local_channel_latency>0);
 

  assert(_n==1);

  //radix
  if (_n == 1)
    _k = _p + 2*_p + 2*(2*_p  - 1);
  else
    _k = _p + 2*_p + 4*_p;

  

  gK = _p; 
  gN = _n;

  //routers per group
  if (_n == 1)
    _a = 2 * _p; //double channels in local group
  else
    _a = powi(_p, _n);
  //only evens but this shouldn't be a problem
  assert(((_a*_p)&0x01)==0);

  //globals per router
  _h = 2*_p;
  //groups
  _g = ((_a * _h)>>1) + 1;
  //network size
  _nodes   = (_a * _p)* _g;
  
  //router count
  _num_of_switch = _nodes / _p;
  _size = _num_of_switch;
  //enwtork channel count
  _channels = _num_of_switch * (_k - _p); 


  PiggyPack::_size=gH;
  gH = _h;
  gG = _g;
  gP = _p;
  gA = _a;

  _grp_num_routers = gA;
  _grp_num_nodes =_grp_num_routers*gP;

  g_grp_num_routers = gA;
  g_grp_num_nodes =_grp_num_routers*gP;
  g_network_size = _nodes;
}

void DragonFull::_BuildNet( const Configuration &config )
{

  int _output;
  int _input;
  int c;
  int _dim_ID;
  int _num_ports_per_switch;

  ostringstream router_name;



  cout << " Dragonfly fullx2" << endl;
  cout << " p = " << _p << " n = " << _n << endl;
  cout << " each switch - total radix =  "<< _k << endl;
  cout << " # of switches = "<<  _num_of_switch << endl;
  cout << " # of channels = "<<  _channels << endl;
  cout << " # of nodes ( size of network ) = " << _nodes << endl;
  cout << " # of groups (_g) = " << _g << endl;
  cout << " # of routers per group (_a) = " << _a << endl;

  for ( int node = 0; node < _num_of_switch; ++node ) {
    //cout<<"router "<<node<<endl;
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
    // _p == # of processor per router
    //  need 2*_p routers  --thus, 
    //  2(2_p-1) outputs channels within group, double channel
    //

    if (_n > 1 )  { cout << " ERROR: n>1 dimension NOT supported yet... " << endl; exit(-1); }

    //********************************************
    //   connect OUTPUT channels   EASY
    //********************************************
    _num_ports_per_switch = (_k - _p);
    // add intra-group output channel
    for ( int cnt = 0; cnt < 2*(2*_p -1); ++cnt ) {
      _output = _num_ports_per_switch*node + cnt;
      
	_routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );
	
	_chan[_output]->SetLatency(_local_channel_latency);
	_chan_cred[_output]->SetLatency(_local_channel_latency);
	//cout<<_output<<" output local "<<endl;
    }
    
    // add inter-group output channel
    for ( int cnt = 0; cnt < _h; ++cnt ) {
      _output = _num_ports_per_switch * node + 2*(2*_p - 1) + cnt;
      
      _routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );

      _chan[_output]->SetGlobal();
      _chan_cred[_output]->SetGlobal();
      _chan[_output]->SetLatency(_global_channel_latency);
      _chan_cred[_output]->SetLatency(_global_channel_latency);
      //cout<<_output<<" output global "<<endl;
    }


    //********************************************
    //   connect INPUT channels HARD
    //********************************************
    // # of non-local nodes 
    int _num_port_bundle =  (_k - _p)>>1;
    
    // intra-group GROUP channels
    {
      // NODE ID withing group
      _dim_ID = node % _a;

      //cnt is not 2(2p-1) for easier calculation, we will just add input and input+1
      for ( int cnt = 0; cnt < (2*_p-1); ++cnt ) {

	if ( cnt < _dim_ID)  {

	  _input = 	grp_ID  * _num_ports_per_switch * _a +
	    2*(cnt *  _num_port_bundle+ 
	       (_dim_ID - 1));
	}else {
	  _input =  grp_ID * _num_ports_per_switch * _a + 
	    2*((cnt + 1) *_num_port_bundle + 
	       _dim_ID);
			
	}

	if (_input < 0 ) {
	  cout << " ERROR: _input less than zero or greater than _k " <<_input<< endl;
	  exit(-1);
	}
	//cout<<_input<<" input local "<<endl<<_input+1<<" input local "<<endl;
	_routers[node]->AddInputChannel( _chan[_input], _chan_cred[_input] );
	_routers[node]->AddInputChannel( _chan[_input+1], _chan_cred[_input+1] );
      }
    }
    
    
    // add INPUT channels -- "optical" channels connecting the groups
    int grp_output_bundle;

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      grp_output_bundle = _dim_ID* _p + cnt; //output bundle


      if ( grp_ID > grp_output_bundle)   {

	_input = (grp_output_bundle) * _num_ports_per_switch * _a    +   		// starting point of group
	  2*((_num_port_bundle  - _p) * (int) ((grp_ID - 1) / _p) +      // find the correct router within grp
	     (_num_port_bundle  - _p) + 					// add offset within router
	     grp_ID - 1);	
      } else {

	_input = (grp_output_bundle + 1) * _num_ports_per_switch * _a    + 
	  2*((_num_port_bundle  - _p) * (int) ((grp_ID) / _p) +      // find the correct router within grp
	     (_num_port_bundle  - _p) +
	     grp_ID);	
      }
      //cout<<_input<<" input global "<<endl<<_input+1<<" input global "<<endl;
      _routers[node]->AddInputChannel( _chan[_input], _chan_cred[_input] );
      _routers[node]->AddInputChannel( _chan[_input+1], _chan_cred[_input+1] );
    }

  }

  for(size_t i = 0; i<_chan.size(); i++){
    assert(_chan[i]->GetSink()!=-1);
    assert(_chan[i]->GetSource()!=-1);
  }

  cout<<"Done links"<<endl;
}


int DragonFull::GetN( ) const
{
  return _n;
}

int DragonFull::GetK( ) const
{
  return _k;
}

void DragonFull::InsertRandomFaults( const Configuration &config )
{
 
}

double DragonFull::Capacity( ) const
{
  return (double)_k / 8.0;
}

void DragonFull::RegisterRoutingFunctions(){

  gRoutingFunctionMap["min_dragonfull"] = &min_dragonfull;
  gRoutingFunctionMap["val_dragonfull"] = &val_dragonfull;
  gRoutingFunctionMap["ugal_dragonfull"] = &ugal_dragonfull;
  gRoutingFunctionMap["ugalcrt_dragonfull"] = &ugal_dragonfull;
  gRoutingFunctionMap["ugalpb_dragonfull"] = &ugal_dragonfull;
  gRoutingFunctionMap["ugalprog_dragonfull"] = &ugalprog_dragonfull;
}




void min_dragonfull( const Router *r, const Flit *f, int in_channel, 
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

  out_port = dragonfull_port(rID, f->src, dest);

  //optical dateline
  if (out_port >=gP + 2*(2*gP-1)) {
    f->ph = 1;
  }  
  
  out_vc = SRP_VC_CONVERTER(f->ph,f->res_type);

  if (debug)
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
	       << "	through output port : " << out_port 
	       << " out vc: " << out_vc << endl;
  outputs->AddRange( out_port, out_vc, out_vc );
}



void val_dragonfull( const Router *r, const Flit *f, int in_channel, 
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
      f->intm =RandomInt(g_network_size - 1);
      intm_grp_ID = (int)(f->intm/g_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }
      //intermediate are in the same group
      if(f->res_type>RES_TYPE_NORM){
	f->ph = 0;
	f->minimal = 1;
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
    out_port = dragonfull_port(rID, f->src, f->intm);
  } else if(f->ph == 0){
    out_port = dragonfull_port(rID, f->src, f->dest);
  } else if(f->ph == 1 ){
    out_port = dragonfull_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if (f->ph == 0 && out_port >=gP + 2*(2*gP-1)) {
    f->ph = 1;
  }  

  //optical dateline nonmin
  if (f->ph == 2 && out_port >=gP + 2*(2*gP-1)) {
    f->ph = 3;
  }  
 
  out_vc = SRP_VC_CONVERTER(f->ph, f->res_type);
  outputs->AddRange( out_port, out_vc, out_vc );

}



int dragonfull_intm_select(int src_grp, int dst_grp, int grps, int grp_size, int net_size){
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



extern int debug_adaptive_same;
extern int debug_adaptive_same_min;

extern int debug_adaptive_prog_GvL;
extern int debug_adaptive_prog_GvL_min;
extern int debug_adaptive_prog_GvG;
extern int debug_adaptive_prog_GvG_min;

extern int debug_adaptive_LvL;
extern int debug_adaptive_LvL_min;
extern int debug_adaptive_LvG;
extern int debug_adaptive_LvG_min;
extern int debug_adaptive_GvL;
extern int debug_adaptive_GvL_min;
extern int debug_adaptive_GvG;
extern int debug_adaptive_GvG_min;

void ugalprog_dragonfull( const Router *r, const Flit *f, int in_channel, 
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
      f->intm =dragonfull_intm_select(grp_ID, dest_grp_ID, 
			   gG, g_grp_num_nodes, g_network_size);
      intm_grp_ID = int(f->intm/g_grp_num_nodes);
      if (debug){
	cout<<"Intermediate node "<<f->intm<<" grp id "<<intm_grp_ID<<endl;
      }

      //intermediate was useless are in the same group
      if( f->res_type>RES_TYPE_NORM){
	f->ph = 0;
	f->minimal = 1;
      } else if(grp_ID == intm_grp_ID ||intm_grp_ID==dest_grp_ID){
	f->ph = 0;
	f->minimal = 1;
      } else {
	min_router_output = dragonfull_port(rID, f->src, f->dest); 
	nonmin_router_output = dragonfull_port(rID, f->src, f->intm);

	//min and non-min output port could be identical, need to distinquish them
	if(nonmin_router_output == min_router_output ||
	   r->GetOutputChannel(min_router_output)->GetSink()==
	   r->GetOutputChannel(nonmin_router_output)->GetSink()){

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
	  //handling spec packets speically
	  if(f->res_type!=RES_TYPE_SPEC){
	    min_queue_size=0;
	    nonmin_queue_size=9999;
	  }
	} else {	  
	  min_queue_size = r->GetCredit(min_router_output); 
	  nonmin_queue_size = r->GetCredit(nonmin_router_output);
	}
	
	if(nonmin_router_output == min_router_output||
	   r->GetOutputChannel(min_router_output)->GetSink()==
	   r->GetOutputChannel(nonmin_router_output)->GetSink()){
	  debug_adaptive_same++;
	} else if( (min_router_output >=gP + 2*(2*gP-1)) && 
		   (nonmin_router_output >=gP + 2*(2*gP-1)) ){
	  debug_adaptive_GvG++;
	} else if( (min_router_output >=gP +  2*(2*gP-1)) && 
		   (nonmin_router_output <gP +  2*(2*gP-1)) ){
	  debug_adaptive_GvL++;
	} else if( (min_router_output <gP +  2*(2*gP-1)) && 
		   (nonmin_router_output >=gP +  2*(2*gP-1)) ){
	  debug_adaptive_LvG++;
	} else if( (min_router_output <gP +  2*(2*gP-1)) && 
		   (nonmin_router_output <gP +  2*(2*gP-1)) ){
	  debug_adaptive_LvL++;
	} else {
	  assert(false);
	}
	//handling spec packets specially
	//spec packet has a hard deadline and must be respected
	if(f->res_type==RES_TYPE_SPEC){
	  if(min_queue_size>f->exptime && nonmin_queue_size<f->exptime){
	    min_queue_size=999999;
	    nonmin_queue_size=0;
	  } 
	}

	if ((1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold) {	  
	  if (debug)  cout << " MINIMAL routing " << endl;
	  f->ph = 4;
	  f->minimal = 1;
	  if(nonmin_router_output == min_router_output||
	   r->GetOutputChannel(min_router_output)->GetSink()==
	   r->GetOutputChannel(nonmin_router_output)->GetSink()){
	    debug_adaptive_same_min++;	  
	  } else if( (min_router_output >=gP +  2*(2*gP-1)) && 
		     (nonmin_router_output >=gP +  2*(2*gP-1)) ){
	    debug_adaptive_GvG_min++;
	  } else if( (min_router_output >=gP +  2*(2*gP-1)) && 
		     (nonmin_router_output <gP +  2*(2*gP-1)) ){
	    debug_adaptive_GvL_min++;
	  } else if( (min_router_output <gP +  2*(2*gP-1)) && 
		     (nonmin_router_output >=gP +  2*(2*gP-1)) ){
	    debug_adaptive_LvG_min++;
	  } else if( (min_router_output <gP +  2*(2*gP-1)) && 
		     (nonmin_router_output <gP +  2*(2*gP-1)) ){
	    debug_adaptive_LvL_min++;
	  }else {
	    assert(false);
	  }
	} else {

	  if(nonmin_router_output == min_router_output){
	  } else if( (min_router_output >=gP +  2*(2*gP-1)) && 
		     (nonmin_router_output >=gP +  2*(2*gP-1)) ){
	  } else if( (min_router_output >=gP +  2*(2*gP-1)) && 
		     (nonmin_router_output <gP +  2*(2*gP-1)) ){


	  } else if( (min_router_output <gP +  2*(2*gP-1)) && 
		     (nonmin_router_output >=gP +  2*(2*gP-1)) ){

	  } else if( (min_router_output <gP +  2*(2*gP-1)) && 
		     (nonmin_router_output <gP +  2*(2*gP-1)) ){
	  }else {
	    assert(false);
	  }

	  f->ph = 2;
	  f->minimal = 0;
	}
      }
    }
  } else if(f->ph == 4 && f->minimal==1){ //progressive
    assert(in_channel<gP +  2*(2*gP-1));

    //select a random node, maybe we should keep the old one
    //f->intm =dragonfull_intm_select(grp_ID, dest_grp_ID, 
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
      min_router_output = dragonfull_port(rID, f->src, f->dest); 
      min_queue_size = r->GetCredit(min_router_output); 
      
      
      nonmin_router_output = dragonfull_port(rID, f->src, f->intm);
      nonmin_queue_size = r->GetCredit(nonmin_router_output);
      
      if( (min_router_output >=gP + 2*(2*gP-1)) && 
	  (nonmin_router_output >=gP + 2*(2*gP-1)) ){
	debug_adaptive_prog_GvG++;
      } else if( (min_router_output >=gP + 2*(2*gP-1)) && 
		 (nonmin_router_output <gP + 2*(2*gP-1)) ){
	debug_adaptive_prog_GvL++;
      } else {
	assert(false);
      }
      
      //special spec handling
      if(f->res_type==RES_TYPE_SPEC){
	if(min_queue_size>f->exptime && nonmin_queue_size<f->exptime){
	  min_queue_size=999999;
	  nonmin_queue_size=0;
	} 
      }
      
      if ((1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold ) {
	if( (min_router_output >=gP + 2*(2*gP-1)) && 
	    (nonmin_router_output >=gP + 2*(2*gP-1)) ){
	  debug_adaptive_prog_GvG_min++;
	} else if( (min_router_output >=gP + 2*(2*gP-1)) && 
		   (nonmin_router_output <gP + 2*(2*gP-1)) ){
	  debug_adaptive_prog_GvL_min++;
	} else {
	  assert(false);
	}
      } else {
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
    out_port = dragonfull_port(rID, f->src, f->intm);
  } else if(f->ph == 0 || f->ph == 4 ){
    out_port = dragonfull_port(rID, f->src, f->dest);
  } else if(f->ph == 1){
    out_port = dragonfull_port(rID, f->src, f->dest);
  } else {
    assert(false);
  }

  //optical dateline
  if ((f->ph == 0||f->ph == 4) && out_port >=gP + 2*(2*gP-1)) {
    f->ph = 1;
  }  
  if (f->ph == 2 && out_port >=gP + 2*(2*gP-1)) {
    f->ph = 3;
  }  
 
  out_vc = SRP_VC_CONVERTER(f->ph, f->res_type);
  outputs->AddRange( out_port, out_vc, out_vc );
}


int dragonfull_global_index(int src, int dest){
  if(src<dest){
    return dest-1;
  } else {
    return dest;
  }
}

//Why would you use this on a dragonfly
//Basic adaptive routign algorithm for the dragonfly
void ugal_dragonfull( const Router *r, const Flit *f, int in_channel, 
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
      f->intm =dragonfull_intm_select(grp_ID, dest_grp_ID, 
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
	min_router_output = dragonfull_port(rID, f->src, f->dest); 
	nonmin_router_output = dragonfull_port(rID, f->src, f->intm);

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
	  bool min_global_congest = r->GetCongest(dragonfull_global_index(grp_ID, dest_grp_ID));
	  bool nonmin_global_congest= r->GetCongest(dragonfull_global_index(grp_ID, intm_grp_ID));
	  
	  minimal = (1 * min_queue_size ) <= (2 * nonmin_queue_size)+adaptive_threshold &&
	    !(min_global_congest && !nonmin_global_congest);
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
    out_port = dragonfull_port(rID, f->src, f->intm);
  } else if(f->ph == 0){
    out_port = dragonfull_port(rID, f->src, f->dest);
  } else if(f->ph == 1){
    out_port = dragonfull_port(rID, f->src, f->dest);
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

