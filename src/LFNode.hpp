#ifndef LFNODE_HPP
#define LFNODE_HPP

#ifndef LFNode
#define LFNode LFNode
#endif

#include "AtomicReference.hpp"

using namespace std;

template <class T>
class LFNode : public AtomicReference<T> {
public:
  typedef T value_type;
  value_type value;
  LFNode<T> *next;

  LFNode(T val){
    value = val;
    next = new LFNode(NULL);
  }

  ~LFNode(){
    delete(next);
  }

  AtomicReference<T>* get();
};

template <class T>
AtomicReference<T>* LFNode<T>::get(){
  return this;
}

#undef LFNode

#endif
