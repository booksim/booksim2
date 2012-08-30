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

#include <sstream>

#include "reservation.hpp"
#include "globals.hpp"
#include "booksim.hpp"
#include "buffer.hpp"

Buffer::Buffer(Module *parent, const string& name) :
Module( parent, name ){
  
 }

Buffer::Buffer( const Configuration& config, int outputs, 
		Module *parent, const string& name ) :
Module( parent, name ), _shared_count(0)
{

  _vc_size = config.GetInt( "vc_buf_size" );
  _spec_vc_size = config.GetInt("reservation_spec_vc_size");
  _spec_vc_size = (_spec_vc_size==0)?(_vc_size):(_spec_vc_size);
  _shared_size = config.GetInt( "shared_buf_size" );

  int num_vcs = config.GetInt( "num_vcs" );

  _vc.resize(num_vcs);

  for(int i = 0; i < num_vcs; ++i) {
    ostringstream vc_name;
    vc_name << "vc_" << i;
    _vc[i] = new VC(config, outputs, this, vc_name.str( ) );
  }
}

Buffer::~Buffer()
{
  for(vector<VC*>::iterator i = _vc.begin(); i != _vc.end(); ++i) {
    delete *i;
  }
}

bool Buffer::AddFlit( int vc, Flit *f )
{
  VC * v = _vc[vc];
  if(v->GetSize() < _vc_size) {
    return v->AddFlit(f);
  } else if(_shared_count < _shared_size) {
    _shared_count++;
    return v->AddFlit(f);
  }
  return false;
}

Flit *Buffer::RemoveFlit( int vc )
{
  VC * v = _vc[vc];
  if(v->GetSize() > _vc_size) {
    --_shared_count;
  }
  return _vc[vc]->RemoveFlit( );
}

bool Buffer::Full( int vc ) const
{
  return (_shared_count >= _shared_size) && _vc[vc]->Full( );
}

void Buffer::Display( ostream & os ) const
{
  for(vector<VC*>::const_iterator i = _vc.begin(); i != _vc.end(); ++i) {
    (*i)->Display(os);
  }
}
