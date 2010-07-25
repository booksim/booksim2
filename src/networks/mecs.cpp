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

/*MECS topology
 *
 *MECS, multiple-drop off express channel S? topology
 *
 *1 output channel per direction, these channel has drop off poitns
 *  to multiple router in that direction
 *A combiner take multiple channel inputs from the same directio and
 *  feed it into the router, router size is 4+concentration
 *Literally allows for only 1 VC becaues the drop off points have a 
 *  single fifo
 */


#include "booksim.hpp"
#include <vector>
#include <sstream>
#include <math.h>
#include "mecs.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "globals.hpp"
#include "MECSRouter.hpp"
#include "MECSChannels.hpp"
#include "MECSCreditChannel.hpp"

//#define DEBUG_MECS


MECS::MECS( const Configuration &config, const string & name ) : 
Network( config, name ){
  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}


void MECS::_ComputeSize( const Configuration &config ) {
  _k = config.GetInt( "k" );	// # of routers per dimension
  _n = config.GetInt( "n" );	// dimension
  _c = config.GetInt( "c" );    //concentration, may be different from k

  xcount = config.GetInt("x");  //how many routers in the x or y direction
  ycount = config.GetInt("y");

  xrouter = config.GetInt("xr");  //configuration of hohw many clients in X and Y per router
  yrouter = config.GetInt("yr");


  //only support this configuration right now
  //need to extend to 256 nodes
  assert(_k ==4 && _n ==2 && _c == 4);

  _r = _c+4; //, chanenls are combined pior to entering the router
  gK = _k; 
  gN = _n;
  gC = _c;

  //standard traffic pattern shennanigin
  string fn;
  config.GetStr( "traffic", fn, "none" );
  if(fn.compare("neighbor")==0 || fn.compare("tornado")==0){
    realgk = 8;
    realgn = 2;
  } else {
    realgk = _k;
    realgn = _n;
  }

  _sources = powi( _k, _n )*_c;   //network size
  _dests   = powi( _k, _n )*_c;

  _num_of_switch = _sources / _c;
  //k-1 channels  per dimension per router, 
  //add 4 channels go from forwarder to the routers
  //yes, some of the channels will be forever idle in the corner routers
  _channels = _num_of_switch * ((_k-1)*_n+4);
  _channels_per_router =  ((_k-1)*_n+4);
  _size = _num_of_switch;

  cout<<"MECS"<<endl;
  cout<<"Number of sources "<<_sources<<endl;
  cout<<"Number of routers "<<_num_of_switch<<endl;
  cout<<"NUmber of channels "<<_channels<<endl;

}

void MECS::_BuildNet( const Configuration &config ) {


  ostringstream router_name;
  for ( int node = 0; node < _num_of_switch; ++node ) {
    router_name << "router";
    router_name << "_" <<  node ;
    _routers[node] = Router::NewRouter( config, this, router_name.str( ), 
					node, _r, _r );
#ifdef DEBUG_MECS
    cout<<"=============="<<router_name.str()<<"===================\n";
#endif
    router_name.str("");
    
    //the MECSRouters has many non-standard methods....
    MECSRouter* cur = dynamic_cast<MECSRouter*>(_routers[node]);
    //******************************************************************
    // add inject/eject channels connected to the processor nodes
    //******************************************************************
    
    int y_index = node/(xcount);
    int x_index = node%(xcount);
    //assume inject, eject latency of 1
    for (int y = 0; y < yrouter ; y++) {
      for (int x = 0; x < xrouter ; x++) {
	//adopted from the CMESH, the first node has 0,1,8,9 (as an example)
	int link = (xcount * xrouter) * (yrouter * y_index + y) + (xrouter * x_index + x) ;
	//	_inject[link]->SetLatency(ileng);
	//_inject_cred[link]->SetLatency(ileng);
	//_eject[link] ->SetLatency(ileng);
	//_eject_cred[link]->SetLatency(ileng);
	_inject[link]->SetLatency(0);
	_inject_cred[link]->SetLatency(0);
	_eject[link] ->SetLatency(0);
	_eject_cred[link]->SetLatency(0);
	cur->AddInputChannel( _inject[link], _inject_cred[link] );
	
#ifdef DEBUG_MECS
	cout << "  Adding injection channel " << link << " with latency "<<ileng<<endl;
#endif
	
	cur->AddOutputChannel( _eject[link], _eject_cred[link] );
#ifdef DEBUG_MECS
	cout << "  Adding ejection channel " << link << " with latency "<<ileng<<endl;
#endif
      }
    }
    //******************************************************************
    // add Input channels
    //******************************************************************
    /*
      N = 0
      E = 1
      S = 2;
      W = 3;
    */
    int link = -1;

    for(int i = 0; i<4; i++){
      link = node * _channels_per_router + i;
      //inputs channels is the wire from combiner to the router crossbar
      _chan[link]->SetLatency(0);
      cur->AddInputChannel( _chan[link], _chan_cred[link] , i);
#ifdef DEBUG_MECS
      cout<<"    input channel "<<link<<" to router "<<node<<" latency "<<0<<endl;
#endif
    }

  }

  //******************************************************************
  // add output channels
  //******************************************************************
  for ( int node = 0; node < _num_of_switch; ++node ) {
#ifdef DEBUG_MECS
    cout<<"==============Router "<<node<<"===================\n";
#endif
    int link = -1;
    int x_location = -1; //where the router is ... 0,0 is northwest
    int y_location = -1;
    x_location = node%gK;
    y_location = (int)(node/gK);

    //the MECSRouters has many non-standard methods....
    MECSRouter* cur = dynamic_cast<MECSRouter*>(_routers[node]);
    //track the number of output_added to correctness check
    int output_added  = 0;
    for(int i = 0; i<4; i++){
      link = link = node * _channels_per_router + 4 + output_added;
      MECSChannels* cur_channel = 0;
      MECSCreditChannels* cur_credit = 0;
      //determine the number of drop off points for this output channel
      int drops = 0;
      switch(i){
      case 0:
	drops = y_location;
	break;
      case 1:
	drops = gK-1-x_location;
	break;
      case 2:
	drops = gK-1-y_location;
	break;
      case 3:
	drops = x_location;
	break;
      default:
	assert(false);
      }
      //non-zero means there is a channel that direction
      if(drops!=0){
	cur_channel = new MECSChannels(this, "chan", node, i, drops);
	cur_credit = new MECSCreditChannels(this, "cred", node, i, drops);
#ifdef DEBUG_MECS
	cout<<"    NEW MECSchannel  to router "<<node<<endl;
#endif
	//add the MECSChannels to the routers
	cur->AddMECSChannel(cur_channel,i);
	cur->AddMECSCreditChannel(cur_credit, i);
	
	for(int j = 0; j<drops; j++){
	  //calculate which channel
	  link = link = node * _channels_per_router + 4 + output_added;
	  output_added++;
	  //_chan[link]->SetLatency(latency);
	  _chan[link]->SetLatency(0);
	  //the first output channel gets added to the routers
	  if(j ==0){
	    cur->AddOutputChannel(_chan[link], _chan_cred[link]);
#ifdef DEBUG_MECS
	    cout<<"    output channel "<<link<<" to router "<<node<<" latency "<<latency<<endl;
#endif
	  }
	  //add the channel to the MECS Channels
	  cur_channel->AddChannel(_chan[link],  j);
	  cur_credit->AddChannel(_chan_cred[link], j);
	  //add a dropoff point the router
	  MECSForwarder * forwarder = cur_channel->GetForwarder(j);
	  MECSCreditForwarder * creditforwarder = cur_credit->GetForwarder(j);
	  int add_router = forwarder->GetLocation();
	  dynamic_cast<MECSRouter*>(_routers[add_router])->AddForwarder(forwarder,i);
	  add_router = creditforwarder->GetLocation();
	  dynamic_cast<MECSRouter*>(_routers[add_router])->AddCreditForwarder(creditforwarder,i);

#ifdef DEBUG_MECS
	cout<<"    output channel "<<link<<" to MECS Channel \n";
#endif
	}
      } else {
	//if the channel is suppose to be empty, put in something
	//so it doesn't segfaults
	cur->AddOutputChannel(new FlitChannel(), new CreditChannel());
#ifdef DEBUG_MECS
	cout<<"   Fake Channel\n";
#endif
      }
    }
    assert(output_added == (gK-1)*2);
    //connec the channels to the underlying iq_router in a MECSRouter
    cur->Finalize();
  }
  
}

void MECS::RegisterRoutingFunctions(){
  gRoutingFunctionMap["dor_MECS"] = &dor_MECS;
}


//X first Y second
void dor_MECS( const Router *r, const Flit *f, int in_channel, 
	       OutputSet *outputs, bool inject ){

  outputs->Clear();
  int out_port =-1;
  
  bool debug = f->watch;

  int rID = r->GetID();
  int r_x_location = (int)(rID%gK);
  int r_y_location = (int)(rID/gK);
  int dest = mecs_transformation(f->dest);
  int dest_router = (int)(dest/gK);
  int dest_x_location = (int)(dest_router%gK);
  int dest_y_location = (int)(dest_router/gK);


 int intm = 0;
  int intm_router = 0;

  //at destination
  if(rID == dest_router){
    out_port = dest%gK; 
    if(debug)
      {
	*gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		    <<f->id<<" Routing to final destination outport "<<out_port<<endl;}
    goto dor_done;
  }
  
  //at first injection
  if(in_channel<gK){
    f->ph = 0;
    if(debug)
      {
	*gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		    <<f->id<<" Injected "<<endl;}
    //already in the same X
    if(r_x_location == dest_x_location){
      if(debug)
	{
	  *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		      <<f->id<<" Switching to Y"<<endl;}
      f->intm = -1;
      f->ph = 1;
    } else {
      f->intm = (xcount * xrouter) * (yrouter * r_y_location) + (xrouter * dest_x_location) ;
    }
  }

  intm = mecs_transformation(f->intm);
  intm_router = (int)(intm/gK);
  if(f->ph == 0){
    //switch over to Y dmension
    if(rID == intm_router){
      if(debug)
	{
	  *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		      <<f->id<<" Switching to Y"<<endl;}
      f->intm = -1;
      f->ph = 1;
    } else {
      assert(in_channel<gK);//should only get here on injection
      if(dest_x_location>r_x_location){ //east
	out_port = gK+1;
      } else { //west
	out_port = gK+3;
      }
      if(debug)
	{
	  *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		      <<f->id<<" Routing in X direction"<<out_port<<endl;}
      goto dor_done;
    }
  } 

  if(f->ph== 1){
    if(dest_y_location>r_y_location){ //south
      out_port = gK+2;
    } else {//north
      out_port = gK;
    }
    if(debug)
      {
	*gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		    <<f->id<<" Routing in Y direction"<<out_port<<endl;}
    goto dor_done;
  }
  

 dor_done:
  if (f->type == Flit::READ_REQUEST)
    outputs->AddRange( out_port, gReadReqBeginVC, gReadReqEndVC );
  if (f->type == Flit::WRITE_REQUEST)
    outputs->AddRange( out_port, gWriteReqBeginVC, gWriteReqEndVC );
  if (f->type ==  Flit::READ_REPLY)
    outputs->AddRange( out_port, gReadReplyBeginVC, gReadReplyEndVC );
  if (f->type ==  Flit::WRITE_REPLY)
    outputs->AddRange( out_port, gWriteReplyBeginVC, gWriteReplyEndVC );
  if (f->type ==  Flit::ANY_TYPE)
    outputs->AddRange( out_port, 0, gNumVCS-1 );
}

int mecs_transformation(int dest){
  //the magic of destination transformation

  //destination transformation, translate how the nodes are actually arranged
  //to the easier way of routing
  //this transformation only support 64 nodes

  //cout<<"ORiginal destination "<<dest<<endl;
  //router in the x direction = find which column, and then mod by cY to find 
  //which horizontal router
  int horizontal = (dest%(xcount*xrouter))/(xrouter);
  int horizontal_rem = (dest%(xcount*xrouter))%(xrouter);
  //router in the y direction = find which row, and then divided by cX to find 
  //vertical router
  int vertical = (dest/(xcount*xrouter))/(yrouter);
  int vertical_rem = (dest/(xcount*xrouter))%(yrouter);
  //transform the destination to as if node0 was 0,1,2,3 and so forth
  dest = (vertical*xcount + horizontal)*gC+xrouter*vertical_rem+horizontal_rem;
  //cout<<"Transformed destination "<<dest<<endl<<endl;
  return dest;
}
