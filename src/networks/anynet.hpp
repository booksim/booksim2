#ifndef _ANYNET_HPP_
#define _ANYNET_HPP_

#include "network.hpp"
#include "routefunc.hpp"
#include <assert.h>
#include <string>
#include <map>
#include <list>

class AnyNet : public Network {

  string file_name;
  //associtation between  nodes and routers
  map<int, int > node_list;
  map<int,  map<int, int>* >* router_list;
  map<int, int>* routing_table;

  void _ComputeSize( const Configuration &config );
  void _BuildNet( const Configuration &config );
  void readFile();
  void buildRoutingTable();
  int findPath(int router, int dest, int* hop_count,map<int, bool>* visited); 

public:
  AnyNet( const Configuration &config );

  int GetN( ) const{ return -1;}
  int GetK( ) const{ return -1;}

  static void RegisterRoutingFunctions();
  double Capacity( ) const {return -1;}
  void InsertRandomFaults( const Configuration &config ){}
};

void min_anynet( const Router *r, const Flit *f, int in_channel, 
		      OutputSet *outputs, bool inject );
#endif
