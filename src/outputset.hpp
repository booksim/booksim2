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

#ifndef _OUTPUTSET_HPP_
#define _OUTPUTSET_HPP_

#include <queue>
#include <list>

class OutputSet {


public:
  struct sSetElement {
    int vc_start;
    int vc_end;
    int pri;
    int output_port;
  };
  OutputSet( int num_outputs );
  ~OutputSet( );

  void Clear( );
  void Add( int output_port, int vc, int pri = 0 );
  void AddRange( int output_port, int vc_start, int vc_end, int pri = 0 );

  int Size( ) const;
  bool OutputEmpty( int output_port ) const;
  int NumVCs( int output_port ) const;
  
  const list<sSetElement>* GetSetList() const;

  int  GetVC( int output_port,  int vc_index, int *pri = 0 ) const;
  bool GetPortVC( int *out_port, int *out_vc ) const;
private:
  list<sSetElement> _outputs;
};

#endif


