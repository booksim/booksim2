// $Id$

/*
Copyright (c) 2007-2010, Trustees of The Leland Stanford Junior University
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

#include <iostream>
#include <cassert>
#include "random_utils.hpp"
#include "injection.hpp"

using namespace std;

InjectionProcess::InjectionProcess(double rate)
: _rate(rate)
{
  assert((rate >= 0.0) && (rate <= 1.0));
}

vector<vector<InjectionProcess *> > InjectionProcess::Load(Configuration const & config, int nodes)
{
  int classes = config.GetInt("classes");

  vector<int> packet_size = config.GetIntArray( "packet_size" );
  if(packet_size.empty()) {
    packet_size.push_back(config.GetInt("packet_size"));
  }
  packet_size.resize(classes, packet_size.back());
  
  vector<double> load = config.GetFloatArray("injection_rate"); 
  if(load.empty()) {
    load.push_back(config.GetFloat("injection_rate"));
  }
  load.resize(classes, load.back());

  if(config.GetInt("injection_rate_uses_flits")) {
    for(int c = 0; c < classes; ++c)
      load[c] /= (double)packet_size[c];
  }

  vector<string> inject = config.GetStrArray("injection_process");
  inject.resize(classes, inject.back());
  
  vector<double> alpha = config.GetFloatArray("burst_alpha");
  if(alpha.empty()) {
    alpha.push_back(config.GetFloat("burst_alpha"));
  }
  alpha.resize(classes, alpha.back());
  vector<double> beta = config.GetFloatArray("burst_beta");
  if(beta.empty()) {
    beta.push_back(config.GetFloat("burst_beta"));
  }
  beta.resize(classes, beta.back());

  vector<vector<InjectionProcess *> > result;
  result.resize(nodes);
  for(int n = 0; n < nodes; ++n) {
    result[n].resize(classes);
    for(int c = 0; c < classes; ++c) {
      if(inject[c] == "bernoulli") {
	result[n][c] = new BernoulliInjectionProcess(load[c]);
      } else if(inject[c] == "on_off") {
	result[n][c] = new OnOffInjectionProcess(load[c], alpha[c], beta[c]);
      } else {
	cout << "Invalid injection process: " << inject[c] << endl;
      }
    }
  }
  return result;
}

//=============================================================

BernoulliInjectionProcess::BernoulliInjectionProcess(double rate)
  : InjectionProcess(rate)
{

}

bool BernoulliInjectionProcess::test()
{
  return (RandomFloat() < _rate);
}

//=============================================================

OnOffInjectionProcess::OnOffInjectionProcess(double rate, double alpha, 
					     double beta, bool initial)
  : InjectionProcess(rate), _alpha(alpha), _beta(beta), _state(initial)
{
  assert((alpha >= 0.0) && (alpha <= 1.0));
  assert((beta >= 0.0) && (beta <= 1.0));
}

bool OnOffInjectionProcess::test()
{

  // advance state

  if(!_state) {
    if(RandomFloat() < _alpha) { // from off to on
      _state = true;
    }
  } else {
    if(RandomFloat() < _beta) { // from on to off
      _state = false;
    }
  }

  // generate packet

  if(_state) { // on?
    double r1 = _rate * (_alpha + _beta) / _alpha;
    return (RandomFloat() < r1);
  }

  return false;
}
