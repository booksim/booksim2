/*	Ameya: new traffic manager to handle bufferless networks
*/

#ifndef _BLESSTRAFFICMANAGER_HPP_
#define _BLESSTRAFFICMANAGER_HPP_

#include <iostream>
#include <vector>
#include <algorithm>
#include <map>
#include <string>

#include "config_utils.hpp"
#include "stats.hpp"
#include "trafficmanager.hpp"

class BlessTrafficManager : public TrafficManager {
private:
  int _golden_turn;
  int _golden_packet;
  int _golden_epoch;
  // Ameya
  int _file_inject;
  int _eoif;
  string _inject_file;
  //Nandan
  int f_source;
  int f_time;
  int f_dest;
  char request_type;
  int position;

  struct Stat_Util{
    Flit * f;
    int pending;
  };
  
  vector<map<int, Stat_Util*> > _retire_stats;
  vector<map<int, vector<Flit *> > > _router_flits_in_flight;

protected:

  void _UpdateGoldenStatus( );
  void _RetireFlit( Flit *f, int dest );
  virtual void _Step( );
  void _GeneratePacket( int source, int stype, int cl, int time );
  void _Read_File( int position );
  int _IssuePacket( int source, char request_type, int cl);
  void _Inject();
  int Calculate_Dest( string address );

public:

  BlessTrafficManager( const Configuration &config, const vector<Network *> & net );
  virtual ~BlessTrafficManager( );
};

#endif