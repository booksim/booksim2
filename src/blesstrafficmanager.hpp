/*	Ameya: new traffic manager to handle bufferless networks
*/

#ifndef _BLESSTRAFFICMANAGER_HPP_
#define _BLESSTRAFFICMANAGER_HPP_

#include <iostream>

#include "config_utils.hpp"
#include "stats.hpp"
#include "trafficmanager.hpp"

class BlessTrafficManager : public TrafficManager {

protected:
  // virtual void _RetireFlit( Flit *f, int dest );

  // virtual void _ClearStats( );
  virtual void _Step( );
  void _GeneratePacket( int source, int stype, int cl, int time );
  // virtual void _UpdateOverallStats( );

  // virtual string _OverallStatsCSV(int c = 0) const;

public:

  BlessTrafficManager( const Configuration &config, const vector<Network *> & net );
  virtual ~BlessTrafficManager( );

  // virtual void WriteStats( ostream & os = cout ) const;
  // virtual void DisplayStats( ostream & os = cout ) const;
  // virtual void DisplayOverallStats( ostream & os = cout ) const;

};

#endif