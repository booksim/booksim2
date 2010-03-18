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

#ifndef _ALLOCATOR_HPP_
#define _ALLOCATOR_HPP_

#include <string>
#include <list>

#include "module.hpp"
#include "config_utils.hpp"

class Allocator : public Module {
protected:
  const int _inputs;
  const int _outputs;

  int *_inmatch;
  int *_outmatch;

  int *_outmask;

  void _ClearMatching( );
public:

  struct sRequest {
    int port;
    int label;
    int in_pri;
    int out_pri;
  };

  Allocator( Module *parent, const string& name,
	     int inputs, int outputs );
  virtual ~Allocator( );

  virtual void Clear( ) = 0;
  
  virtual int  ReadRequest( int in, int out ) const = 0;
  virtual bool ReadRequest( sRequest &req, int in, int out ) const = 0;

  virtual void AddRequest( int in, int out, int label = 1, 
			   int in_pri = 0, int out_pri = 0 ) = 0;
  virtual void RemoveRequest( int in, int out, int label = 1 ) = 0;
  
  virtual void Allocate( ) = 0;

  void MaskOutput( int out, int mask = 1 );

  int OutputAssigned( int in ) const;
  int InputAssigned( int out ) const;
  virtual void PrintRequests( ostream * os = NULL ) const = 0;

  static Allocator *NewAllocator( Module *parent, const string& name,
				  const string &alloc_type, 
				  int inputs, int outputs,
				  int iters, const string &arb_type );
};

//==================================================
// A dense allocator stores the entire request
// matrix.
//==================================================

class DenseAllocator : public Allocator {
protected:
  sRequest **_request;

public:
  DenseAllocator( Module *parent, const string& name,
		  int inputs, int outputs );
  virtual ~DenseAllocator( );

  void Clear( );
  
  int  ReadRequest( int in, int out ) const;
  bool ReadRequest( sRequest &req, int in, int out ) const;

  void AddRequest( int in, int out, int label = 1, 
		   int in_pri = 0, int out_pri = 0 );
  void RemoveRequest( int in, int out, int label = 1 );

  void PrintRequests( ostream * os = NULL ) const;
};

//==================================================
// A sparse allocator only stores the requests
// (allows for a more efficient implementation).
//==================================================

class SparseAllocator : public Allocator {
protected:
  list<int> _in_occ;
  list<int> _out_occ;
  
  list<sRequest> *_in_req;
  list<sRequest> *_out_req;

public:
  SparseAllocator( Module *parent, const string& name,
		   int inputs, int outputs );
  virtual ~SparseAllocator( );

  void Clear( );
  
  int  ReadRequest( int in, int out ) const;
  bool ReadRequest( sRequest &req, int in, int out ) const;

  void AddRequest( int in, int out, int label = 1, 
		   int in_pri = 0, int out_pri = 0 );
  void RemoveRequest( int in, int out, int label = 1 );
  
  void PrintRequests( ostream * os = NULL ) const;
};

#endif
