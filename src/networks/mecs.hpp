#ifndef _MECS_HPP_
#define _MECS_HPP_

#include "network.hpp"
#include "routefunc.hpp"


class MECS : public Network {

  int _n;
  int _k;
  int _c;
  int _num_of_switch;
  int _channels_per_router;

  int _r; 

  void _ComputeSize( const Configuration &config );
  void _BuildNet( const Configuration &config );


public:
  MECS( const Configuration &config );

  int GetN( ) const {return _n;}
  int GetK( ) const {return _k;}

  static void RegisterRoutingFunctions() ;
  void InsertRandomFaults( const Configuration &config ){}
};

void dor_MECS( const Router *r, const Flit *f, int in_channel, 
	       OutputSet *outputs, bool inject );
int mecs_transformation(int dest);
#endif
