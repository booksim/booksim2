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

/*stats.cpp
 *
 *class stores statistics gnerated by the trafficmanager such as the latency
 *hope count of the the flits
 *
 *reset option resets the min and max alues of this statistiscs
 */

#include "booksim.hpp"
#include <math.h>
#include <iostream>
#include <stdio.h>
#include "stats.hpp"

Stats::Stats( Module *parent, const string &name,
	      double bin_size, int num_bins ) :
  Module( parent, name ),
  _num_bins( num_bins ), _bin_size( bin_size )
{
  _hist = new int [_num_bins];

  Clear( );
}

Stats::~Stats( )
{
  delete [] _hist;
}

void Stats::Clear( )
{
  _num_samples = 0;
  _sample_sum  = 0.0;

  for ( int b = 0; b < _num_bins; ++b ) {
    _hist[b] = 0;
  }

  _reset = true;
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

int Stats::NumSamples( ) const
{
  return _num_samples;
}

void Stats::AddSample( double val )
{
  int b;

  _num_samples++;
  _sample_sum += val;

  if ( _reset ) {
    _reset = false;
    _max = val;
    _min = val;
  } else {
    if ( val > _max ) { _max = val; }
    if ( val < _min ) { _min = val; }
  }

  b = (int)floor( val / _bin_size );

  if ( b < 0 ) { b = 0; }
  else if ( b >= _num_bins ) { b = _num_bins - 1; }

  _hist[b]++;
}

void Stats::AddSample( int val )
{
  AddSample( (double)val );
}

void Stats::Display( ) const
{
  int b;

  cout << "bins = [ "<<0<<" ..." << _num_bins-1<<"];" << endl;

  cout << "freq = [ ";
  for ( b = 0; b < _num_bins; ++b ) {
    cout << _hist[b] << " ";
  }
  cout << "];" << endl;

 FILE *ostream=fopen("stat","w");


 for ( b = 0; b < _num_bins; ++b ) {
 	int temp =	(int)(b*_bin_size);
	fprintf(ostream,"%d   %d \n",temp,_hist[b] );	
  }

  fclose(ostream);
}

void Stats::MergeStats(Stats * that){
  
  this->_num_samples += that->_num_samples;
  this->_sample_sum += that->_sample_sum;

  if ( that->_max > this->_max ) { this->_max = that->_max; }
  if ( that->_min < this->_min ) { this->_min = that->_min; }

  for(int b = 0; b<_num_bins; b++){
    this->_hist[b] +=that->_hist[b];
  }
  that->Clear();
}
