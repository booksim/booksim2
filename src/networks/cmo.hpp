// $Id$
#ifndef _CMO_HPP_
#define _CMO_HPP_

#include "network.hpp"
#include "routefunc.hpp"

class CMO : public Network {

  int _c;    
  // concentration degree --> must be perfectly divisible by this
  // only two slices
  // N = _c*8 * 2


  //   int _wire_delay1;
  //   int _wire_delay2;
  //   int _wire_delay3;
  //   int _wire_delay4;

  void _ComputeSize( const Configuration &config );
  void _BuildNet( const Configuration &config );

  int _LeftChannel( int node );
  int _RightChannel( int node );
  int _CrossChannel( int node );
  int _SliceChannel( int node );
  
  int _LeftNode( int node );
  int _RightNode( int node );
  int _CrossNode( int node );
  int _SliceNode( int node );
  
public:
  CMO( const Configuration &config );

  int GetC( ) const;

  double Capacity( ) const;
  static void RegisterRoutingFunctions();
  void InsertRandomFaults( const Configuration &config );
  int MapNode(int physical_node) const;
  int UnmapNode(int physical_node) const;
};

void dim_order_cmo( const Router *r, const Flit *f, int in_channel,
		    OutputSet *outputs, bool inject );
void dor_next_cmo( int flitid, int cur, int dest, int in_port,
		   int *out_port, int *partition,
		   bool balance );
#endif
