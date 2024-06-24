// $Id$

/*
 Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
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

#ifndef _INJECTION_HPP_
#define _INJECTION_HPP_

#include "config_utils.hpp"
#include "wkld_comp.hpp"

using namespace std;

class InjectionProcess : public WorkloadComponent {
protected:
  int _nodes;
  vector<double> _rate;
  InjectionProcess(int nodes, vector<double> rate);
public:
  virtual ~InjectionProcess() {}
  virtual bool test(int source) = 0;
  virtual void reset();

  // new interface for injectors that want to provide all packet info, return false if not implemented
  virtual WorkloadMessagePtr get(int source) { return NULL; }
  virtual void print_stats() {}
  virtual void set_state(int node, float val) {}

  static InjectionProcess * New(string const & inject, int nodes, vector<double> load,
				Configuration const * const config = NULL);
};

class BernoulliInjectionProcess : public InjectionProcess {
public:
  BernoulliInjectionProcess(int nodes, vector<double> rate);
  virtual bool test(int source);
};

class OnOffInjectionProcess : public InjectionProcess {
private:
  vector<double> _alpha;
  vector<double> _beta;
  vector<double> _r1;
  vector<int> _initial;
  vector<int> _state;
public:
  OnOffInjectionProcess(int nodes, vector<double> rate, vector<double> alpha, vector<double> beta,
			vector<double> r1, vector<int> initial);
  virtual void reset();
  virtual bool test(int source);
};

#endif
