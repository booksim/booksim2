/*	Ameya: new traffic manager to handle bufferless networks
*/

#ifndef _BLESSTRAFFICMANAGER_HPP_
#define _BLESSTRAFFICMANAGER_HPP_

#include <iostream>
#include <vector>
#include <algorithm>
#include <map>

#include "config_utils.hpp"
#include "stats.hpp"
#include "trafficmanager.hpp"

class BlessTrafficManager : public TrafficManager {
private:
  int _golden_turn;
  int _golden_packet;
  int _golden_epoch;
  
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
  
public:

  BlessTrafficManager( const Configuration &config, const vector<Network *> & net );
  virtual ~BlessTrafficManager( );

};

#endif