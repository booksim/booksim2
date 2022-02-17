// $Id$

/*
 Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
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

//Flattened butterfly simulator
//Created by John Kim
//
//Updated 11/6/2007 by Ted Jiang, now scales 
//with any n such that N = K^3, k is a power of 2
//however, the change restrict it to a 2D FBfly
//
//updated sometimes in december by Ted Jiang, now works for updat to 4
//dimension. 
//
//Updated 2/4/08 by Ted Jiang disabling partial networks
//change concentrations
//
//More update 3/31/08 to correctly assign the nodes to the routers
//UGAL now has added a "mapping" to account for this new assignment
//of the nodes to the routers
//
//Updated by mihelog   27 Aug to add progressive choice of intermediate destination.
//Also, half of the total vcs are used for non-minimal routing, others for minimal (for UGAL and valiant).


#include "booksim.hpp"
#include <vector>
#include <sstream>
#include <limits>
#include <cmath>
#include "flatfly_onchip.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "globals.hpp"



//#define DEBUG_FLATFLY


FlatFlyOnChip::FlatFlyOnChip( const Configuration &config, const string & name, Module * clock, CreditBox *credits ) :
  Network( config, name, clock, credits )
{

  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}

void FlatFlyOnChip::_ComputeSize( const Configuration &config )
{
  _k = config.GetInt( "k" );	// # of routers per dimension
  _n = config.GetInt( "n" );	// dimension
  _c = config.GetInt( "c" );    //concentration, may be different from k
  _r = _c + (_k-1)*_n ;		// total radix of the switch  ( # of inputs/outputs)

  //how many routers in the x or y direction
  _xcount = config.GetInt("x");
  _ycount = config.GetInt("y");
  assert(_xcount == _ycount);
  //configuration of hohw many clients in X and Y per router
  _xrouter = config.GetInt("xr");
  _yrouter = config.GetInt("yr");
  assert(_xrouter == _yrouter);
  _radix = _k; 
  _dim = _n;
  _conc = _c;
  
  assert(_c == _xrouter*_yrouter);

  _nodes = powi( _k, _n )*_c;   //network size

  _num_of_switch = _nodes / _c;
  _channels = _num_of_switch * (_r - _c); 
  _size = _num_of_switch;

}

void FlatFlyOnChip::_BuildNet( const Configuration &config )
{
  int _output;

  ostringstream router_name;

  
  if(gTrace){

    cout<<"Setup Finished Router"<<endl;
    
  }

  //latency type, noc or conventional network
  bool use_noc_latency;
  use_noc_latency = (config.GetInt("use_noc_latency")==1);
  
  cout << " Flat Bufferfly " << endl;
  cout << " k = " << _k << " n = " << _n << " c = "<<_c<< endl;
  cout << " each switch - total radix =  "<< _r << endl;
  cout << " # of switches = "<<  _num_of_switch << endl;
  cout << " # of channels = "<<  _channels << endl;
  cout << " # of nodes ( size of network ) = " << _nodes << endl;

  for ( int node = 0; node < _num_of_switch; ++node ) {

    router_name << "router";
    router_name << "_" <<  node ;

    _routers[node] = Router::NewRouter( config, this, router_name.str( ), 
					node, _r, _r, _credits );
    _timed_modules.push_back(_routers[node]);


#ifdef DEBUG_FLATFLY
    cout << " ======== router node : " << node << " ======== " << " router_" << router_name.str() << " router node # : " << node << endl;
#endif

    router_name.str("");

    //******************************************************************
    // add inject/eject channels connected to the processor nodes
    //******************************************************************
    
    //as accurately model the length of these channels as possible
    int yleng = -_yrouter/2;
    int xleng = -_xrouter/2;
    bool yodd = _yrouter%2==1;
    bool xodd = _xrouter%2==1;
    
    int y_index = node/(_xcount);
    int x_index = node%(_xcount);
    //estimating distance from client to router
    for (int y = 0; y < _yrouter ; y++) {
      for (int x = 0; x < _xrouter ; x++) {
	//Zero is a naughty number
	if(yleng == 0 && !yodd){
	  yleng++;
	}
	if(xleng == 0 && !xodd){
	  xleng++;
	}
	int ileng = 1;   //at least 1 away
	//measure distance in the y direction
	if(abs(yleng)>1){
	  ileng+=(abs(yleng)-1);
	}
	//measure distance in the x direction
	if(abs(xleng)>1){
	  ileng+=(abs(xleng)-1);
	}
	//increment for the next client, add Y, if full, reset y add x
	yleng++;
	if(yleng>_yrouter/2){
	  yleng= -_yrouter/2;
	  xleng++;
	}
	//adopted from the CMESH, the first node has 0,1,8,9 (as an example)
	int link = (_xcount * _xrouter) * (_yrouter * y_index + y) + (_xrouter * x_index + x) ;

	if(use_noc_latency){
	  _inject[link]->SetLatency(ileng);
	  _inject_cred[link]->SetLatency(ileng);
	  _eject[link] ->SetLatency(ileng);
	  _eject_cred[link]->SetLatency(ileng);
	} else {
	  _inject[link]->SetLatency(1);
	  _inject_cred[link]->SetLatency(1);
	  _eject[link] ->SetLatency(1);
	  _eject_cred[link]->SetLatency(1);
	}
	_routers[node]->AddInputChannel( _inject[link], _inject_cred[link] );
	
#ifdef DEBUG_FLATFLY
	cout << "  Adding injection channel " << link << endl;
#endif
	
	_routers[node]->AddOutputChannel( _eject[link], _eject_cred[link] );
#ifdef DEBUG_FLATFLY
	cout << "  Adding ejection channel " << link << endl;
#endif
      }
    }
  }
  //******************************************************************
  // add output inter-router channels
  //******************************************************************

  //for every router, in every dimension
  for ( int node = 0; node < _num_of_switch; ++node ) {
    for ( int dim = 0; dim < _n; ++dim ) {

      //locate itself in every dimension
      int xcurr = node%_k;
      int ycurr = (int)(node/_k);
      int curr3 = node%(_k*_k);
      int curr4 = (int)(node/(_k*_k));
      int curr5 = (int)(node/(_k*_k*_k));//mmm didn't mean to be racist
      int curr6 = (node%(_k*_k*_k));//mmm didn't mean to be racist

      //for every other router in the dimension
      for ( int cnt = 0; cnt < (_k ); ++cnt ) {	
	int other=0; //the other router that we are trying to connect
	int offset = 0; //silly ness when node< other or when node>other
	//if xdimension
	if(dim == 0){
	  other = ycurr * _k +cnt;
	} else if (dim ==1){
	  other = cnt * _k + xcurr;
	  if(_n==3){
	    other+= curr4*_k*_k;
	  } 
	  if(_n==4){
	    curr4=((int)(node/(_k*_k)))%_k;
	    other+= curr4*_k*_k+curr5*_k*_k*_k;
	  }
	}else if (dim ==2){
	  other = cnt*_k*_k + curr3;
	  if(_n==4){
	    other+= curr5*_k*_k*_k;
	  }
	}else if (dim ==3){
	  other = cnt*_k*_k*_k+curr6;
	}
	assert(dim < 4);
	if(other == node){
#ifdef DEBUG_FLATFLY
	  cout << "ignore channel : " << _output << " to node " << node <<" and "<<other<<endl;
#endif
	  continue;
	}
	//calculate channel length
	int length = 0;
	int oned = abs((node%_xcount)-(other%_xcount));
	int twod = abs(node/_xcount-other/_xcount);
	length = _xrouter*oned + _yrouter *twod;
	//oh the node<other silly ness
	if(node<other){
	  offset = -1;
	}
	//node, dimension, router within dimension. Good luck understanding this
	_output = (_k-1) * _n  * node + (_k-1) * dim  + cnt+offset;
	
	
#ifdef DEBUG_FLATFLY
	cout << "Adding channel : " << _output << " to node " << node <<" and "<<other<<" with length "<<length<<endl;
#endif
	if(use_noc_latency){
	  _chan[_output]->SetLatency(length);
	  _chan_cred[_output]->SetLatency(length);
	} else {
	  _chan[_output]->SetLatency(1);
	  _chan_cred[_output]->SetLatency(1);
	}
	_routers[node]->AddOutputChannel( _chan[_output], _chan_cred[_output] );
	
	_routers[other]->AddInputChannel( _chan[_output], _chan_cred[_output]);
	
	if(gTrace){
	  cout<<"Link "<<_output<<" "<<node<<" "<<other<<" "<<length<<endl;
	}
	
      }
    }
  }
  if(gTrace){
    cout<<"Setup Finished Link"<<endl;
  }
}


int FlatFlyOnChip::GetN( ) const
{
  return _n;
}

int FlatFlyOnChip::GetK( ) const
{
  return _k;
}

void FlatFlyOnChip::InsertRandomFaults( const Configuration &config )
{

}

double FlatFlyOnChip::Capacity( ) const
{
  return (double)_k / 8.0;
}


void FlatFlyOnChip::RegisterRoutingFunctions(){

  
  gRoutingFunctionMap["ran_min_flatfly"] = &min_flatfly;
  gRoutingFunctionMap["adaptive_xyyx_flatfly"] = &adaptive_xyyx_flatfly;
  gRoutingFunctionMap["xyyx_flatfly"] = &xyyx_flatfly;
  gRoutingFunctionMap["valiant_flatfly"] = &valiant_flatfly;
  gRoutingFunctionMap["ugal_flatfly"] = &ugal_flatfly_onchip;
  gRoutingFunctionMap["ugal_pni_flatfly"] = &ugal_pni_flatfly_onchip;
  gRoutingFunctionMap["ugal_xyyx_flatfly"] = &ugal_xyyx_flatfly_onchip;

}

//The initial XY or YX minimal routing direction is chosen adaptively
void adaptive_xyyx_flatfly( const Router *r, const Flit *f, int in_channel, 
		  OutputSet *outputs, bool inject, RoutingConfig *rc )
{ 
  // ( Traffic Class , Routing Order ) -> Virtual Channel Range
  int vcBegin = 0, vcEnd = rc->NumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = rc->ReadReqBeginVC;
    vcEnd = rc->ReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = rc->WriteReqBeginVC;
    vcEnd = rc->WriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = rc->ReadReplyBeginVC;
    vcEnd = rc->ReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = rc->WriteReplyBeginVC;
    vcEnd = rc->WriteReplyEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  int out_port;

  if(inject) {

    out_port = -1;

  } else {
    /**
     * This dynamic_cast is Bad. 
     * However the legacy code is using a constant interface of `tRoutingFunction`
     * for handling routing of every single networks type Booksim2 supports,
     * leaving no other options without back pain.
     */
    const FlatFlyOnChip *net = dynamic_cast<const FlatFlyOnChip *>(r->GetOWner());
    if(!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }
    int dest = net->flatfly_transformation(f->dest);
    int targetr = (int)(dest/net->GetConc());

    if(targetr==r->GetID()){ //if we are at the final router, yay, output to client
      out_port = dest % net->GetConc();

    } else {

      //each class must have at least 2 vcs assigned or else xy_yx will deadlock
      int const available_vcs = (vcEnd - vcBegin + 1) / 2;
      assert(available_vcs > 0);

      int out_port_xy =  net->flatfly_outport(dest, r->GetID());
      int out_port_yx =  net->flatfly_outport_yx(dest, r->GetID());

      // Route order (XY or YX) determined when packet is injected
      //  into the network, adaptively
      bool x_then_y;
      if(in_channel < net->GetConc()){
	int credit_xy = r->GetUsedCredit(out_port_xy);
	int credit_yx = r->GetUsedCredit(out_port_yx);
	if(credit_xy > credit_yx) {
	  x_then_y = false;
	} else if(credit_xy < credit_yx) {
	  x_then_y = true;
	} else {
	  x_then_y = (RandomInt(1) > 0);
	}
      } else {
	x_then_y =  (f->vc < (vcBegin + available_vcs));
      }
      
      if(x_then_y) {
	out_port = out_port_xy;
	vcEnd -= available_vcs;
      } else {
	out_port = out_port_yx;
	vcBegin += available_vcs;
      }
    }

  }

  outputs->Clear( );

  outputs->AddRange( out_port , vcBegin, vcEnd );
}

//The initial XY or YX minimal routing direction is chosen randomly
void xyyx_flatfly( const Router *r, const Flit *f, int in_channel, 
		  OutputSet *outputs, bool inject, RoutingConfig *rc )
{ 
  // ( Traffic Class , Routing Order ) -> Virtual Channel Range
  int vcBegin = 0, vcEnd = rc->NumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = rc->ReadReqBeginVC;
    vcEnd = rc->ReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = rc->WriteReqBeginVC;
    vcEnd = rc->WriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = rc->ReadReplyBeginVC;
    vcEnd = rc->ReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = rc->WriteReplyBeginVC;
    vcEnd = rc->WriteReplyEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  int out_port;

  if(inject) {

    out_port = -1;

  } else {
    /**
     * This dynamic_cast is Bad. 
     * However the legacy code is using a constant interface of `tRoutingFunction`
     * for handling routing of every single networks type Booksim2 supports,
     * leaving no other options without back pain.
     */
    const FlatFlyOnChip *net = dynamic_cast<const FlatFlyOnChip *>(r->GetOWner());
    if(!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }
    int dest = net->flatfly_transformation(f->dest);
    int targetr = (int)(dest/net->GetConc());

    if(targetr==r->GetID()){ //if we are at the final router, yay, output to client
      out_port = dest % net->GetConc();

    } else {
   
      //each class must have at least 2 vcs assigned or else xy_yx will deadlock
      int const available_vcs = (vcEnd - vcBegin + 1) / 2;
      assert(available_vcs > 0);

      // randomly select dimension order at first hop
      bool x_then_y = ((in_channel < net->GetConc()) ?
		       (RandomInt(1) > 0) : 
		       (f->vc < (vcBegin + available_vcs)));

      if(x_then_y) {
	out_port = net->flatfly_outport(dest, r->GetID());
	vcEnd -= available_vcs;
      } else {
	out_port = net->flatfly_outport_yx(dest, r->GetID());
	vcBegin += available_vcs;
      }
    }

  }

  outputs->Clear( );

  outputs->AddRange( out_port , vcBegin, vcEnd );
}

int FlatFlyOnChip::flatfly_outport_yx(int dest, int rID) const {
  int dest_rID = (int) (dest / _conc);
  int output = -1, dID, sID;
  
  if(dest_rID==rID){
    return dest % _conc;
  }

  for (int d=_dim-1;d >= 0; d--) {
    int power = powi(_radix,d);
    dID = int(dest_rID / power);
    sID = int(rID / power);
    if ( dID != sID ) {
      output = _conc + ((_radix-1)*d) - 1;
      if (dID > sID) {
	output += dID;
      } else {
	output += dID + 1;
      }
      return output;
    }
    dest_rID = (int) (dest_rID %power);
    rID      = (int) (rID %power);
  }
  if (output == -1) {
    cout << " ERROR ---- FLATFLY_OUTPORT function : output not found yx" << endl;
    exit(-1);
  }
  return -1;
}

void valiant_flatfly( const Router *r, const Flit *f, int in_channel, 
		  OutputSet *outputs, bool inject, RoutingConfig *rc )
{
  // ( Traffic Class , Routing Order ) -> Virtual Channel Range
  int vcBegin = 0, vcEnd = rc->NumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = rc->ReadReqBeginVC;
    vcEnd = rc->ReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = rc->WriteReqBeginVC;
    vcEnd = rc->WriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = rc->ReadReplyBeginVC;
    vcEnd = rc->ReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = rc->WriteReplyBeginVC;
    vcEnd = rc->WriteReplyEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  int out_port;

  if(inject) {

    out_port = -1;

  } else {
    /**
     * This dynamic_cast is Bad. 
     * However the legacy code is using a constant interface of `tRoutingFunction`
     * for handling routing of every single networks type Booksim2 supports,
     * leaving no other options without back pain.
     */
    const FlatFlyOnChip *net = dynamic_cast<const FlatFlyOnChip *>(r->GetOWner());
    if(!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }
    if ( in_channel < net->GetConc() ){
      f->ph = 0;
      f->intm = RandomInt( powi( net->GetRadix(), net->GetDim() )*net->GetConc()-1);
    }

    int intm = net->flatfly_transformation(f->intm);
    int dest = net->flatfly_transformation(f->dest);

    if((int)(intm/net->GetConc()) == r->GetID() || (int)(dest/net->GetConc())== r->GetID()){
      f->ph = 1;
    }

    if(f->ph == 0) {
      out_port = net->flatfly_outport(intm, r->GetID());
    } else {
      assert(f->ph == 1);
      out_port = net->flatfly_outport(dest, r->GetID());
    }

    if((int)(dest/net->GetConc()) != r->GetID()) {

      //each class must have at least 2 vcs assigned or else valiant valiant will deadlock
      int const available_vcs = (vcEnd - vcBegin + 1) / 2;
      assert(available_vcs > 0);

      if(f->ph == 0) {
	vcEnd -= available_vcs;
      } else {
	// If routing to final destination use the second half of the VCs.
	assert(f->ph == 1);
	vcBegin += available_vcs;
      }
    }

  }

  outputs->Clear( );

  outputs->AddRange( out_port , vcBegin, vcEnd );
}

void min_flatfly( const Router *r, const Flit *f, int in_channel, 
		  OutputSet *outputs, bool inject, RoutingConfig *rc )
{
  // ( Traffic Class , Routing Order ) -> Virtual Channel Range
  int vcBegin = 0, vcEnd = rc->NumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = rc->ReadReqBeginVC;
    vcEnd = rc->ReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = rc->WriteReqBeginVC;
    vcEnd = rc->WriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = rc->ReadReplyBeginVC;
    vcEnd = rc->ReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = rc->WriteReplyBeginVC;
    vcEnd = rc->WriteReplyEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  int out_port;

  if(inject) {

    out_port = -1;

  } else {
    /**
     * This dynamic_cast is Bad. 
     * However the legacy code is using a constant interface of `tRoutingFunction`
     * for handling routing of every single networks type Booksim2 supports,
     * leaving no other options without back pain.
     */
    const FlatFlyOnChip *net = dynamic_cast<const FlatFlyOnChip *>(r->GetOWner());
    if(!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }
    int dest  = net->flatfly_transformation(f->dest);
    int targetr= (int)(dest/net->GetConc());
    //int xdest = ((int)(dest/net->GetConc())) % net->GetRadix();
    //int xcurr = ((r->GetID())) % net->GetRadix();

    //int ydest = ((int)(dest/net->GetConc())) / net->GetRadix();
    //int ycurr = ((r->GetID())) / net->GetRadix();

    if(targetr==r->GetID()){ //if we are at the final router, yay, output to client
      out_port = dest % net->GetConc();
    } else{ //else select a dimension at random
      out_port = net->flatfly_outport(dest, r->GetID());
    }

  }

  outputs->Clear( );

  outputs->AddRange( out_port , vcBegin, vcEnd );
}

//=============================================================^M
// route UGAL in the flattened butterfly
//=============================================================^M


//same as ugal except uses xyyx routing
void ugal_xyyx_flatfly_onchip( const Router *r, const Flit *f, int in_channel,
			  OutputSet *outputs, bool inject, RoutingConfig *rc )
{
  // ( Traffic Class , Routing Order ) -> Virtual Channel Range
  int vcBegin = 0, vcEnd = rc->NumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = rc->ReadReqBeginVC;
    vcEnd = rc->ReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = rc->WriteReqBeginVC;
    vcEnd = rc->WriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = rc->ReadReplyBeginVC;
    vcEnd = rc->ReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = rc->WriteReplyBeginVC;
    vcEnd = rc->WriteReplyEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  int out_port;

  if(inject) {

    out_port = -1;

  } else {
    /**
     * This dynamic_cast is Bad. 
     * However the legacy code is using a constant interface of `tRoutingFunction`
     * for handling routing of every single networks type Booksim2 supports,
     * leaving no other options without back pain.
     */
    const FlatFlyOnChip *net = dynamic_cast<const FlatFlyOnChip *>(r->GetOWner());
    if(!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }
    int dest  = net->flatfly_transformation(f->dest);

    int rID =  r->GetID();
    int _concentration = net->GetConc();
    int _radix = net->GetRadix();
    int _dimension = net->GetDim();
    int found;
    int debug = 0;
    int tmp_out_port, _ran_intm;
    int _min_hop, _nonmin_hop, _min_queucnt, _nonmin_queucnt;
    int threshold = 2;


    if ( in_channel < _concentration ){
      if(gTrace){
	cout<<"New Flit "<<f->src<<endl;
      }
      f->ph   = 0;
    }

    if(gTrace){
      int load = 0;
      cout<<"Router "<<rID<<endl;
      cout<<"Input Channel "<<in_channel<<endl;
      //need to modify router to report the buffere depth
      load +=r->GetBufferOccupancy(in_channel);
      cout<<"Rload "<<load<<endl;
    }

    if (debug){
      cout << " FLIT ID: " << f->id << " Router: " << rID << " routing from src : " << f->src <<  " to dest : " << dest << " f->ph: " <<f->ph << " intm: " << f->intm <<  endl;
    }
    // f->ph == 0  ==> make initial global adaptive decision
    // f->ph == 1  ==> route nonminimaly to random intermediate node
    // f->ph == 2  ==> route minimally to destination

    found = 0;

    if (f->ph == 1){
      dest = f->intm;
    }

    if (dest >= rID*_concentration && dest < (rID+1)*_concentration) {
      if (f->ph == 1) {
	f->ph = 2;
	dest = net->flatfly_transformation(f->dest);
	if (debug)   cout << "      done routing to intermediate ";
      }
      else  {
	found = 1;
	out_port = dest % _concentration;
	if (debug)   cout << "      final routing to destination ";
      }
    }

    if (!found) {

      int const xy_available_vcs = (vcEnd - vcBegin + 1) / 2;
      assert(xy_available_vcs > 0);

      // randomly select dimension order at first hop
      bool x_then_y = ((in_channel < _concentration) ?
		       (RandomInt(1) > 0) : 
		       (f->vc < (vcBegin + xy_available_vcs)));

      if (f->ph == 0) {
	//find the min port and min distance
	_min_hop = net->find_distance(net->flatfly_transformation(f->src),dest);
	if(x_then_y){
	  tmp_out_port =  net->flatfly_outport(dest, rID);
	} else {
	  tmp_out_port =  net->flatfly_outport_yx(dest, rID);
	}
	if (f->watch){
	  cout << " MIN tmp_out_port: " << tmp_out_port;
	}
	//sum over all vcs of that port
	_min_queucnt =   r->GetUsedCredit(tmp_out_port);

	//find the nonmin router, nonmin port, nonmin count
	_ran_intm = net->find_ran_intm(net->flatfly_transformation(f->src), dest);
	_nonmin_hop = net->find_distance(net->flatfly_transformation(f->src),_ran_intm) + net->find_distance(_ran_intm, dest);
	if(x_then_y){
	  tmp_out_port =  net->flatfly_outport(_ran_intm, rID);
	} else {
	  tmp_out_port =  net->flatfly_outport_yx(_ran_intm, rID);
	}

	if (f->watch){
	  cout << " NONMIN tmp_out_port: " << tmp_out_port << endl;
	}
	if (_ran_intm >= rID*_concentration && _ran_intm < (rID+1)*_concentration) {
	  _nonmin_queucnt = numeric_limits<int>::max();
	} else  {
	  _nonmin_queucnt =   r->GetUsedCredit(tmp_out_port);
	}

	if (debug){
	  cout << " _min_hop " << _min_hop << " _min_queucnt: " <<_min_queucnt << " _nonmin_hop: " << _nonmin_hop << " _nonmin_queucnt :" << _nonmin_queucnt <<  endl;
	}

	if (_min_hop * _min_queucnt   <= _nonmin_hop * _nonmin_queucnt +threshold) {

	  if (debug) cout << " Route MINIMALLY " << endl;
	  f->ph = 2;
	} else {
	  // route non-minimally
	  if (debug)  { cout << " Route NONMINIMALLY int node: " <<_ran_intm << endl; }
	  f->ph = 1;
	  f->intm = _ran_intm;
	  dest = f->intm;
	  if (dest >= rID*_concentration && dest < (rID+1)*_concentration) {
	    f->ph = 2;
	    dest = net->flatfly_transformation(f->dest);
	  }
	}
      }

      //dest here should be == intm if ph==1, or dest == dest if ph == 2
      if(x_then_y){
	out_port =  net->flatfly_outport(dest, rID);
	if(out_port >= _concentration) {
	  vcEnd -= xy_available_vcs;
	}
      } else {
	out_port =  net->flatfly_outport_yx(dest, rID);
	if(out_port >= _concentration) {
	  vcBegin += xy_available_vcs;
	}
      }

      // if we haven't reached our destination, restrict VCs appropriately to avoid routing deadlock
      if(out_port >= _concentration) {

	int const ph_available_vcs = xy_available_vcs / 2;
	assert(ph_available_vcs > 0);

	if(f->ph == 1) {
	  vcEnd -= ph_available_vcs;
	} else {
	  assert(f->ph == 2);
	  vcBegin += ph_available_vcs;
	}
      }

      found = 1;
    }

    if (!found) {
      cout << " ERROR: output not found in routing. " << endl;
      cout << *f; exit (-1);
    }

    if (out_port >= _dimension*(_radix-1) + _concentration)  {
      cout << " ERROR: output port too big! " << endl;
      cout << " OUTPUT select: " << out_port << endl;
      cout << " router radix: " <<  _dimension*(_radix-1) + _radix << endl;
      exit (-1);
    }

    if (debug) cout << "        through output port : " << out_port << endl;
    if(gTrace){cout<<"Outport "<<out_port<<endl;cout<<"Stop Mark"<<endl;}

  }

  outputs->Clear( );

  outputs->AddRange( out_port , vcBegin, vcEnd );
}



//ugal now uses modified comparison, modefied getcredit
void ugal_flatfly_onchip( const Router *r, const Flit *f, int in_channel,
			  OutputSet *outputs, bool inject, RoutingConfig *rc )
{
  // ( Traffic Class , Routing Order ) -> Virtual Channel Range
  int vcBegin = 0, vcEnd = rc->NumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = rc->ReadReqBeginVC;
    vcEnd = rc->ReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = rc->WriteReqBeginVC;
    vcEnd = rc->WriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = rc->ReadReplyBeginVC;
    vcEnd = rc->ReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = rc->WriteReplyBeginVC;
    vcEnd = rc->WriteReplyEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  int out_port;

  if(inject) {

    out_port = -1;

  } else {
    /**
     * This dynamic_cast is Bad. 
     * However the legacy code is using a constant interface of `tRoutingFunction`
     * for handling routing of every single networks type Booksim2 supports,
     * leaving no other options without back pain.
     */
    const FlatFlyOnChip *net = dynamic_cast<const FlatFlyOnChip *>(r->GetOWner());
    if(!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }
    int dest  = net->flatfly_transformation(f->dest);

    int rID =  r->GetID();
    int _concentration = net->GetConc();
    int _radix = net->GetRadix();
    int _dimension = net->GetDim();
    int found;
    int debug = 0;
    int tmp_out_port, _ran_intm;
    int _min_hop, _nonmin_hop, _min_queucnt, _nonmin_queucnt;
    int threshold = 2;

    if ( in_channel < _concentration ){
      if(gTrace){
	cout<<"New Flit "<<f->src<<endl;
      }
      f->ph   = 0;
    }

    if(gTrace){
      int load = 0;
      cout<<"Router "<<rID<<endl;
      cout<<"Input Channel "<<in_channel<<endl;
      //need to modify router to report the buffere depth
      load +=r->GetBufferOccupancy(in_channel);
      cout<<"Rload "<<load<<endl;
    }

    if (debug){
      cout << " FLIT ID: " << f->id << " Router: " << rID << " routing from src : " << f->src <<  " to dest : " << dest << " f->ph: " <<f->ph << " intm: " << f->intm <<  endl;
    }
    // f->ph == 0  ==> make initial global adaptive decision
    // f->ph == 1  ==> route nonminimaly to random intermediate node
    // f->ph == 2  ==> route minimally to destination

    found = 0;

    if (f->ph == 1){
      dest = f->intm;
    }


    if (dest >= rID*_concentration && dest < (rID+1)*_concentration) {

      if (f->ph == 1) {
	f->ph = 2;
	dest = net->flatfly_transformation(f->dest);
	if (debug)   cout << "      done routing to intermediate ";
      }
      else  {
	found = 1;
	out_port = dest % _concentration;
	if (debug)   cout << "      final routing to destination ";
      }
    }

    if (!found) {

      if (f->ph == 0) {
	_min_hop = net->find_distance(net->flatfly_transformation(f->src),dest);
	_ran_intm = net->find_ran_intm(net->flatfly_transformation(f->src), dest);
	tmp_out_port =  net->flatfly_outport(dest, rID);
	if (f->watch){
	  *gWatchOut << r->GetSimTime() << " | " << r->FullName() << " | "
		     << " MIN tmp_out_port: " << tmp_out_port;
	}

	_min_queucnt =   r->GetUsedCredit(tmp_out_port);

	_nonmin_hop = net->find_distance(net->flatfly_transformation(f->src),_ran_intm) + net->find_distance(_ran_intm, dest);
	tmp_out_port = net->flatfly_outport(_ran_intm, rID);

	if (f->watch){
	  *gWatchOut << r->GetSimTime() << " | " << r->FullName() << " | "
		     << " NONMIN tmp_out_port: " << tmp_out_port << endl;
	}
	if (_ran_intm >= rID*_concentration && _ran_intm < (rID+1)*_concentration) {
	  _nonmin_queucnt = numeric_limits<int>::max();
	} else  {
	  _nonmin_queucnt =   r->GetUsedCredit(tmp_out_port);
	}

	if (debug){
	  cout << " _min_hop " << _min_hop << " _min_queucnt: " <<_min_queucnt << " _nonmin_hop: " << _nonmin_hop << " _nonmin_queucnt :" << _nonmin_queucnt <<  endl;
	}

	if (_min_hop * _min_queucnt   <= _nonmin_hop * _nonmin_queucnt +threshold) {

	  if (debug) cout << " Route MINIMALLY " << endl;
	  f->ph = 2;
	} else {
	  // route non-minimally
	  if (debug)  { cout << " Route NONMINIMALLY int node: " <<_ran_intm << endl; }
	  f->ph = 1;
	  f->intm = _ran_intm;
	  dest = f->intm;
	  if (dest >= rID*_concentration && dest < (rID+1)*_concentration) {
	    f->ph = 2;
	    dest = net->flatfly_transformation(f->dest);
	  }
	}
      }

      // find minimal correct dimension to route through
      out_port =  net->flatfly_outport(dest, rID);

      // if we haven't reached our destination, restrict VCs appropriately to avoid routing deadlock
      if(out_port >= _concentration) {
	int const available_vcs = (vcEnd - vcBegin + 1) / 2;
	assert(available_vcs > 0);
	if(f->ph == 1) {
	  vcEnd -= available_vcs;
	} else {
	  assert(f->ph == 2);
	  vcBegin += available_vcs;
	}
      }

      found = 1;
    }

    if (!found) {
      cout << " ERROR: output not found in routing. " << endl;
      cout << *f; exit (-1);
    }

    if (out_port >= _dimension*(_radix-1) + _concentration)  {
      cout << " ERROR: output port too big! " << endl;
      cout << " OUTPUT select: " << out_port << endl;
      cout << " router radix: " <<  _dimension*(_radix-1) + _radix << endl;
      exit (-1);
    }

    if (debug) cout << "        through output port : " << out_port << endl;
    if(gTrace) {
      cout<<"Outport "<<out_port<<endl;
      cout<<"Stop Mark"<<endl;
    }
  }

  outputs->Clear( );

  outputs->AddRange( out_port , vcBegin, vcEnd );
}


// partially non-interfering (i.e., packets ordered by hash of destination) UGAL
void ugal_pni_flatfly_onchip( const Router *r, const Flit *f, int in_channel,
			      OutputSet *outputs, bool inject, RoutingConfig *rc )
{
  // ( Traffic Class , Routing Order ) -> Virtual Channel Range
  int vcBegin = 0, vcEnd = rc->NumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = rc->ReadReqBeginVC;
    vcEnd = rc->ReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = rc->WriteReqBeginVC;
    vcEnd = rc->WriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = rc->ReadReplyBeginVC;
    vcEnd = rc->ReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = rc->WriteReplyBeginVC;
    vcEnd = rc->WriteReplyEndVC;
  }
  assert(((f->vc >= vcBegin) && (f->vc <= vcEnd)) || (inject && (f->vc < 0)));

  int out_port, _concentration = 0, _radix = 0, _dim = 0;
  /**
   * This dynamic_cast is Bad. 
   * However the legacy code is using a constant interface of `tRoutingFunction`
   * for handling routing of every single networks type Booksim2 supports,
   * leaving no other options without back pain.
   */
  const FlatFlyOnChip *net = dynamic_cast<const FlatFlyOnChip *>(r->GetOWner());
  if(net)
  {
    _concentration = net->GetConc();
    _radix = net->GetRadix();
    _dim = net->GetDim();
  } else {
    r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
  }


  if(inject) {
    
    out_port = -1;

  } else {

    int dest  = net->flatfly_transformation(f->dest);

    int rID =  r->GetID();
    int found;
    int debug = 0;
    int tmp_out_port, _ran_intm;
    int _min_hop, _nonmin_hop, _min_queucnt, _nonmin_queucnt;
    int threshold = 2;

    if ( in_channel < _concentration ){
      if(gTrace){
	cout<<"New Flit "<<f->src<<endl;
      }
      f->ph   = 0;
    }

    if(gTrace){
      int load = 0;
      cout<<"Router "<<rID<<endl;
      cout<<"Input Channel "<<in_channel<<endl;
      //need to modify router to report the buffere depth
      load +=r->GetBufferOccupancy(in_channel);
      cout<<"Rload "<<load<<endl;
    }

    if (debug){
      cout << " FLIT ID: " << f->id << " Router: " << rID << " routing from src : " << f->src <<  " to dest : " << dest << " f->ph: " <<f->ph << " intm: " << f->intm <<  endl;
    }
    // f->ph == 0  ==> make initial global adaptive decision
    // f->ph == 1  ==> route nonminimaly to random intermediate node
    // f->ph == 2  ==> route minimally to destination

    found = 0;

    if (f->ph == 1){
      dest = f->intm;
    }


    if (dest >= rID*_concentration && dest < (rID+1)*_concentration) {

      if (f->ph == 1) {
	f->ph = 2;
	dest = net->flatfly_transformation(f->dest);
	if (debug)   cout << "      done routing to intermediate ";
      }
      else  {
	found = 1;
	out_port = dest % _concentration;
	if (debug)   cout << "      final routing to destination ";
      }
    }

    if (!found) {

      if (f->ph == 0) {
	_min_hop = net->find_distance(net->flatfly_transformation(f->src),dest);
	_ran_intm = net->find_ran_intm(net->flatfly_transformation(f->src), dest);
	tmp_out_port =  net->flatfly_outport(dest, rID);
	if (f->watch){
	  *gWatchOut << r->GetSimTime() << " | " << r->FullName() << " | "
		     << " MIN tmp_out_port: " << tmp_out_port;
	}

	_min_queucnt =   r->GetUsedCredit(tmp_out_port);

	_nonmin_hop = net->find_distance(net->flatfly_transformation(f->src),_ran_intm) + net->find_distance(_ran_intm, dest);
	tmp_out_port =  net->flatfly_outport(_ran_intm, rID);

	if (f->watch){
	  *gWatchOut << r->GetSimTime() << " | " << r->FullName() << " | "
		     << " NONMIN tmp_out_port: " << tmp_out_port << endl;
	}
	if (_ran_intm >= rID*_concentration && _ran_intm < (rID+1)*_concentration) {
	  _nonmin_queucnt = numeric_limits<int>::max();
	} else  {
	  _nonmin_queucnt =   r->GetUsedCredit(tmp_out_port);
	}

	if (debug){
	  cout << " _min_hop " << _min_hop << " _min_queucnt: " <<_min_queucnt << " _nonmin_hop: " << _nonmin_hop << " _nonmin_queucnt :" << _nonmin_queucnt <<  endl;
	}

	if (_min_hop * _min_queucnt   <= _nonmin_hop * _nonmin_queucnt +threshold) {

	  if (debug) cout << " Route MINIMALLY " << endl;
	  f->ph = 2;
	} else {
	  // route non-minimally
	  if (debug)  { cout << " Route NONMINIMALLY int node: " <<_ran_intm << endl; }
	  f->ph = 1;
	  f->intm = _ran_intm;
	  dest = f->intm;
	  if (dest >= rID*_concentration && dest < (rID+1)*_concentration) {
	    f->ph = 2;
	    dest = net->flatfly_transformation(f->dest);
	  }
	}
      }

      // find minimal correct dimension to route through
      out_port =  net->flatfly_outport(dest, rID);

      // if we haven't reached our destination, restrict VCs appropriately to avoid routing deadlock
      if(out_port >= _concentration) {
	int const available_vcs = (vcEnd - vcBegin + 1) / 2;
	assert(available_vcs > 0);
	if(f->ph == 1) {
	  vcEnd -= available_vcs;
	} else {
	  assert(f->ph == 2);
	  vcBegin += available_vcs;
	}
      }

      found = 1;
    }

    if (!found) {
      cout << " ERROR: output not found in routing. " << endl;
      cout << *f; exit (-1);
    }

    if (out_port >= _dim*(_radix-1) + _concentration)  {
      cout << " ERROR: output port too big! " << endl;
      cout << " OUTPUT select: " << out_port << endl;
      cout << " router radix: " <<  _dim*(_radix-1) + _radix << endl;
      exit (-1);
    }

    if (debug) cout << "        through output port : " << out_port << endl;
    if(gTrace) {
      cout<<"Outport "<<out_port<<endl;
      cout<<"Stop Mark"<<endl;
    }
  }

  if(inject || (out_port >= _concentration)) {

    // NOTE: for "proper" flattened butterfly configurations (i.e., ones 
    // derived from flattening an actual butterfly), gK and gC are the same!
    assert(_radix == _concentration);

    assert(inject ? (f->ph == -1) : (f->ph == 1 || f->ph == 2));
    int next_coord = net->flatfly_transformation(f->dest);
    if(inject) {
      next_coord /= _concentration;
      next_coord %= _radix;
    } else {
      int next_dim = (out_port - _concentration) / (_radix - 1) + 1;
      if(next_dim == _dim) {
	next_coord %= _concentration;
      } else {
	next_coord /= _concentration;
	for(int d = 0; d < next_dim; ++d) {
	  next_coord /= _radix;
	}
	next_coord %= _radix;
      }
    }
    assert(next_coord >= 0 && next_coord < _radix);
    int vcs_per_dest = (vcEnd - vcBegin + 1) / _radix;
    assert(vcs_per_dest > 0);
    vcBegin += next_coord * vcs_per_dest;
    vcEnd = vcBegin + vcs_per_dest - 1;
  }

  outputs->Clear( );

  outputs->AddRange( out_port , vcBegin, vcEnd );
}


//=============================================================^M
// UGAL : calculate distance (hop cnt)  between src and destination
//=============================================================^M
int FlatFlyOnChip::find_distance (int src, int dest) const {
  int dist = 0;
  
  int src_tmp= (int) src / _conc;
  int dest_tmp = (int) dest / _conc;
  
  //  cout << " HOP CNT between  src: " << src << " dest: " << dest;
  for (int d=0;d < _dim; d++) {
    //int _dim_size = powi(_radix, d )*_conc;
    //if ((int)(src / _dim_size) !=  (int)(dest / _dim_size))
    //   dist++;
    int src_id = src_tmp % _radix;
    int dest_id = dest_tmp % _radix;
    if (src_id !=  dest_id)
      dist++;
    
    src_tmp = (int) (src_tmp / _radix);
    dest_tmp = (int) (dest_tmp / _radix);
  }
  
  //  cout << " : " << dist << endl;
  
  return dist;
}

//=============================================================^M
// UGAL : find random node for load balancing
//=============================================================^M
int FlatFlyOnChip::find_ran_intm (int src, int dest) const {
  int _dim_size;
  int _ran_dest = 0;
  int debug = 0;
  
  if (debug) 
    cout << " INTM node for  src: " << src << " dest: " <<dest << endl;
  
  src = (int) (src / _conc);
  dest = (int) (dest / _conc);
  
  _ran_dest = RandomInt(_conc - 1);
  if (debug) cout << " ............ _ran_dest : " << _ran_dest << endl;
  for (int d=0;d < _dim; d++) {
    
    _dim_size = powi(_radix, d)*_conc;
    if ((src % _radix) ==  (dest % _radix)) {
      _ran_dest += (src % _radix) * _dim_size;
      if (debug) 
	cout << "    share same dimension : " << d << " int node : " << _ran_dest << " src ID : " << src % _radix << endl;
    } else {
      // src and dest are in the same dimension "d" + 1
      // ==> thus generate a random destination within
      _ran_dest += RandomInt(_radix - 1) * _dim_size;
      if (debug) 
	cout << "    different  dimension : " << d << " int node : " << _ran_dest << " _dim_size: " << _dim_size << endl;
    }
    src = (int) (src / _radix);
    dest = (int) (dest / _radix);
  }
  
  if (debug) cout << " intermediate destination NODE: " << _ran_dest << endl;
  return _ran_dest;
}



//=============================================================
// UGAL : calculated minimum distance output port for flatfly
// given the dimension and destination
//=============================================================
// starting from DIM 0 (x first)
int FlatFlyOnChip::flatfly_outport(int dest, int rID) const {
  int dest_rID = (int) (dest / _conc);
  int output = -1, dID, sID;
  
  if(dest_rID==rID){
    return dest % _conc;
  }


  for (int d=0;d < _dim; d++) {
    dID = (dest_rID % _radix);
    sID = (rID % _radix);
    if ( dID != sID ) {
      output = _conc + ((_radix-1)*d) - 1;
      if (dID > sID) {

	output += dID;
      } else {
	output += dID + 1;
      }
      
      return output;
    }
    dest_rID = (int) (dest_rID / _radix);
    rID      = (int) (rID / _radix);
  }
  if (output == -1) {
    cout << " ERROR ---- FLATFLY_OUTPORT function : output not found " << endl;
    exit(-1);
  }
  return -1;
}

int FlatFlyOnChip::flatfly_transformation(int dest) const {
  //the magic of destination transformation

  //destination transformation, translate how the nodes are actually arranged
  //to the easier way of routing
  //this transformation only support 64 nodes

  //cout<<"ORiginal destination "<<dest<<endl;
  //router in the x direction = find which column, and then mod by cY to find 
  //which horizontal router
  int horizontal = (dest%(_xcount*_xrouter))/(_xrouter);
  int horizontal_rem = (dest%(_xcount*_xrouter))%(_xrouter);
  //router in the y direction = find which row, and then divided by cX to find 
  //vertical router
  int vertical = (dest/(_xcount*_xrouter))/(_yrouter);
  int vertical_rem = (dest/(_xcount*_xrouter))%(_yrouter);
  //transform the destination to as if node0 was 0,1,2,3 and so forth
  dest = (vertical*_xcount + horizontal)*_conc+_xrouter*vertical_rem+horizontal_rem;
  //cout<<"Transformed destination "<<dest<<endl<<endl;
  return dest;
}
