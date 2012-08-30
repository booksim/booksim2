// $Id$

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
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

#ifndef _SYNTHETICTRAFFICMANAGER_HPP_
#define _SYNTHETICTRAFFICMANAGER_HPP_

#include <vector>

#include "trafficmanager.hpp"
#include "traffic.hpp"
#include "stats.hpp"

class SyntheticTrafficManager : public TrafficManager {

private:

  vector<vector<int> > _packet_size;
  vector<vector<int> > _packet_size_rate;
  vector<int> _packet_size_max_val;

protected:

  vector<string> _traffic;
  vector<TrafficPattern *> _traffic_pattern;

  vector<int> _reply_class;
  vector<int> _request_class;

  vector<vector<int> > _qtime;
  vector<vector<bool> > _qdrained;

  vector<Stats *> _tlat_stats;     
  vector<double> _overall_min_tlat;  
  vector<double> _overall_avg_tlat;  
  vector<double> _overall_max_tlat;  

  vector<vector<Stats *> > _pair_tlat;

  virtual void _RetirePacket( Flit * head, Flit * tail );

  virtual int _IssuePacket( int source, int cl ) = 0;

  virtual void _Inject( );

  virtual bool _PacketsOutstanding( ) const;

  virtual void _ResetSim( );

  virtual string _OverallStatsHeaderCSV() const;
  virtual string _OverallClassStatsCSV(int c) const;

  int _GetNextPacketSize(int cl) const;
  double _GetAveragePacketSize(int cl) const;

  SyntheticTrafficManager( const Configuration &config, const vector<Network *> & net );

public:

  virtual ~SyntheticTrafficManager( );

};

#endif
