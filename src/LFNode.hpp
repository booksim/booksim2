#ifndef LFNODE_HPP
#define LFNODE_HPP

#ifndef LFNode
#define LFNode LFNode
#endif

#include "AtomicReference.hpp"

using namespace std;

template <class T>
class LFNode {
public:
  typedef T value_type;
  value_type value;
  AtomicReference<LFNode<T>* > *next;
public:
  LFNode(const T val){
    value = val;
    next = new AtomicReference<LFNode<T>* >(NULL);
  }

  ~LFNode(){
    
  }

};


#undef LFNode

#endif
