#ifndef ATOMIC_REFERENCE_HPP
#define ATOMIC_REFERENCE_HPP

#ifndef AtomicReference
#define AtomicReference AtomicReference
#endif

#include <cstdlib>
#include <pthread.h>

using namespace std;

template <class T>
class AtomicReference {
public:
  typedef T value_type;
protected:
  value_type value;
  pthread_mutex_t* value_lock;
public:
  AtomicReference(T val){
    value = val;
    value_lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(value_lock,0);
  }

  ~AtomicReference(){
    pthread_mutex_destroy(value_lock);
    free(value_lock);
  }

  bool compareAndSet(T expectedReference, T newReference);

  T get();

};

template <class T>
bool AtomicReference<T>::compareAndSet(T expectedReference, T newReference){
  /*bool retValue;
  pthread_mutex_lock(value_lock);
  if(value==expectedReference){
    value = newReference;
    retValue=true;
  }
  else{
    retValue=false;
  }
  pthread_mutex_unlock(value_lock);
  return retValue; */
  return __sync_bool_compare_and_swap(&value, expectedReference, newReference);
}

template <class T>
T AtomicReference<T>::get(){
  return value;
}


#undef AtomicReference

#endif
