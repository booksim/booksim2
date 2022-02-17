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

// ----------------------------------------------------------------------
//
// CMesh: Network with <Int> Terminal Nodes arranged in a concentrated
//        mesh topology
//
// ----------------------------------------------------------------------

// ----------------------------------------------------------------------
//  $Author: jbalfour $
//  $Date: 2007/06/28 17:24:35 $
//  $Id$
//  Modified 11/6/2007 by Ted Jiang
//  Now handeling n = most power of 2: 16, 64, 256, 1024
// ----------------------------------------------------------------------
#include "booksim.hpp"
#include <vector>
#include <sstream>
#include <cassert>
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "cmesh.hpp"

CMesh::CMesh( const Configuration& config, const string & name, Module * clock, CreditBox *credits ) 
  : Network(config, name, clock, credits), _cX(0), _cY(0), _memo_NodeShiftX(0), _memo_NodeShiftY(0), _memo_PortShiftY(0)
{
  _ComputeSize( config );
  _Alloc();
  _BuildNet(config);
}

void CMesh::RegisterRoutingFunctions() {
  gRoutingFunctionMap["dor_cmesh"] = &dor_cmesh;
  gRoutingFunctionMap["dor_no_express_cmesh"] = &dor_no_express_cmesh;
  gRoutingFunctionMap["xy_yx_cmesh"] = &xy_yx_cmesh;
  gRoutingFunctionMap["xy_yx_no_express_cmesh"]  = &xy_yx_no_express_cmesh;
}

void CMesh::_ComputeSize( const Configuration &config ) {

  int k = config.GetInt( "k" );
  int n = config.GetInt( "n" );
  assert(n <= 2); // broken for n > 2
  int c = config.GetInt( "c" );
  assert(c == 4); // broken for c != 4

  ostringstream router_name;
  //how many routers in the x or y direction
  _xcount = config.GetInt("x");
  _ycount = config.GetInt("y");
  assert(_xcount == _ycount); // broken for asymmetric topologies
  //configuration of hohw many clients in X and Y per router
  _xrouter = config.GetInt("xr");
  _yrouter = config.GetInt("yr");
  assert(_xrouter == _yrouter); // broken for asymmetric concentration

  _radix = _k = k ;
  _dim = _n = n ;
  _conc = _c = c ;

  assert(c == _xrouter*_yrouter);
  
  _nodes    = _c * powi( _k, _n); // Number of nodes in network
  _size     = powi( _k, _n);      // Number of routers in network
  _channels = 2 * _n * _size;     // Number of channels in network

  _cX = _c / _n ;   // Concentration in X Dimension 
  _cY = _c / _cX ;  // Concentration in Y Dimension

  //
  _memo_NodeShiftX = _cX >> 1 ;
  _memo_NodeShiftY = log_two(_radix * _cX) + ( _cY >> 1 ) ;
  _memo_PortShiftY = log_two(_radix * _cX)  ;

}

void CMesh::_BuildNet( const Configuration& config ) {

  int x_index ;
  int y_index ;

  //standard trace configuration 
  if(gTrace){
    cout<<"Setup Finished Router"<<endl;
  }

  //latency type, noc or conventional network
  bool use_noc_latency;
  use_noc_latency = (config.GetInt("use_noc_latency")==1);
  
  ostringstream name;
  // The following vector is used to check that every
  //  processor in the system is connected to the network
  vector<bool> channel_vector(_nodes, false) ;
  
  //
  // Routers and Channel
  //
  for (int node = 0; node < _size; ++node) {

    // Router index derived from mesh index
    y_index = node / _k ;
    x_index = node % _k ;

    const int degree_in  = 2 *_n + _c ;
    const int degree_out = 2 *_n + _c ;

    name << "router_" << y_index << '_' << x_index;
    _routers[node] = Router::NewRouter( config, 
					this, 
					name.str(), 
					node,
					degree_in,
					degree_out,
          _credits);
    _timed_modules.push_back(_routers[node]);
    name.str("");

    //
    // Port Numbering: as best as I can determine, the order in
    //  which the input and output channels are added to the
    //  router determines the associated port number that must be
    //  used by the router. Output port number increases with 
    //  each new channel
    //

    //
    // Processing node channels
    //
    for (int y = 0; y < _cY ; y++) {
      for (int x = 0; x < _cX ; x++) {
	int link = (_k * _cX) * (_cY * y_index + y) + (_cX * x_index + x) ;
	assert( link >= 0 ) ;
	assert( link < _nodes ) ;
	assert( channel_vector[ link ] == false ) ;
	channel_vector[link] = true ;
	// Ingress Ports
	_routers[node]->AddInputChannel(_inject[link], _inject_cred[link]);
	// Egress Ports
	_routers[node]->AddOutputChannel(_eject[link], _eject_cred[link]);
	//injeciton ejection latency is 1
	_inject[link]->SetLatency( 1 );
	_eject[link]->SetLatency( 1 );
      }
    }

    //
    // router to router channels
    //
    const int x = node % _k ;
    const int y = node / _k ;
    const int offset = powi( _k, _n ) ;

    //the channel number of the input output channels.
    int px_out = _k * y + x + 0 * offset ;
    int nx_out = _k * y + x + 1 * offset ;
    int py_out = _k * y + x + 2 * offset ;
    int ny_out = _k * y + x + 3 * offset ;
    int px_in  = _k * y + ((x+1)) + 1 * offset ;
    int nx_in  = _k * y + ((x-1)) + 0 * offset ;
    int py_in  = _k * ((y+1)) + x + 3 * offset ;
    int ny_in  = _k * ((y-1)) + x + 2 * offset ;

    // Express Channels
    if (x == 0){
      // Router on left edge of mesh. Connect to -x output of
      //  another router on the left edge of the mesh.
      if (y < _k / 2 )
	nx_in = _k * (y + _k/2) + x + offset ;
      else
	nx_in = _k * (y - _k/2) + x + offset ;
    }
    
    if (x == (_k-1)){
      // Router on right edge of mesh. Connect to +x output of 
      //  another router on the right edge of the mesh.
      if (y < _k / 2)
   	px_in = _k * (y + _k/2) + x ;
      else
   	px_in = _k * (y - _k/2) + x ;
    }
    
    if (y == 0) {
      // Router on bottom edge of mesh. Connect to -y output of
      //  another router on the bottom edge of the mesh.
      if (x < _k / 2) 
	ny_in = _k * y + (x + _k/2) + 3 * offset ;
      else
	ny_in = _k * y + (x - _k/2) + 3 * offset ;
    }
    
    if (y == (_k-1)) {
      // Router on top edge of mesh. Connect to +y output of
      //  another router on the top edge of the mesh
      if (x < _k / 2)
	py_in = _k * y + (x + _k/2) + 2 * offset ;
      else
	py_in = _k * y + (x - _k/2) + 2 * offset ;
    }

    /*set latency and add the channels*/

    // Port 0: +x channel
    if(use_noc_latency) {
      int const px_latency = (x == _k-1) ? (_cY*_k/2) : _cX;
      _chan[px_out]->SetLatency( px_latency );
      _chan_cred[px_out]->SetLatency( px_latency );
    } else {
      _chan[px_out]->SetLatency( 1 );
      _chan_cred[px_out]->SetLatency( 1 );
    }
    _routers[node]->AddOutputChannel( _chan[px_out], _chan_cred[px_out] );
    _routers[node]->AddInputChannel( _chan[px_in], _chan_cred[px_in] );
    
    if(gTrace) {
      cout<<"Link "<<" "<<px_out<<" "<<px_in<<" "<<node<<" "<<_chan[px_out]->GetLatency()<<endl;
    }

    // Port 1: -x channel
    if(use_noc_latency) {
      int const nx_latency = (x == 0) ? (_cY*_k/2) : _cX;
      _chan[nx_out]->SetLatency( nx_latency );
      _chan_cred[nx_out]->SetLatency( nx_latency );
    } else {
      _chan[nx_out]->SetLatency( 1 );
      _chan_cred[nx_out]->SetLatency( 1 );
    }
    _routers[node]->AddOutputChannel( _chan[nx_out], _chan_cred[nx_out] );
    _routers[node]->AddInputChannel( _chan[nx_in], _chan_cred[nx_in] );

    if(gTrace){
      cout<<"Link "<<" "<<nx_out<<" "<<nx_in<<" "<<node<<" "<<_chan[nx_out]->GetLatency()<<endl;
    }

    // Port 2: +y channel
    if(use_noc_latency) {
      int const py_latency = (y == _k-1) ? (_cX*_k/2) : _cY;
      _chan[py_out]->SetLatency( py_latency );
      _chan_cred[py_out]->SetLatency( py_latency );
    } else {
      _chan[py_out]->SetLatency( 1 );
      _chan_cred[py_out]->SetLatency( 1 );
    }
    _routers[node]->AddOutputChannel( _chan[py_out], _chan_cred[py_out] );
    _routers[node]->AddInputChannel( _chan[py_in], _chan_cred[py_in] );
    
    if(gTrace){
      cout<<"Link "<<" "<<py_out<<" "<<py_in<<" "<<node<<" "<<_chan[py_out]->GetLatency()<<endl;
    }

    // Port 3: -y channel
    if(use_noc_latency){
      int const ny_latency = (y == 0) ? (_cX*_k/2) : _cY;
      _chan[ny_out]->SetLatency( ny_latency );
      _chan_cred[ny_out]->SetLatency( ny_latency );
    } else {
      _chan[ny_out]->SetLatency( 1 );
      _chan_cred[ny_out]->SetLatency( 1 );
    }
    _routers[node]->AddOutputChannel( _chan[ny_out], _chan_cred[ny_out] );
    _routers[node]->AddInputChannel( _chan[ny_in], _chan_cred[ny_in] );    

    if(gTrace){
      cout<<"Link "<<" "<<ny_out<<" "<<ny_in<<" "<<node<<" "<<_chan[ny_out]->GetLatency()<<endl;
    }
    
  }    

  // Check that all processors were connected to the network
  for ( int i = 0 ; i < _nodes ; i++ ) 
    assert( channel_vector[i] == true ) ;
  
  if(gTrace){
    cout<<"Setup Finished Link"<<endl;
  }
}


// ----------------------------------------------------------------------
//
//  Routing Helper Functions
//
// ----------------------------------------------------------------------

int CMesh::NodeToRouter( int address ) const {

  int y  = (address /  (_cX*_radix))/_cY ;
  int x  = (address %  (_cX*_radix))/_cY ;
  int router = y*_radix + x ;
  
  return router ;
}

int CMesh::NodeToPort( int address ) const {
  
  const int maskX  = _cX - 1 ;
  const int maskY  = _cY - 1 ;

  int x = address & maskX ;
  int y = (int)(address/(2*_radix)) & maskY ;

  return (_conc / 2) * y + x;
}

// ----------------------------------------------------------------------
//
//  Routing Functions
//
// ----------------------------------------------------------------------

// Concentrated Mesh: X-Y
int CMesh::cmesh_xy( int cur, int dest ) const {

  const int POSITIVE_X = 0 ;
  const int NEGATIVE_X = 1 ;
  const int POSITIVE_Y = 2 ;
  const int NEGATIVE_Y = 3 ;

  int cur_y  = cur / _radix;
  int cur_x  = cur % _radix;
  int dest_y = dest / _radix;
  int dest_x = dest % _radix;

  // Dimension-order Routing: x , y
  if (cur_x < dest_x) {
    // Express?
    if ((dest_x - cur_x) > 1){
      if (cur_y == 0)
    	return _conc + NEGATIVE_Y ;
      if (cur_y == (_radix-1))
    	return _conc + POSITIVE_Y ;
    }
    return _conc + POSITIVE_X ;
  }
  if (cur_x > dest_x) {
    // Express ? 
    if ((cur_x - dest_x) > 1){
      if (cur_y == 0)
    	return _conc + NEGATIVE_Y ;
      if (cur_y == (_radix-1))
    	return _conc + POSITIVE_Y ;
    }
    return _conc + NEGATIVE_X ;
  }
  if (cur_y < dest_y) {
    // Express?
    if ((dest_y - cur_y) > 1) {
      if (cur_x == 0)
    	return _conc + NEGATIVE_X ;
      if (cur_x == (_radix-1))
    	return _conc + POSITIVE_X ;
    }
    return _conc + POSITIVE_Y ;
  }
  if (cur_y > dest_y) {
    // Express ?
    if ((cur_y - dest_y) > 1 ){
      if (cur_x == 0)
    	return _conc + NEGATIVE_X ;
      if (cur_x == (_radix-1))
    	return _conc + POSITIVE_X ;
    }
    return _conc + NEGATIVE_Y ;
  }
  return 0;
}

// Concentrated Mesh: Y-X
int CMesh::cmesh_yx( int cur, int dest ) const {
  const int POSITIVE_X = 0 ;
  const int NEGATIVE_X = 1 ;
  const int POSITIVE_Y = 2 ;
  const int NEGATIVE_Y = 3 ;

  int cur_y  = cur / _radix ;
  int cur_x  = cur % _radix ;
  int dest_y = dest / _radix ;
  int dest_x = dest % _radix ;

  // Dimension-order Routing: y, x
  if (cur_y < dest_y) {
    // Express?
    if ((dest_y - cur_y) > 1) {
      if (cur_x == 0)
    	return _conc + NEGATIVE_X ;
      if (cur_x == (_radix-1))
    	return _conc + POSITIVE_X ;
    }
    return _conc + POSITIVE_Y ;
  }
  if (cur_y > dest_y) {
    // Express ?
    if ((cur_y - dest_y) > 1 ){
      if (cur_x == 0)
    	return _conc + NEGATIVE_X ;
      if (cur_x == (_radix-1))
    	return _conc + POSITIVE_X ;
    }
    return _conc + NEGATIVE_Y ;
  }
  if (cur_x < dest_x) {
    // Express?
    if ((dest_x - cur_x) > 1){
      if (cur_y == 0)
    	return _conc + NEGATIVE_Y ;
      if (cur_y == (_radix-1))
    	return _conc + POSITIVE_Y ;
    }
    return _conc + POSITIVE_X ;
  }
  if (cur_x > dest_x) {
    // Express ? 
    if ((cur_x - dest_x) > 1){
      if (cur_y == 0)
    	return _conc + NEGATIVE_Y ;
      if (cur_y == (_radix-1))
    	return _conc + POSITIVE_Y ;
    }
    return _conc + NEGATIVE_X ;
  }
  return 0;
}

void xy_yx_cmesh( const Router *r, const Flit *f, int in_channel, 
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
    const CMesh *net = dynamic_cast<const CMesh *>(r->GetOWner());
    if(!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }
    // Current Router
    int cur_router = r->GetID();

    // Destination Router
    int dest_router = net->NodeToRouter( f->dest ) ;  

    if (dest_router == cur_router) {

      // Forward to processing element
      out_port = net->NodeToPort( f->dest );      

    } else {

      // Forward to neighbouring router

      //each class must have at least 2 vcs assigned or else xy_yx will deadlock
      int const available_vcs = (vcEnd - vcBegin + 1) / 2;
      assert(available_vcs > 0);

      // randomly select dimension order at first hop
      bool x_then_y = ((in_channel < net->GetConc()) ?
		       (RandomInt(1) > 0) :
		       (f->vc < (vcBegin + available_vcs)));

      if(x_then_y) {
	out_port = net->cmesh_xy( cur_router, dest_router );
	vcEnd -= available_vcs;
      } else {
	out_port = net->cmesh_yx( cur_router, dest_router );
	vcBegin += available_vcs;
      }
    }

  }

  outputs->Clear();

  outputs->AddRange( out_port , vcBegin, vcEnd );
}

// ----------------------------------------------------------------------
//
//  Concentrated Mesh: Random XY-YX w/o Express Links
//
//   <int> cur:  current router address 
///  <int> dest: destination router address 
//
// ----------------------------------------------------------------------

int CMesh::cmesh_xy_no_express( int cur, int dest ) const {
  
  const int POSITIVE_X = 0 ;
  const int NEGATIVE_X = 1 ;
  const int POSITIVE_Y = 2 ;
  const int NEGATIVE_Y = 3 ;

  const int cur_y  = cur  / _radix ;
  const int cur_x  = cur  % _radix ;
  const int dest_y = dest / _radix ;
  const int dest_x = dest % _radix ;


  //  Note: channel numbers bellow _conc (degree of concentration) are
  //        injection and ejection links

  // Dimension-order Routing: X , Y
  if (cur_x < dest_x) {
    return _conc + POSITIVE_X ;
  }
  if (cur_x > dest_x) {
    return _conc + NEGATIVE_X ;
  }
  if (cur_y < dest_y) {
    return _conc + POSITIVE_Y ;
  }
  if (cur_y > dest_y) {
    return _conc + NEGATIVE_Y ;
  }
  return 0;
}

int CMesh::cmesh_yx_no_express( int cur, int dest ) const {

  const int POSITIVE_X = 0 ;
  const int NEGATIVE_X = 1 ;
  const int POSITIVE_Y = 2 ;
  const int NEGATIVE_Y = 3 ;
  
  const int cur_y  = cur / _radix ;
  const int cur_x  = cur % _radix ;
  const int dest_y = dest / _radix ;
  const int dest_x = dest % _radix ;

  //  Note: channel numbers bellow _conc (degree of concentration) are
  //        injection and ejection links

  // Dimension-order Routing: X , Y
  if (cur_y < dest_y) {
    return _conc + POSITIVE_Y ;
  }
  if (cur_y > dest_y) {
    return _conc + NEGATIVE_Y ;
  }
  if (cur_x < dest_x) {
    return _conc + POSITIVE_X ;
  }
  if (cur_x > dest_x) {
    return _conc + NEGATIVE_X ;
  }
  return 0;
}

void xy_yx_no_express_cmesh( const Router *r, const Flit *f, int in_channel, 
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
    const CMesh *net = dynamic_cast<const CMesh *>(r->GetOWner());
    if(!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }

    // Current Router
    int cur_router = r->GetID();

    // Destination Router
    int dest_router = net->NodeToRouter( f->dest );  

    if (dest_router == cur_router) {

      // Forward to processing element
      out_port = net->NodeToPort( f->dest );

    } else {

      // Forward to neighbouring router
    
      //each class must have at least 2 vcs assigned or else xy_yx will deadlock
      int const available_vcs = (vcEnd - vcBegin + 1) / 2;
      assert(available_vcs > 0);

      // randomly select dimension order at first hop
      bool x_then_y = ((in_channel < net->GetConc()) ?
		       (RandomInt(1) > 0) :
		       (f->vc < (vcBegin + available_vcs)));

      if(x_then_y) {
	out_port = net->cmesh_xy_no_express( cur_router, dest_router );
	vcEnd -= available_vcs;
      } else {
	out_port = net->cmesh_yx_no_express( cur_router, dest_router );
	vcBegin += available_vcs;
      }
    }
  }

  outputs->Clear();

  outputs->AddRange( out_port , vcBegin, vcEnd );
}
//============================================================
//
//=====
int CMesh::cmesh_next( int cur, int dest ) const {

  const int POSITIVE_X = 0 ;
  const int NEGATIVE_X = 1 ;
  const int POSITIVE_Y = 2 ;
  const int NEGATIVE_Y = 3 ;
  
  int cur_y  = cur / _radix ;
  int cur_x  = cur % _radix ;
  int dest_y = dest / _radix ;
  int dest_x = dest % _radix ;

  // Dimension-order Routing: x , y
  if (cur_x < dest_x) {
    // Express?
    if ((dest_x - cur_x) > _radix/2-1){
      if (cur_y == 0)
	return _conc + NEGATIVE_Y ;
      if (cur_y == (_radix-1))
	return _conc + POSITIVE_Y ;
    }
    return _conc + POSITIVE_X ;
  }
  if (cur_x > dest_x) {
    // Express ? 
    if ((cur_x - dest_x) > _radix/2-1){
      if (cur_y == 0)
	return _conc + NEGATIVE_Y ;
      if (cur_y == (_radix-1)) 
	return _conc + POSITIVE_Y ;
    }
    return _conc + NEGATIVE_X ;
  }
  if (cur_y < dest_y) {
    // Express?
    if ((dest_y - cur_y) > _radix/2-1) {
      if (cur_x == 0)
	return _conc + NEGATIVE_X ;
      if (cur_x == (_radix-1))
	return _conc + POSITIVE_X ;
    }
    return _conc + POSITIVE_Y ;
  }
  if (cur_y > dest_y) {
    // Express ?
    if ((cur_y - dest_y) > _radix/2-1){
      if (cur_x == 0)
	return _conc + NEGATIVE_X ;
      if (cur_x == (_radix-1))
	return _conc + POSITIVE_X ;
    }
    return _conc + NEGATIVE_Y ;
  }

  assert(false);
  return -1;
}

void dor_cmesh( const Router *r, const Flit *f, int in_channel, 
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
    const CMesh *net = dynamic_cast<const CMesh *>(r->GetOWner());
    if (!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }
    // Current Router
    int cur_router = r->GetID();

    // Destination Router
    int dest_router = net->NodeToRouter( f->dest ) ;  
  
    if (dest_router == cur_router) {

      // Forward to processing element
      out_port = net->NodeToPort( f->dest ) ;

    } else {

      // Forward to neighbouring router
      out_port = net->cmesh_next( cur_router, dest_router );
    }
  }

  outputs->Clear();

  outputs->AddRange( out_port, vcBegin, vcEnd);
}

//============================================================
//
//=====
int CMesh::cmesh_next_no_express( int cur, int dest ) const {

  const int POSITIVE_X = 0 ;
  const int NEGATIVE_X = 1 ;
  const int POSITIVE_Y = 2 ;
  const int NEGATIVE_Y = 3 ;
  
  //magic constant 2, which is supose to be _cX and _cY
  int cur_y  = cur/_radix ;
  int cur_x  = cur%_radix ;
  int dest_y = dest/_radix;
  int dest_x = dest%_radix ;

  // Dimension-order Routing: x , y
  if (cur_x < dest_x) {
    return _conc + POSITIVE_X ;
  }
  if (cur_x > dest_x) {
    return _conc + NEGATIVE_X ;
  }
  if (cur_y < dest_y) {
    return _conc + POSITIVE_Y ;
  }
  if (cur_y > dest_y) {
    return _conc + NEGATIVE_Y ;
  }
  assert(false);
  return -1;
}

void dor_no_express_cmesh( const Router *r, const Flit *f, int in_channel, 
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
  }
  else
  {
    /**
     * This dynamic_cast is Bad.
     * However the legacy code is using a constant interface of `tRoutingFunction`
     * for handling routing of every single networks type Booksim2 supports,
     * leaving no other options without back pain.
     */
    const CMesh *net = dynamic_cast<const CMesh *>(r->GetOWner());
    if (!net)
    {
      r->Error("This router doesn't belong to any Networks, or the Network it belongs doesn't support this routing method.");
    }

    // Current Router
    int cur_router = r->GetID();

    // Destination Router
    int dest_router = net->NodeToRouter(f->dest);

    if (dest_router == cur_router)
    {

      // Forward to processing element
      out_port = net->NodeToPort(f->dest);
    } else {

      // Forward to neighbouring router
      out_port = net->cmesh_next_no_express(cur_router, dest_router);
    }
  }

  outputs->Clear();

  outputs->AddRange( out_port, vcBegin, vcEnd );
}
