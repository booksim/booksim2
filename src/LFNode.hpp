#ifndef LFNODE_HPP
#define LFNODE_HPP

#ifndef LFNode
#define LFNode LFNode
#endif

#include <pthread.h>
#include <cstdlib>

using namespace std;

template <class T>
class LFNode {
public:
  typedef T value_type;
  value_type value;
  LFNode<T> *next;
protected:
  pthread_mutex_t* value_lock;
public:
  LFNode(T val){
    value = val;
    next = NULL;
    value_lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(value_lock,0);
  }

  ~LFNode(){
    pthread_mutex_destroy(value_lock);
    free(value_lock);
  }

  bool compareAndSet(LFNode<T> *expectedReference, LFNode<T> *newReference);

  T get();
};

template <class T>
bool LFNode<T>::compareAndSet(LFNode<T> *expectedReference, LFNode<T> *newReference){
  bool retValue;
  pthread_mutex_lock(value_lock);
  if(next==expectedReference){
    next=newReference;
    retValue=true;
  }
  else{
    retValue=false;
  }
  pthread_mutex_unlock(value_lock);
  return retValue;
}

template <class T>
T LFNode<T>::get(){
  return value;
}

#undef LFNode

#endif
