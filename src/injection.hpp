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

#ifndef _INJECTION_HPP_
#define _INJECTION_HPP_

#include "config_utils.hpp"
#include <set> 
using namespace std;

class InjectionProcess {
protected:
  double _rate;
  InjectionProcess(double rate);
public:
  virtual bool test(int src = 0) = 0;
  static InjectionProcess * New(Configuration const & config, string const & inject, double load);
};

class BernoulliInjectionProcess : public InjectionProcess {
public:
  BernoulliInjectionProcess(double rate);
  virtual bool test(int src = 0);
};

class OnOffInjectionProcess : public InjectionProcess {
private:
  double _alpha;
  double _beta;
  bool _state;
public:
  OnOffInjectionProcess(double rate, double alpha, double beta, 
			bool initial = false);
  virtual bool test(int src = 0);
};


class HotspotInjectionProcess : public InjectionProcess {
private:
  bool  hs_send_all ;
  set<int> hs_lookup;
  set<int> hs_senders;
  BernoulliInjectionProcess * ber;
public:
  HotspotInjectionProcess(Configuration const & config, double load);


  virtual bool test(int src = 0);
};

#endif 
