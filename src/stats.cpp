// $Id$

/*
Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
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

/*stats.cpp
 *
 *class stores statistics gnerated by the trafficmanager such as the latency
 *hope count of the the flits
 *
 *reset option resets the min and max alues of this statistiscs
 */

#include "booksim.hpp"
#include <iostream>
#include <sstream>
#include <limits>
#include <cmath>
#include <cstdio>

#include "stats.hpp"

Stats::Stats( Module *parent, const string &name,
	      double bin_size, int num_bins ) :
  Module( parent, name ),
   _num_bins( num_bins ), _bin_size( bin_size ), _hist(_num_bins, 0), _num_samples(0), _sample_sum(0.0), _min(numeric_limits<double>::max()), _max(numeric_limits<double>::min())
{

}

void Stats::Clear( )
{
  _num_samples = 0;
  _sample_sum  = 0.0;

  _hist.assign(_num_bins, 0);

  _min = numeric_limits<double>::max();
  _max = numeric_limits<double>::min();
  
  //  _reset = true;
}

double Stats::Average( ) const
{
  return _sample_sum / (double)_num_samples;
}

double Stats::Min( ) const
{
  return _min;
}

double Stats::Max( ) const
{
  return _max;
}

double Stats::Sum( ) const
{
  return _sample_sum;
}

int Stats::NumSamples( ) const
{
  return _num_samples;
}

void Stats::AddSample( double val )
{
  int b;

  _num_samples++;
  _sample_sum += val;

  _max = fmax(val, _max);
  _min = fmin(val, _min);

  //double clamp between 0 and num_bins-1
  b = (int)fmax(floor( val / _bin_size ), 0.0);
  b = (b >= _num_bins) ? (_num_bins - 1) : b;

  _hist[b]++;
}

void Stats::AddSample( int val )
{
  AddSample( (double)val );
}

void Stats::Display( ) const
{
  cout << *this << endl;
}

ostream & operator<<(ostream & os, const Stats & s) {
  os << s._hist;
  return os;
}
