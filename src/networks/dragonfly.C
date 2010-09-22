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
#include <vector>
#include <sstream>

#include "dragonfly.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "globals.hpp"

//#define DEBUG_DRAGONFLYNEW 
#define DRAGON_LATENCY

// //reservation
// int** reservation;
// extern int gConstPacketSize;
// extern int compressed_buffersensitivity;

// //statistics tracking
// int total_packet = 0;
// int failed_packet = 0;
// int * route_categories = 0;


// //total fail count for dual traffic
// extern bool dual_traffic;
// extern int dual_time;
// extern int dual_window;
// int dual_total_packet=0;
// int dual_failed_packet=0;

// //cycle accurate failur count for dual traffic
// int* dual_total_cycle;
// int* dual_failed_cycle;
// int* dual_extra_cycle;
// extern int foward_window;

// //routing GUI
// bool route_trace = false;
// extern int global_time;

// //read reservation routing 
// bool use_reservation = false;

// //compression 
// bool use_compressed = false;
// int*** compressed_data;
// //int** uncompressed_data;


// bool is_cray = false;

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
  _sources = _a * _p * _g;
  _dests   = _sources;

  _num_of_switch = _sources / _p;
  _channels = _num_of_switch * (_k - _p); 
  _size = _num_of_switch;
  

//   //are we using reservation?
//   string fn;
//   config.GetStr( "routing_function", fn, "none" );
//   if(fn.compare("res")==0){
//     cout<<"USING RESERVATION\n";
//     use_reservation = true;
//   } else if (fn.compare("compressed")==0){
//     cout<<"USING COMPRESSION\n";
//     use_compressed = true;
//   }else {
//     use_reservation = false;
//     use_compressed = false;
//   }

//   if(use_reservation){
//     //reservation level of each router
//     reservation = (int**)malloc(_num_of_switch*sizeof(int*));
//     for(int i = 0 ; i<_num_of_switch; i++){
//       reservation[i]=(int *)malloc(gK*sizeof(int));
//     }
//     for(int i = 0 ; i<_num_of_switch; i++){
//       for(int j = 0; j<gK; j++){
// 	reservation[i][j]= 0;
//       }
//     }
//   }
 
//   if(use_compressed){
//     //compression data for each router, that hold data for each outport
//     compressed_data = (int***)malloc(_num_of_switch*sizeof(int**));
//     for(int i = 0 ; i<_num_of_switch; i++){
//       compressed_data[i]=(int **)malloc(_a*sizeof(int*));
//     }
//     for(int i = 0 ; i<_num_of_switch; i++){
//       for(int j = 0; j<_a; j++){
// 	compressed_data[i][j]=(int *)malloc(gK*sizeof(int));
//       }
//     }
//     for(int i = 0 ; i<_num_of_switch; i++){
//       for(int j = 0; j<_a; j++){
// 	for(int l = 0; l<gK; l++){
// 	  compressed_data[i][j][l]=0;
// 	}
//       }
//     }
//   }

//   //transient analysis
//   if(dual_traffic){
//     dual_total_cycle = (int *)malloc(dual_window*sizeof(int));
//     dual_failed_cycle =(int *)malloc(dual_window*sizeof(int));
//     dual_extra_cycle =(int *)malloc(dual_window*sizeof(int));
//     if(dual_traffic){
//       for(int i = 0 ; i<dual_window; i++){
// 	dual_total_cycle[i] = 0;
// 	dual_failed_cycle[i]= 0;
// 	dual_extra_cycle[i]= 0;
//       }
//     }
//   }

//   route_categories = (int*)malloc(5*sizeof(int));
//   for(int i = 0; i<5; i++)
//     route_categories[i] = 0;


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
  cout << " # of sources = " << _sources << endl;
  cout << " # of nodes ( size of network ) = " << _sources << endl;
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

#ifdef DEBUG_DRAGONFLYNEW
    cout << " ======== router node : " << node << " ======== " << " router_" << router_name.str() << " router node # : " << node<< endl;
    cout <<"group_ "<<grp_ID<<endl;
#endif

    router_name.str("");

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      c = _p * node +  cnt;
      _routers[node]->AddInputChannel( _inject[c], _inject_cred[c] );
#ifdef DEBUG_DRAGONFLYNEW
      cout << "  Adding injection channel " << c << endl;
#endif
    }

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      c = _p * node +  cnt;
      _routers[node]->AddOutputChannel( _eject[c], _eject_cred[c] );
#ifdef DEBUG_DRAGONFLYNEW
      cout << "  Adding ejection channel " << c << endl;
#endif
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
#ifdef DEBUG_DRAGONFLYNEW
	cout << "     Adding intra-group output channel : " << _output << " to node " << node << endl;
#endif
	_routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );

#ifdef DRAGON_LATENCY
	_chan[_output]->SetLatency(10);
	_chan_cred[_output]->SetLatency(10);
#endif
      }
    }

    // add inter-group output channel

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      _output = (2*_p-1 + _p) * node + (2*_p - 1) + cnt;
      #ifdef DEBUG_DRAGONFLYNEW
      cout << "     Adding inter-group output channel : " << _output << " to node " << node << endl;
      #endif
      //      _chan[_output].global = true;
      _routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );
#ifdef DRAGON_LATENCY
	_chan[_output]->SetLatency(100);
	_chan_cred[_output]->SetLatency(100);
#endif
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

#ifdef DEBUG_DRAGONFLYNEW
      cout << " NODE: " << node  << " DIMID: " << _dim_ID <<  " asdf: " << powi (_k+1, dim+1) << endl;
      cout << " _num_ports_per_switch " << _num_ports_per_switch << " _dim_size: " << _dim_size  <<  endl;
#endif

      // NODE ID withing group
      _dim_ID = node % _a;



#ifdef DEBUG_DRAGONFLYNEW
      //      cout << " new _dim_ID: " << _dim_ID << endl;
#endif

      for ( int cnt = 0; cnt < (2*_p-1); ++cnt ) {

	if ( cnt < _dim_ID)  {
#ifdef DEBUG_DRAGONFLYNEW
	  //	  cout << " Small ";
#endif
	  _input = 	grp_ID  * _num_ports_per_switch * _a - 
	    (_dim_ID - cnt) *  _num_ports_per_switch + 
	    _dim_ID * _num_ports_per_switch + 
	    (_dim_ID - 1);
	}
	else {
#ifdef DEBUG_DRAGONFLYNEW
	  //	  cout << " Big ";
#endif
	  //_input =  grp_ID * _num_ports_per_switch * _a + node * _num_ports_per_switch + (cnt - node + 1) * _num_ports_per_switch + node;
	  _input =  grp_ID * _num_ports_per_switch * _a + 
	    _dim_ID * _num_ports_per_switch + 
	    (cnt - _dim_ID + 1) * _num_ports_per_switch + 
	    _dim_ID;
			
	}

	if (_input < 0) {
	  cout << " ERROR: _input less than zero " << endl;
	  exit(-1);
	}

#ifdef DEBUG_DRAGONFLYNEW
	cout << "     Adding input channel : " << _input << " to node " << node << " "<<_chan[_input]->GetLatency()<<endl;
#endif

	_routers[node]->AddInputChannel( _chan[_input], _chan_cred[_input] );
      }
    }


    // add INPUT channels -- "optical" channels connecting the groups
    int grp_size_routers;
    int grp_output;
    int grp_ID2;

    for ( int cnt = 0; cnt < _p; ++cnt ) {
      //	   _dim_ID
      grp_output = _dim_ID* _p + cnt;

      grp_size_routers = powi(_k, _n-1);
      //grp_output = (node % grp_size_routers)* (_k-1) + cnt;
      grp_ID2 = (int) ((grp_ID - 1) / (_k - 1));

#ifdef DEBUG_DRAGONFLYNEW

#endif
      if ( grp_ID > grp_output)   {
#ifdef DEBUG_DRAGONFLYNEW
#endif
	_input = (grp_output) * _num_ports_per_switch * _a    +   		// starting point of group
	  (_num_ports_per_switch - _p) * (int) ((grp_ID - 1) / _p) +      // find the correct router within grp
	  (_num_ports_per_switch - _p) + 					// add offset within router
	  grp_ID - 1;	
      } else {
#ifdef DEBUG_DRAGONFLYNEW
	//	cout << " Big ";
#endif
	_input = (grp_output + 1) * _num_ports_per_switch * _a    + 
	  (_num_ports_per_switch - _p) * (int) ((grp_ID) / _p) +      // find the correct router within grp
	  (_num_ports_per_switch - _p) +
	  grp_ID;	
      }


#ifdef DEBUG_DRAGONFLYNEW
      cout << "     Adding inter-grp input channel : " << _input << " to node " << node << endl;
#endif

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

  //    gRoutingFunctionMap["res_dragonflynew"] = &res_dragonflynew;
  //    gRoutingFunctionMap["compressed_dragonflynew"] = &compressed_dragonflynew;
    gRoutingFunctionMap["min_dragonflynew"] = &min_dragonflynew;
}


int flatfly_selfrouting(int dest) {
  int out_port;
  out_port = dest % gK;
  return out_port;
}

void min_dragonflynew( const Router *r, const Flit *f, int in_channel, 
		       OutputSet *outputs, bool inject )
{
  outputs->Clear( );

  int dest  = f->dest;
  int rID =  r->GetID(); 
  int _radix = gK;
  int _dim_found;
  int grp_size_routers = 2* gK;
  int grp_size_nodes = grp_size_routers * gK;
  int grp_ID = (int) (rID / grp_size_routers); 
  int debug = f->watch;
  int out_port = -1;
  int out_vc = 0;
  int dest_grp_ID;
  int grp_output_RID;
  int grp_output;

  
  if ( in_channel < gK ) out_vc = 0; 
  else if (f->vc == 1)  out_vc = 1;

  if ( in_channel < gK )  
    f->ph  = 0;  

  if (debug)
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		<< " FLIT ID: " << f->id << " Router: " << rID << " routing  from src : " << f->src <<  " to dest : " << f->dest << " f->ph: " << f->ph << " in_channel: " << in_channel << " gK: " << gK << endl;
  
  if (dest >= grp_ID*grp_size_nodes && dest < (grp_ID+1)*grp_size_nodes) {
    // routing within router.
    f->ph == 2;
  } 

  if (f->ph == 0) {
    // find "optical" long link to route 

    dest_grp_ID = (int)((dest / gK) / grp_size_routers);
    if (dest_grp_ID == grp_ID) {
      f->ph = 2;
      dest  = f->dest;
    } else {
      if (grp_ID > dest_grp_ID) 
	grp_output = dest_grp_ID;
      else
	grp_output = dest_grp_ID - 1;

      grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;

      // create dummy dest for routing purpose
      dest = grp_output_RID * gK;
    }

  } else if (f->ph == 2) {
    dest  = f->dest;
  }

  _dim_found = 0;
  if (f->ph == 0 && grp_output_RID == rID) {
    if (debug)
      *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		  << " routing directly... " << endl;
    out_port = gK + (2*gK-1) + grp_output %(gK);
    _dim_found = 1;
    out_vc = 1;
    f->ph = 2;
  } else if (dest >= rID*_radix && dest < (rID+1)*_radix) {
    if (debug)
      *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		  << " selfrouting directly... " << endl;

    out_port = flatfly_selfrouting(dest);
    _dim_found = 1;
  } else {
    // routing within GROUP 
    _dim_found = 1;
    int dest_rID = (int) (dest / gK);
    //out_vc = 0;
    if (debug)
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		<< " rID: " << rID << " dest_rID: " << dest_rID << " dest: " << dest << endl;
    if (rID < dest_rID)
      out_port = (dest_rID % grp_size_routers) - 1 + gK;
    else
      out_port = (dest_rID % grp_size_routers) + gK;
  }

  if (debug) {
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		<< " grp_size_routers: " << grp_size_routers << endl
		<< " grp_ID: " << grp_ID
		<< " dest_grp_ID: " << dest_grp_ID << endl
		<< "dest: " << dest
		<< " grp_output_RID: " << grp_output_RID
		<< " rID: " << rID << endl
		<< " grp_output : " << grp_output << endl
		<< *f; }

  if (out_port == -1) { cout << " ERROR: no out_port found ! " << endl; exit(-1); }
  if (debug)
    *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		<< "	through output port : " << out_port << " out vc: " << out_vc << endl;

  outputs->AddRange( out_port, out_vc, out_vc );
}


// ////////////////////////////////////////////////////////////
// //FLIT RESERVATION
// ////////////////////////////////////////////////////////////
// void res_dragonflynew( const Router *r, const Flit *f, int in_channel,
// 		       OutputSet *outputs, bool inject )
// {


//   outputs->Clear( );
  
//   bool debug = false;

//   int _radix = gK;
//   int grp_size_routers = 2* gK;
//   int grp_size_nodes = grp_size_routers * gK;

//  beginning:

//   int dest  = f->dest;
//   int intm_dest = f->intm;

//   int rID =  r->GetID();
//   int intm_rID = (int)(f->intm/gK);
//   int dest_rID =  (int)(f->dest/gK);

//   int grp_ID = (int)(rID / grp_size_routers);
//   int dest_grp_ID = (int)((dest / gK) / grp_size_routers);
//   int intm_grp_ID = (int)((intm_dest / gK) / grp_size_routers);

//   int grp_output_RID;
//   int grp_output;

//   int out_port = -1;
//   int out_vc = 0;

//   double self_threshold = 0;

//   // f->ph == 0  -- routing to intermediate --> VC0
//   // f->ph == 1  -- routing minimally  --> VC1
//   // f->ph == 2  -- within destination --> VC2
//   // VC0  taking non-minimal hop 
//   // VC1  taking minimal hop to destination
//   // VC2  at final group

//   if(debug){
//     cout<<"/////////////////////"<<rID<<"//////////////////////////////////"<<endl;
//     cout<<*f;
//   }
//   if(debug && in_channel < gK)
//     cout<<"NEW FLIT"<<endl;

//   if(in_channel < gK && !f->reservation_flit){
//     total_packet++;


//     if(dual_traffic){
//       if(f->time>=dual_time && f->time<dual_time+dual_window){
// 	dual_total_packet++;
//       }
//       //start at dual_time-1
//       int monitortime = (f->time)-dual_time+foward_window;
//       if(monitortime>=0 && monitortime<dual_window){
// 	dual_total_cycle[monitortime]++;
//       }
//     }
//   }
//   //////////////////////////////////////////////////////////////////////
//   //At final deinstation group
//   //////////////////////////////////////////////////////////////////////
//   if(grp_ID == dest_grp_ID){
//     if(debug)
//       cout<<"WITH IN DEST GROUP group id "<<grp_ID<<endl;
//     f->ph = 2;
//     // if the destination is within the router
//     if (dest_rID == rID) {
//       if(debug)
// 	cout<<"WITHIN ROUTER  router id "<<rID<<endl;
//       out_port = dest%gK;
//     } else {
//       //if the destination is not within the router 
//       if (rID < dest_rID)
// 	out_port = (dest_rID % grp_size_routers) - 1 + gK;
//       else
// 	out_port = (dest_rID % grp_size_routers) + gK;
//     }
//     goto done;
//   }
  
//   //////////////////////////////////////////////////////////////////////
//   //Not at final destination group
//   //////////////////////////////////////////////////////////////////////
//   if (grp_ID > dest_grp_ID){
//     grp_output = dest_grp_ID;
//   } else {
//     grp_output = dest_grp_ID - 1;
//   }
//   grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;
//   // create dummy dest for routing purpose
//   dest = grp_output_RID * gK;
  
//   //////////////////////////////////////////////////////////////////////
//   //For a brand new flit at the source group
//   //////////////////////////////////////////////////////////////////////
//   if ( in_channel < gK ){

//     f->ph  = 0;
//     {
//       if(debug)
//       	cout<<"ACQUIRING RESERVATION"<<endl;
      
	
//       //check the minimal router "optical cable"
//       //dest here should be the dummy dest
//       dest_rID = (int) (dest / gK);

//       //MOTHER OF ALL COMPARISONS

//       if(!f->made_reservation){
// 	assert(!f->reservation_flit);
// 	assert(!f->reserve);
// 	//EPIC FAILED
// 	//time for valiant routing
// 	failed_packet++; //this statistics maybe wrong

// 	  if(dual_traffic){
// 	    if(f->time>=dual_time && f->time<dual_time+dual_window){
// 	      dual_failed_packet++;
// 	    }
// 	    //start at dual_time-1
// 	    int monitortime = (f->time)-dual_time+foward_window;
// 	    if(monitortime>=0 && monitortime<dual_window){
// 	      dual_failed_cycle[monitortime]++;
// 	    }
// 	  }

// 	f->minimal = 0;
// 	if(debug){
// 	  cout<<"Reservaiton failed actual "<<reservation[dest_rID][grp_output%gK]<<" Router"<<dest_rID<<endl;
// 	}
// 	intm_dest = RandomInt((grp_size_nodes)*(grp_size_nodes+1)-1);//(a*p+1)*a*p

// 	intm_grp_ID = (int)(intm_dest/grp_size_nodes); 
// 	intm_rID = (int)(intm_dest/gK);
// 	f->ph=0;
// 	f->intm = intm_dest;
// 	if(debug)
// 	  cout<<"CURRENT GROUP "<<grp_ID<<" DESTINATION GROUP "<<dest_grp_ID<<" INTERMEDIATE GROUP "<<intm_grp_ID<<" INTERMETIDATE ROUTER "<<intm_dest<<endl;


// 	//luck have it we go through the same route
// 	if(intm_grp_ID==grp_ID ){
// 	  if(debug)
// 	    cout<<"ALREADY AT INTERMEDiATE"<<endl;
// 	  goto rewin;
// 	}
	
// 	//do the noniminal routing based on the intermetidate router
// 	if (grp_ID > intm_grp_ID)
// 	  grp_output = intm_grp_ID;
// 	else
// 	  grp_output = intm_grp_ID - 1;
// 	grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;

// 	if(grp_output_RID == rID){
// 	  out_port = gK + (2*gK-1) + grp_output %(gK);
// 	  if(debug)
// 	    cout<<"LEAVING BY SELF"<<endl;
// 	  if(intm_grp_ID == dest_grp_ID){
// 	    f->ph = 1;
// 	  }
// 	  goto done;
// 	}

// 	if (rID < grp_output_RID)
// 	  out_port = (grp_output_RID % grp_size_routers) - 1 + gK;
// 	else
// 	  out_port = (grp_output_RID % grp_size_routers) + gK;
// 	goto done;
//       } else { 
// 	//minimal routing
// 	f->ph=1;
// 	f->reserve = true;
// 	if(debug)
// 	  cout<<"RESERVATION SUCCEDE limit  actual "<<reservation[dest_rID][grp_output%gK]-1<<" router "<<dest_rID<<endl;
	
// 	if(rID == dest_rID){
// 	  out_port = gK + (2*gK-1) + grp_output%gK;
// 	  goto done;
// 	}
// 	//route based on minimal routing
// 	if (rID < dest_rID)
// 	  out_port = (dest_rID % grp_size_routers) - 1 + gK;
// 	else
// 	  out_port = (dest_rID % grp_size_routers) + gK;
// 	goto done;
//       }
//     }
//   }

//   //////////////////////////////////////////////////////////////////////
//   //minimal routing
//   //////////////////////////////////////////////////////////////////////
//   if(f->ph == 1){
//     if(debug)
//       cout<<"MINIMAL ROUTING"<<endl;
//     if (grp_output_RID == rID) {
//       if(debug)
// 	cout<<"LEAVING OPTICS"<<endl;
//       out_port = gK + (2*gK-1) + grp_output %(gK);
//       {
// 	if(debug)
// 	  cout<<"USED UP RESERVATION channel "<<grp_output %(gK)<<" reservation level "<<reservation[rID][grp_output %(gK)]<<endl;
//       }
//       goto done;
//     } else {
//       if (rID < grp_output_RID)
// 	out_port = (grp_output_RID % grp_size_routers) - 1 + gK;
//       else
// 	out_port = (grp_output_RID % grp_size_routers) + gK;
//       goto done;
//     }

//     //////////////////////////////////////////////////////////////////////
//     //nonminimal routing
//     //////////////////////////////////////////////////////////////////////
//   } else if(f->ph == 0){
//     if(debug)
//       cout<<"NONMINIMAL ROUTING"<<endl;
//     dest = f->intm;
//     dest_grp_ID = (int)((dest / gK) / grp_size_routers);
//     dest_rID = (int)((dest / gK));
//     if (dest_rID == rID) {
//       if(debug)
// 	cout<<"SWITCHING TO MINIMAL ROUTING AT INTERM GROUP"<<endl;
//       //we have arrived at the intermediate destation group
//       //time to route minimally
//     rewin:
//       f->ph = 1;
//       dest = f->dest;
//       dest_grp_ID = (int)((dest / gK) / grp_size_routers);
 
//       //switching to minimal routing
//       if (grp_ID > dest_grp_ID){
// 	grp_output = dest_grp_ID;
//       } else{
// 	grp_output = dest_grp_ID - 1;
//       }
//       grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;
//       if(debug){
// 	cout<<"MINIMAL ROUTING"<<endl;
//       }
//       if (grp_output_RID == rID) {
// 	f->ph = 1;
// 	if(debug)
// 	  cout<<"LEAVING OPTICS"<<endl;
// 	out_port = gK + (2*gK-1) + grp_output %(gK);
    
// 	goto done;
//       } else {

// 	if (rID < grp_output_RID)
// 	  out_port = (grp_output_RID % grp_size_routers) - 1 + gK;
// 	else
// 	  out_port = (grp_output_RID % grp_size_routers) + gK;
// 	goto done;
//       }
      
//     } else {
//       if (grp_ID > dest_grp_ID){
// 	grp_output = dest_grp_ID;
//       } else{
// 	grp_output = dest_grp_ID - 1;
//       }

//       if(grp_ID == dest_grp_ID){
// 	grp_output_RID =(int)(dest/gK);
//       } else {
// 	grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;
//       }
//       if (rID < grp_output_RID){
// 	out_port = (grp_output_RID % grp_size_routers) - 1 + gK;
//       } else if(rID == grp_output_RID){
// 	if(debug)
// 	  cout<<"LEAVING OPTICS"<<endl;
// 	out_port = gK + (2*gK-1) + grp_output %(gK);
//       } else{ 
// 	out_port = (grp_output_RID % grp_size_routers) + gK;   
//       }   
//       goto done;
//     }
//   } else {
//     cout<<"SHOULD NEVER GET HERE, ph = 2 should have been taken care of"<<endl;
//     exit(-1);
//   }  

//  done:
//   out_vc = f->ph;
//   if(debug){
//     cout<<"OUTPUT AT "<<out_port<<" VC "<<out_vc<<endl;
//   }
//   outputs->AddRange( out_port, out_vc,  out_vc);

//   if(route_trace && f->reservation_flit){
//     cout<<"ID: "<<f->id<<" Router: "<<r->GetID()<<" Out Port: "<<out_port<<" phase: "<<f->ph<<" time: "<<global_time<<endl;
//   }

// }




// void compressed_dragonflynew( const Router *r, const Flit *f, int in_channel,
// 		       OutputSet *outputs, bool inject )
// {
//   //if compressed data is > than this, fail and route nonminial
//   int compressed_threshold = 0;
//   outputs->Clear( );
//   bool debug = false;

//   int _radix = gK;
//   int grp_size_routers = 2* gK;
//   int grp_size_nodes = grp_size_routers * gK;

//  beginning:

//   int dest  = f->dest;
//   int intm_dest = f->intm;

//   int rID =  r->GetID();
//   int intm_rID = (int)(f->intm/gK);
//   int dest_rID =  (int)(f->dest/gK);

//   int grp_ID = (int)(rID / grp_size_routers);
//   int dest_grp_ID = (int)((dest / gK) / grp_size_routers);
//   int intm_grp_ID = (int)((intm_dest / gK) / grp_size_routers);
//   int intm_grp_output;
//   int intm_grp_output_RID ;
//   int grp_output_RID;
//   int grp_output;

//   int out_port = -1;
//   int out_vc = 0;

//   // f->ph == 0  -- routing to intermediate --> VC0
//   // f->ph == 1  -- routing minimally  --> VC1
//   // f->ph == 2  -- within destination --> VC2
//   // VC0  taking non-minimal hop 
//   // VC1  taking minimal hop to destination
//   // VC2  at final group

//   if(debug){
//     cout<<"/////////////////////"<<rID<<"//////////////////////////////////"<<endl;
//     cout<<*f;
//   }
//   if(debug && in_channel < gK){
//     cout<<"NEW FLIT"<<endl;
//   }

//   if ( in_channel < gK ){
//     total_packet++;
//     if(dual_traffic){
//       if(f->time>=dual_time && f->time<dual_time+dual_window){
// 	dual_total_packet++;
//       }
//       //start at dual_time-1
//       int monitortime = (f->time)-dual_time+foward_window;
//       if(monitortime>=0 && monitortime<dual_window){
// 	dual_total_cycle[monitortime]++;
//       }
//     }
//   }
  
//   //compressed update happens in iqrouter, input queuex

//   //////////////////////////////////////////////////////////////////////
//   //At final deinstation group
//   //////////////////////////////////////////////////////////////////////
//   if(grp_ID == dest_grp_ID){
//     if(debug){
//       cout<<"WITH IN DEST GROUP group id "<<grp_ID<<endl;
//     }

//     f->ph = 2;
//     // if the destination is within the router
//     if (dest_rID == rID) {
//       if(debug){
// 	cout<<"WITHIN ROUTER  router id "<<rID<<endl;
//       }
//       out_port = dest%gK;
//     } else {
//       //if the destination is not within the router 
//       if (rID < dest_rID)
// 	out_port = (dest_rID % grp_size_routers) - 1 + gK;
//       else
// 	out_port = (dest_rID % grp_size_routers) + gK;
//     }

//     //gotta check local ugal
//     if(in_channel<gK){

//       intm_dest = RandomInt((grp_size_nodes)-1)+grp_ID*grp_size_nodes;
//       intm_rID = (int)(intm_dest/gK);
//       if(intm_rID !=rID){
// 	if (rID < intm_rID)
// 	  intm_grp_output = (intm_rID % grp_size_routers) - 1 + gK;
// 	else
// 	  intm_grp_output = (intm_rID % grp_size_routers) + gK;
// 	int min_queue = r->GetCredit(out_port, -1, -1) ;  
// 	int nonmin_queue =  r->GetCredit(intm_grp_output, -1, -1) ;  
// 	nonmin_queue = 2*nonmin_queue+compressed_buffersensitivity*gConstPacketSize;
// 	if(min_queue>nonmin_queue){
// 	  f->ph = 0;
// 	  failed_packet++;
// 	  if(dual_traffic){
// 	    if(f->time>=dual_time && f->time<dual_time+dual_window){
// 	      dual_failed_packet++;
// 	    }
// 	    //start at dual_time-1
// 	    int monitortime = (f->time)-dual_time+foward_window;
// 	    if(monitortime>=0 && monitortime<dual_window){
// 	      dual_failed_cycle[monitortime]++;
// 	    }
// 	  }
// 	  f->minimal= 0;
// 	  f->intm = intm_dest;
// 	  goto localugal;
// 	}
//       }
//     }
//     goto done;
//   }
  
//   //////////////////////////////////////////////////////////////////////
//   //Not at final destination group
//   //////////////////////////////////////////////////////////////////////
//   if (grp_ID > dest_grp_ID){
//     grp_output = dest_grp_ID;
//   } else {
//     grp_output = dest_grp_ID - 1;
//   }
//   grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;
//   // create dummy dest for routing purpose
//   dest = grp_output_RID * gK;
  
//   //////////////////////////////////////////////////////////////////////
//   //For a brand new flit at the source group
//   //////////////////////////////////////////////////////////////////////
//   if ( in_channel < gK ){
//     f->ph  = 0;
//     {
//       //generate a nonminimal path
//       //check the minimal router "optical cable"
//       dest_rID = (int) (dest / gK);

//       do{
// 	intm_dest = RandomInt((grp_size_nodes)*(grp_size_nodes+1)-1);
// 	intm_grp_ID = (int)(intm_dest/grp_size_nodes); 
// 	intm_rID = (int)(intm_dest/gK);
	
// 	//do the noniminal routing based on the intermetidate router
	
	
// 	if (grp_ID < intm_grp_ID)
// 	  intm_grp_output = intm_grp_ID-1;
// 	else 
// 	  intm_grp_output = intm_grp_ID;
	
// 	intm_grp_output_RID = ((int) (intm_grp_output / (gK))) + grp_ID * grp_size_routers;
//     } while((grp_ID == intm_grp_ID)||((f->dest/grp_size_nodes)==intm_grp_ID));

// 	if((grp_ID == intm_grp_ID)||((f->dest/grp_size_nodes)==intm_grp_ID)){
// 	  goto rewin;
// 	}

      
//       bool minimal_decision = compressed_data[r->GetID()][dest_rID%grp_size_routers][grp_output%gK]>compressed_threshold;
//       bool nonminimal_decision = compressed_data[r->GetID()][intm_grp_output_RID%grp_size_routers][intm_grp_output%gK]>compressed_threshold;

//       //local ugal

//       //route based on minimal routing
//       int min_port = -1;
//       int nonmin_port = -1;
//       if(rID==dest_rID)
// 	min_port = grp_output%gK+3*gK-1;
//       else 
// 	if (rID > dest_rID)
// 	  min_port = (dest_rID % grp_size_routers) + gK;
// 	else
// 	  min_port = (dest_rID % grp_size_routers)-1 + gK;
      
//       if(rID == intm_grp_output_RID)
// 	nonmin_port = intm_grp_output%gK+3*gK-1;
//       else 
// 	if (rID > intm_grp_output_RID)
// 	  nonmin_port = (intm_grp_output_RID % grp_size_routers) + gK;
// 	else
// 	  nonmin_port = (intm_grp_output_RID % grp_size_routers)-1 + gK;

//       int min_queue = r->GetCredit(min_port, -1, -1) ;  
//       int nonmin_queue =  r->GetCredit(nonmin_port, -1, -1) ;  

//       //truth table, true = link too full
//       //Min____NMin____Go MIN
//       //true   true     yes
//       //false  true     yes
//       //true   false    no
//       //false  false    yes
//       if(minimal_decision  && nonminimal_decision){
// 	//route_categories[0]++; 
//       } else if(minimal_decision  && !nonminimal_decision){
// 	//route_categories[1]++; 
//       } else if(!minimal_decision  && nonminimal_decision){
// 	//route_categories[2]++; 
//       } else if(!minimal_decision  && !nonminimal_decision){
// 	//route_categories[3]++; 
//       }
//       ////////////////////////////////////////////////
//       //MOTHER OF ALL COMPARISONS
//       ///////////////////////////////////////////////
//       if(minimal_decision  && !nonminimal_decision){
//       //refail:
// 	//////////////////////////////////////////////
// 	//EPIC FAILED
// 	//////////////////////////////////////////////
// 	//time for valiant routing
// 	failed_packet++;
// 	if(dual_traffic){
// 	  if(f->time>=dual_time && f->time<dual_time+dual_window){
// 	    dual_failed_packet++;
// 	  }
// 	  //start at dual_time-1
// 	  int monitortime = (f->time)-dual_time+foward_window;
// 	  if(monitortime>=0 && monitortime<dual_window){
// 	    dual_failed_cycle[monitortime]++;
// 	  }
// 	}
// 	refail:
// 	f->minimal = 0;
// 	if(debug){
// 	  cout<<"Reservaiton failed actual "<<endl;
// 	}
// 	if (grp_ID > intm_grp_ID)
// 	  grp_output = intm_grp_ID;
// 	else
// 	  grp_output = intm_grp_ID - 1;
// 	grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;
	
// 	f->ph=0;
// 	f->intm = intm_dest;
// 	if(debug){
// 	  cout<<"CURRENT GROUP "<<grp_ID<<" DESTINATION GROUP "<<dest_grp_ID<<" INTERMEDIATE GROUP "<<intm_grp_ID<<" INTERMETIDATE ROUTER "<<intm_dest<<endl;
// 	}

// 	if(grp_output_RID == rID){
// 	  out_port = gK + (2*gK-1) + grp_output %(gK);
// 	  if(debug){
// 	    cout<<"LEAVING BY SELF"<<endl;
// 	  }
// 	  //	  uncompressed_data[r->GetID()][out_port-gK*3+1]+=gConstPacketSize;
// 	  if(intm_grp_ID == dest_grp_ID){
// 	    f->ph = 1;
// 	  }
// 	  goto done;
// 	}

// 	if (rID < grp_output_RID)
// 	  out_port = (grp_output_RID % grp_size_routers) - 1 + gK;
// 	else
// 	  out_port = (grp_output_RID % grp_size_routers) + gK;
// 	goto done;
//       } else { 
// 	//////////////////////////////////////////////
// 	//EPIC WIN
// 	//////////////////////////////////////////////
// 	nonmin_queue = 2*nonmin_queue+compressed_buffersensitivity*gConstPacketSize;

// 	if(min_queue > nonmin_queue){
// 	  if(debug){cout<<"Refailed!\n";}
// 	  route_categories[4]++; 
// 	  goto refail;
// 	}
	  

// 	f->ph=1;
// 	f->minimal =  1;
// 	f->reserve = true;
// 	if(debug)
// 	  cout<<"RESERVATION SUCCEDE limit  actual "<<reservation[dest_rID][grp_output%gK]-1<<" router "<<dest_rID<<endl;
	
// 	if(rID == dest_rID){
// 	  out_port = gK + (2*gK-1) + grp_output%gK;
// 	  //uncompressed_data[r->GetID()][out_port-gK*3+1]+=gConstPacketSize;
// 	  goto done;
// 	}
// 	//route based on minimal routing
// 	if (rID < dest_rID)
// 	  out_port = (dest_rID % grp_size_routers) - 1 + gK;
// 	else
// 	  out_port = (dest_rID % grp_size_routers) + gK;
// 	goto done;
//       }
//     }
//   }

//  localugal:
//   //////////////////////////////////////////////////////////////////////
//   //minimal routing
//   //////////////////////////////////////////////////////////////////////
//   if(f->ph == 1){
//     if(debug){
//       cout<<"MINIMAL ROUTING"<<endl;
//     }
//     if (grp_output_RID == rID) {
//       if(debug){
// 	cout<<"LEAVING OPTICS"<<endl;
//       }
//       out_port = gK + (2*gK-1) + grp_output %(gK);
//       //uncompressed_data[r->GetID()][out_port-gK*3+1]+=gConstPacketSize;
//       if(debug){
// 	cout<<"USED UP RESERVATION channel "<<grp_output %(gK)<<" reservation level "<<reservation[rID][grp_output %(gK)]<<endl;
//       }
//       goto done;
//     } else {
//       if (rID < grp_output_RID)
// 	out_port = (grp_output_RID % grp_size_routers) - 1 + gK;
//       else
// 	out_port = (grp_output_RID % grp_size_routers) + gK;
//       goto done;
//     }
    
//     //////////////////////////////////////////////////////////////////////
//     //nonminimal routing
//     //////////////////////////////////////////////////////////////////////
//   } else if(f->ph == 0){
//     if(debug){
//       cout<<"NONMINIMAL ROUTING"<<endl;
//     }
//     dest = f->intm;
//     dest_grp_ID = (int)((dest / gK) / grp_size_routers);
//     dest_rID = (int)((dest / gK));
//     if (dest_rID == rID) {
//       if(debug){
// 	cout<<"SWITCHING TO MINIMAL ROUTING AT INTERM GROUP"<<endl;
//       }
//       //we have arrived at the intermediate destation group
//       //time to route minimally
//     rewin:
//       f->ph = 1;
//       dest = f->dest;
//       dest_grp_ID = (int)((dest / gK) / grp_size_routers);
 
//       //switching to minimal routing
//       if (grp_ID > dest_grp_ID){
// 	grp_output = dest_grp_ID;
//       } else{
// 	grp_output = dest_grp_ID - 1;
//       }
//       grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;
//       if(debug){
// 	cout<<"MINIMAL ROUTING"<<endl;
//       }
//       if (grp_output_RID == rID) {
// 	f->ph = 1;
// 	if(debug){
// 	  cout<<"LEAVING OPTICS"<<endl;
// 	}
// 	out_port = gK + (2*gK-1) + grp_output %(gK);
// 	//uncompressed_data[r->GetID()][out_port-gK*3+1]+=gConstPacketSize;
// 	goto done;
//       } else {

// 	if (rID < grp_output_RID)
// 	  out_port = (grp_output_RID % grp_size_routers) - 1 + gK;
// 	else
// 	  out_port = (grp_output_RID % grp_size_routers) + gK;
// 	goto done;
//       }
      
//     } else {
//       if (grp_ID > dest_grp_ID){
// 	grp_output = dest_grp_ID;
//       } else{
// 	grp_output = dest_grp_ID - 1;
//       }

//       if(grp_ID == dest_grp_ID){
// 	grp_output_RID =(int)(dest/gK);
//       } else {
// 	grp_output_RID = ((int) (grp_output / (gK))) + grp_ID * grp_size_routers;
//       }
//       if (rID < grp_output_RID){
// 	out_port = (grp_output_RID % grp_size_routers) - 1 + gK;
//       } else if(rID == grp_output_RID){
// 	if(debug){
// 	  cout<<"LEAVING OPTICS"<<endl;
// 	}
// 	out_port = gK + (2*gK-1) + grp_output %(gK);
// 	//uncompressed_data[r->GetID()][out_port-gK*3+1]+=gConstPacketSize;
//       } else{ 
// 	out_port = (grp_output_RID % grp_size_routers) + gK;   
//       }   
//       goto done;
//     }
//   } else {
//     cout<<"SHOULD NEVER GET HERE, ph = 2 should have been taken care of"<<endl;
//     exit(-1);
//   }  

//  done:
//   out_vc = f->ph;
//   if(debug){
//     cout<<"OUTPUT AT "<<out_port<<" VC "<<out_vc<<endl;
//   }
//   outputs->AddRange( out_port, out_vc,  out_vc);

//   if(route_trace && f->reservation_flit){
//     cout<<"ID: "<<f->id<<" Router: "<<r->GetID()<<" Out Port: "<<out_port<<" phase: "<<f->ph<<" time: "<<global_time<<endl;
//   }


// }
