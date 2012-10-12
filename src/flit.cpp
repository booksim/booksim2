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

/*flit.cpp
 *
 *flit struct is a flit, carries all the control signals that a flit needs
 *Add additional signals as necessary. Flits has no concept of length
 *it is a singluar object.
 *
 *When adding objects make sure to set a default value in this constructor
 */

#include "booksim.hpp"
#include "globals.hpp"
#include "flit.hpp"


stack<Flit *> Flit::_all;
stack<Flit *> Flit::_free;

ostream& operator<<( ostream& os, const Flit& f )
{
  os << "  Flit ID: " << f.id << " (" << &f << ")" 
     << " Packet ID: " << f.pid
    //     << " Transaction ID: " << f.tid
     << " Type: " << f.type 
     << " Head: " << f.head
     << " Tail: " << f.tail << endl;
  os << "  Source: " << f.src << "  Dest: " << f.dest << " Intm: "<<f.intm<<endl;
  os << "  Injection time: " << f.time << " Transaction start: "  << "Arrival time: " << f.atime << " Phase: "<<f.ph<< endl;
  os << "  VC: " << f.vc << endl;
  return os;
}


Flit::Flit() 
{  
  pb=NULL;
  Reset();
}  

void Flit::Reset() 
{  
  walkin=true;
  fecn = false;
  becn = false;
  flbid = -1;
  inuse = false;
  res_type = RES_TYPE_SPEC;
  type      = ANY_TYPE ;
  vc        = -1 ;
  cl        = -1 ;
  head      = false ;
  tail      = false ;
  ntime = -1;
  time      = -1 ;
  atime     = -1 ;
  exptime =-1;
  sn        = -1 ;
  head_sn = -1;
  id        = -1 ;
  pid       = -1 ;
  flid = -1;
  hops      = 0 ;
  watch     = false ;
  record    = false ;
  intm = 0;
  src = -1;
  dest = -1;
  pri = 0;
  intm =-1;
  ph = -1;
  minimal = 1;
  payload = -1;
  packet_size=0;
  if(pb)
    pb->Free();
  pb=NULL;

#ifdef FLIT_HOP_LATENCY 
  arrival_stamp=0;
  while(!hop_lat.empty()){
    hop_lat.pop();
  }
#endif

}  

Flit * Flit::Replicate(Flit* f){
  Flit* r = New();

  r->flid  = f->flid;  
  r->id    = f->id+1;
  r->pid   = f->pid;
  assert(f->packet_size>0);
  r->packet_size = f->packet_size-1;
  f->packet_size = 0;

  //r->watch = f->watch;
  r->subnetwork = f->subnetwork;
  r->src    = f->src;
  r->time   = f->time;

  r->record = f->record;
  r->cl     = f->cl;
  r->sn     = f->sn+1;

  
  r->type   = f->type;
  r->pri    = f->pri;
  r->head_sn  = f->head_sn;
  r->res_type = f->res_type;
  r->walkin = f->walkin;

  r->vc = f->vc;
  return r;
}

Flit * Flit::New() {
  Flit * f;
  if(_free.empty()) {
    //better tomake a few more?
    for(int i = 0; i<100; i++){
      f= new Flit;
      _free.push(f);
      _all.push(f);
    }
    if(_all.size()>10000000){
      cerr<<"Simulation time "<<GetSimTime()<<" flit allocation exceeds "<<_all.size()<<endl;
      exit(-1);
    }
  }

  f = _free.top();
  assert(!f->inuse);
  f->Reset();
  _free.pop();
  f->inuse = true;
  return f;
}

void Flit::Free() {
  this->inuse = false;
  _free.push(this);
}

void Flit::FreeAll() {
  while(!_all.empty()) {
    _all.top()->Reset();
    delete _all.top();
    _all.pop();
  }
}


int Flit::OutStanding(){
  return _all.size()-_free.size();
}



stack<PiggyPack *> PiggyPack::_all;
stack<PiggyPack *> PiggyPack::_free;
int PiggyPack::_size=0;
PiggyPack::PiggyPack(){
  _data=NULL;
}
PiggyPack * PiggyPack::New() {
  PiggyPack * p;
  if(_free.empty()) {
    //better tomake a few more?
    for(int i = 0; i<100; i++){
      p= new PiggyPack;
      p->_data=new bool[_size];
      _free.push(p);
      _all.push(p);
    }
  }

  p = _free.top();
  p->Reset();
  _free.pop();
  return p;
}

void PiggyPack::Free() {
  _free.push(this);
}

void PiggyPack::FreeAll() {
  while(!_all.empty()) {
    delete _all.top();
    _all.pop();
  }
}

void PiggyPack::Reset(){
  assert(_data!=NULL);
  for(int i = 0; i<_size;i++){
    _data[i] = false;
  } 
}
