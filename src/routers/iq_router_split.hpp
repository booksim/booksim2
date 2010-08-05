// $Id: iq_router_split.hpp -1   $

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

#ifndef _IQ_ROUTER_SPLIT_HPP_
#define _IQ_ROUTER_SPLIT_HPP_

#include <string>

#include "module.hpp"
#include "vc.hpp"
#include "allocator.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer_state.hpp"
#include "pipefifo.hpp"
#include "iq_router_base.hpp"

class IQRouterSplit : public IQRouterBase {
  
private:

  Allocator *_sw_allocator;
  
  vector<int> _vc_rr_offset;
  vector<int> _sw_rr_offset;
  
  vector<bool> _use_fast_path;
  
protected:

  virtual void _Alloc( );

public:
  IQRouterSplit( const Configuration& config,
	    Module *parent, const string & name, int id,
	    int inputs, int outputs );
  
  virtual ~IQRouterSplit( );
  
};

#endif
