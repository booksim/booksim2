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

#ifndef _STEADYSTATETRAFFICMANAGER_HPP_
#define _STEADYSTATETRAFFICMANAGER_HPP_

#include <vector>

#include "synthetictrafficmanager.hpp"
#include "injection.hpp"

class SteadyStateTrafficManager : public SyntheticTrafficManager {

protected:

  vector<double> _load;

  vector<string> _injection;
  vector<InjectionProcess *> _injection_process;

  bool _measure_latency;

  int   _sample_period;
  int   _max_samples;
  int   _warmup_periods;

  vector<double> _latency_thres;

  vector<double> _stopping_threshold;
  vector<double> _acc_stopping_threshold;

  vector<double> _warmup_threshold;
  vector<double> _acc_warmup_threshold;

  virtual int _IssuePacket( int source, int cl );

  virtual void _ResetSim( );

  virtual bool _SingleSim( );

  virtual string _OverallStatsHeaderCSV() const;
  virtual string _OverallClassStatsCSV(int c) const;

public:

  SteadyStateTrafficManager( const Configuration &config, const vector<Network *> & net );
  virtual ~SteadyStateTrafficManager( );

};

#endif
