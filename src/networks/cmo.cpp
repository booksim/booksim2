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

/*cmo.cpp
 *
 *Concentrated Multi-dimensional Octagon
 * created by Sang Kyun Kim and Wook-Jin Chung
 * cva.stanford.edu/classes/ee382c/projects/cmo.ppt
 *
 * only designed to be 2Dimension with 16 routers
 */


#include "booksim.hpp"
#include <vector>
#include <sstream>
#include "cmo.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "globals.hpp"

#define CMO_ADDR_MASK(dest, cur)      (((dest >> 2) - cur) & 0x7)
#define CMO_DIM_MASK(dest, cur)       (((dest >> 2) ^ cur) & 0x8)
#define CMO_PROC_MASK(dest)           (dest & 0x3)



CMO::CMO( const Configuration &config, const string & name ) :
Network( config, name )
{
  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}

// wire_delay1 : normal wires ( neighboring wires )
// wire_delay2 : slice wires ( between dimensions / express channels )
// wire_delay3 : L1 wires ( the shorter L wires )
// wire_delay4 : L2 wires ( the longer L wires )

void CMO::_ComputeSize( const Configuration &config )
{
  _c = config.GetInt( "c" );
  
  gC = _c;

  //this assume c= 4 nodes, need to change this
  realgk = 8;
  realgk = 2;

  _size     = 8*2; //16 routers
  _channels = 2*12*2 + 2*8;// ocatagon bidirectional*slice + slice connect

  _sources = _c*_size;
  _dests   = _c*_size;


   //  _max_wire_delay = _wire_delay4;
}

void CMO::RegisterRoutingFunctions(){

  gRoutingFunctionMap["dim_order_cmo"] = &dim_order_cmo;
}

void CMO::_BuildNet( const Configuration &config )
{
  int left_node;
  int right_node;
  int cross_node;
  int slice_node;

  int left_input;
  int right_input;
  int cross_input;
  int slice_input;

  int left_output;
  int right_output;
  int cross_output;
  int slice_output;

  int  _wire_delay4 =4;

  ostringstream router_name;

  // router for every _c nodes!!!!!!
  for ( int node = 0; node < _size; ++node ) {
    int node_mod = node & 0x7;  // same as node_mode = node % 8;

    router_name << "router";

    // ROUTER NAME CONVENTION --> router___slice#___octagon#
    router_name << "_" << node/8 << "_" << node%8;

    _routers[node] = Router::NewRouter( config, this, router_name.str( ),
                                        node, 4+_c, 4+_c );   // 4 connections to the outside world and 4 concentration

    router_name.str("");  /// what is this? perhaps reseting the name back to original position

                                       // but who cares cause it's not used anymore

    // determine the neighbors and channel IDs
    left_node  = _LeftNode( node );
    right_node = _RightNode( node );
    cross_node = _CrossNode( node );
    slice_node = _SliceNode( node );

    left_input = _RightChannel( left_node );
    right_input = _LeftChannel( right_node );
    cross_input = _CrossChannel( cross_node );
    slice_input = _SliceChannel( slice_node );

    left_output = _LeftChannel( node );
    right_output = _RightChannel( node );
    cross_output = _CrossChannel( node );
    slice_output = _SliceChannel( node );

//     _chan[left_input]->SetLatency(_wire_delay1*2);
//     _chan_cred[left_input]->SetLatency(_wire_delay1*2);
//     _chan[right_input]->SetLatency(_wire_delay1*2);
//     _chan_cred[right_input]->SetLatency(_wire_delay1*2);

    _chan[left_input]->SetLatency(0);
    _chan_cred[left_input]->SetLatency(0);
    _chan[right_input]->SetLatency(0);
    _chan_cred[right_input]->SetLatency(0);

    _routers[node]->AddInputChannel( _chan[left_input], _chan_cred[left_input] );
    _routers[node]->AddInputChannel( _chan[right_input], _chan_cred[right_input]);
    //same as ((node >> 1) ^ node) & 1, but for clarity, we use the following expression
    if((node_mod == 1) || (node_mod == 2) || (node_mod == 5) || (node_mod == 6)) {

//       _chan[cross_input]->SetLatency(_wire_delay3*2);
//       _chan_cred[cross_input]->SetLatency(_wire_delay3*2);
      _chan[cross_input]->SetLatency(0);
      _chan_cred[cross_input]->SetLatency(0);

      _routers[node]->AddInputChannel( _chan[cross_input], _chan_cred[cross_input] );
    }
    else {
     
//       _chan[cross_input]->SetLatency(_wire_delay4*2);
//       _chan_cred[cross_input]->SetLatency(_wire_delay4*2);
      _chan[cross_input]->SetLatency(0);
      _chan_cred[cross_input]->SetLatency(0);
      
      _routers[node]->AddInputChannel( _chan[cross_input], _chan_cred[cross_input] );
    }

//     _chan[slice_input]->SetLatency(_wire_delay2*2);
//     _chan_cred[slice_input]->SetLatency(_wire_delay2*2);

    _chan[slice_input]->SetLatency(0);
    _chan_cred[slice_input]->SetLatency(0);

    _routers[node]->AddInputChannel( _chan[slice_input], _chan_cred[slice_input] );
    
//     _chan[left_output]->SetLatency(_wire_delay1*2);
//     _chan_cred[left_output]->SetLatency(_wire_delay1*2);
//     _chan[right_output]->SetLatency(_wire_delay1*2);
//     _chan_cred[right_output]->SetLatency(_wire_delay1*2);

    _chan[left_output]->SetLatency(0);
    _chan_cred[left_output]->SetLatency(0);
    _chan[right_output]->SetLatency(0);
    _chan_cred[right_output]->SetLatency(0);

    
    _routers[node]->AddOutputChannel( _chan[left_output], _chan_cred[left_output] );
    _routers[node]->AddOutputChannel( _chan[right_output], _chan_cred[right_output] );

    if((node_mod == 1) || (node_mod == 2) || (node_mod == 5) || (node_mod == 6)) {

//       _chan[cross_output]->SetLatency(_wire_delay3*2);
//       _chan_cred[cross_output]->SetLatency(_wire_delay3*2);
      _chan[cross_output]->SetLatency(0);
      _chan_cred[cross_output]->SetLatency(0);

      _routers[node]->AddOutputChannel( _chan[cross_output], _chan_cred[cross_output] );
    }
    else {

      _chan[cross_output]->SetLatency(_wire_delay4*2);
      _chan_cred[cross_output]->SetLatency(_wire_delay4*2);
      _chan[cross_output]->SetLatency(0);
      _chan_cred[cross_output]->SetLatency(0);

      _routers[node]->AddOutputChannel( _chan[cross_output], _chan_cred[cross_output] );
    }

//     _chan[slice_output]->SetLatency(_wire_delay2*2);
//     _chan_cred[slice_output]->SetLatency(_wire_delay2*2);
    _chan[slice_output]->SetLatency(0);
    _chan_cred[slice_output]->SetLatency(0);

    _routers[node]->AddOutputChannel( _chan[slice_output], _chan_cred[slice_output] );
    

    for( int i=0; i<_c; i++) {    //concentration
      
      _inject[node*_c+i]->SetLatency(0);
      _inject_cred[node*_c+i]->SetLatency(0);
      _eject[node*_c+i]->SetLatency(0);
      _eject_cred[node*_c+i]->SetLatency(0);

      _routers[node]->AddInputChannel( _inject[node*_c+i], _inject_cred[node*_c+i] );
      _routers[node]->AddOutputChannel( _eject[node*_c+i], _eject_cred[node*_c+i] );
    }
  }
}
// CHANNEL NUMBERING
// BASE => 4*node
// OFFSET =>  LEFT=0 | RIGHT=1 | CROSS=2 | SLICE = 3

int CMO::_LeftChannel( int node )
{
    return ( 4*node );
}

int CMO::_RightChannel( int node )
{
    return ( 4*node+1 );
}

int CMO::_CrossChannel( int node )
{
    return ( 4*node+2 );
}

int CMO::_SliceChannel( int node )
{
    return ( 4*node+3 );
}

int CMO::_LeftNode( int node )
{
    int left_node;

    if(node == 0)
        left_node = 7;
    else if(node == 8)
        left_node = 15;
    else
        left_node = node-1;

    return left_node;
}

int CMO::_RightNode( int node )
{
    int right_node;

    if(node == 7)
        right_node = 0;
    else if(node == 15)
        right_node = 8;
    else
        right_node = node+1;

    return right_node;
}

int CMO::_CrossNode( int node )
{
    return ( 8*(node/8) + (node%8+4)%8 );
}

int CMO::_SliceNode( int node )
{
    return ( (node+8)%16 );
}


int CMO::GetC( ) const
{
    return _c;
}

void CMO::InsertRandomFaults( const Configuration &config )
{
    int num_fails;
    num_fails = config.GetInt( "link_failures" );
}

double CMO::Capacity( ) const
{
    return ((double)2);
}

int CMO::MapNode(int physical_node) const
{
	int div_node = physical_node / 16;
	int mod_node = physical_node % 16;
	int sm_node1, sm_node2;
	if(mod_node > 7) {
		mod_node -= 8;
		sm_node1 = mod_node / 2;
		sm_node2 = mod_node % 2;
		return (div_node*16)+(sm_node1*4)+sm_node2+2;
	}
	else {
		sm_node1 = mod_node / 2;
		sm_node2 = mod_node % 2;
		return (div_node*16)+(sm_node1*4)+sm_node2;
	}
}

int CMO::UnmapNode(int logical_node) const
{
	int div_node = logical_node / 16;
	int mod_node = logical_node % 16;
	int sm_node1 = mod_node / 4;
	int sm_node2 = mod_node % 4;

	if(sm_node2 > 1) {
		sm_node2 -= 2;
		return (div_node*16)+(sm_node1*2)+sm_node2+8;
	}
	else {
		return (div_node*16)+(sm_node1*2)+sm_node2;
	}
}

void dor_next_cmo( int flitid, int cur, int dest, int in_port,
                     int *out_port, int *partition,
                     bool balance = false )
{
        int rel_addr;

        rel_addr = CMO_ADDR_MASK(dest, cur);

        // determine output port
        if((rel_addr == 1) || (rel_addr == 2)) {                // route clockwise (right)
                *out_port =  1;
                if( (cur & 0x7) > ((dest >> 2) & 0x7)) {
                        *partition = 2;
                }
                else {
                        *partition = 1;
                }
        }
        else if((rel_addr == 6) || (rel_addr == 7)) {   // route counterclockwise (left)
                *out_port = 0;
                if( ((dest >> 2) & 0x7) > (cur & 0x7)) {
                        *partition = 2;
                }
                else {
                        *partition = 1;
                }
        }
        else if((rel_addr != 0)) {                                              // route cross
                *out_port = 2;
                *partition = 0;
        }
        else if(CMO_DIM_MASK(dest, cur)) {                              // route slice
                *out_port = 3;
                *partition = 0;
        }
        else {
                *out_port = CMO_PROC_MASK(dest)+4;
                *partition = 0;
        }
}


void dim_order_cmo( const Router *r, const Flit *f, int in_channel,
                      OutputSet *outputs, bool inject )
{
  int cur;
  int dest;

  int out_port;
  int vc_min, vc_max;

  outputs->Clear( );

  cur  = r->GetID( );
  dest = f->dest;

  dor_next_cmo( f->id, cur, dest, in_channel,
                  &out_port, &f->ring_par, false );

  // for deadlock avoidance in ring
  if ( f->ring_par == 0 ) {
    vc_min = 0;
    vc_max = gNumVCS - 1;
  } else if ( f->ring_par == 1) {
    vc_min = 0;
    vc_max = gNumVCS -2;
  }
  else {
    vc_min = 1;
    vc_max = gNumVCS - 1;
  }

  if ( f->watch ) {
      *gWatchOut << GetSimTime() << " | " << r->FullName() << " | "
		  << "Adding VC range [" 
		  << vc_min << "," 
		  << vc_max << "]"
		  << " at output port " << out_port
		  << " for flit " << f->id
		  << " (input port " << in_channel
		  << ", destination " << f->dest << ")"
		  << "." << endl;
  }

  if( f->type !=  Flit::ANY_TYPE ){
    cout<<"CMO doesn't support segregating read/write traffic\n";
    assert(false);
  }
  outputs->AddRange( out_port, vc_min, vc_max );
}



