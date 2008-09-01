#ifndef _FlatFlyOnChip_HPP_
#define _FlatFlyOnChip_HPP_

#include "network.hpp"

#include "routefunc.hpp"
#include <assert.h>


class FlatFlyOnChip : public Network {

  int _m;
  int _n;
  int _r;
  int _k;
  int _c;
  int _radix;
  int _net_size;
  int _stageout;
  int _numinput;
  int _stages;
  int _num_of_switch;

  void _ComputeSize( const Configuration &config );
  void _BuildNet( const Configuration &config );

  int _OutChannel( int stage, int addr, int port, int outputs ) const;
  int _InChannel( int stage, int addr, int port ) const;


 
public:
  FlatFlyOnChip( const Configuration &config );

  int GetN( ) const;
  int GetK( ) const;

  static void RegisterRoutingFunctions() ;
  double Capacity( ) const;
  void InsertRandomFaults( const Configuration &config );

  static short half_vcs;
};


void min_flatfly( const Router *r, const Flit *f, int in_channel, 
		  OutputSet *outputs, bool inject );
void ugal_flatfly_onchip( const Router *r, const Flit *f, int in_channel,
			  OutputSet *outputs, bool inject );
void valiant_flatfly( const Router *r, const Flit *f, int in_channel,
			  OutputSet *outputs, bool inject );
int find_distance (int src, int dest);
int find_ran_intm (int src, int dest);
int flatfly_outport(int dest, int rID);
int find_phy_distance(int src, int dest);
int flatfly_transformation(int dest);

#endif
