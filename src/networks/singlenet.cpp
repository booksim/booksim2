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

/*singlenet.cpp
 *
 *single router network
 *
 *replaced by crossbar
 */

#include "booksim.hpp"
#include <vector>

#include "singlenet.hpp"

SingleNet::SingleNet( const Configuration &config, const string & name ) :
Network( config, name )
{
  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}

void SingleNet::_ComputeSize( const Configuration &config )
{
  _sources = config.GetInt( "in_ports" );
  _dests   = config.GetInt( "out_ports" );

  _size     = 1;
  _channels = 0;
}
void SingleNet::RegisterRoutingFunctions() {

}
void SingleNet::_BuildNet( const Configuration &config )
{
  int i;

  _routers[0] = Router::NewRouter( config, this, "router", 0, 
				   _sources, _dests );

  for ( i = 0; i < _sources; ++i ) {
    _routers[0]->AddInputChannel( _inject[i], _inject_cred[i] );
  }

  for ( i = 0; i < _dests; ++i ) {
    _routers[0]->AddOutputChannel( _eject[i], _eject_cred[i] );
  }
}

void SingleNet::Display( ) const
{
  _routers[0]->Display( );
}

