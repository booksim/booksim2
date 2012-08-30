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

/*credit.cpp
 *
 *A class for credits
 */

#include "booksim.hpp"
#include "credit.hpp"
#include <set>

stack<Credit *> Credit::_all;
stack<Credit *> Credit::_free;
Credit::Credit()
{
  Reset();
}

void Credit::Reset()
{
  vc.clear();
  head = false;
  tail = false;
  id   = -1;
  cr_time = -1;
  delay = 0;
}

Credit * Credit::New() {
  Credit * c;
  if(_free.empty()) {
    c = new Credit();
    _all.push(c);
  } else {
    c = _free.top();
    c->Reset();
    _free.pop();
  }
  return c;
}

void Credit::Free() {
  _free.push(this);
}

void Credit::FreeAll() {
  while(!_all.empty()) {
    delete _all.top();
    _all.pop();
  }
}

int Credit::OutStanding(){
  return _all.size()-_free.size();
}

Credit*  Credit::Diff(){
  set<Credit*> free_set;
  set<Credit*> all_set;
  while(!_free.empty()){
    free_set.insert(_free.top());
    _free.pop();
  }
  Credit* found = NULL;
  while(!_all.empty()){
    if(free_set.count(_all.top())){
      found = _all.top();
    }
    all_set.insert(_all.top());
    _all.pop();
  }
  
  size_t limit = free_set.size();
  for(size_t i = 0; i<limit; i++){
    _free.push(*free_set.begin());
    free_set.erase(free_set.begin());
  }
    limit = all_set.size();
  for(size_t i = 0; i<limit; i++){
    _all.push(*all_set.begin());
    all_set.erase(all_set.begin());
  }

  return found;
}

