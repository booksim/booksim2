// $Id$

/*
Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
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

#ifndef _BATCHTRAFFICMANAGER_HPP_
#define _BATCHTRAFFICMANAGER_HPP_

#include <iostream>

#include "config_utils.hpp"
#include "stats.hpp"
#include "trafficmanager.hpp"

class BatchTrafficManager : public TrafficManager {

protected:

  int _batch_size;
  int _batch_count;
  int _last_batch_time;
  int _last_id;
  int _last_pid;

  Stats * _batch_time;
  Stats * _overall_batch_time;

  virtual void _RetireFlit( Flit *f, int dest );

  virtual int _IssuePacket( int source, int cl );
  virtual bool _SingleSim( );

  virtual void _UpdateOverallStats( );
public:
  virtual void printPartialStats(int t , int i){}

  BatchTrafficManager( const Configuration &config, const vector<Booksim_Network *> & net );
  virtual ~BatchTrafficManager( );

  virtual void DisplayStats( ostream & os = cout );
  virtual void DisplayOverallStats( ostream & os = cout ) const;

};

#endif
