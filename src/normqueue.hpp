#ifndef NORMQUEUE_HPP
#define NORMQUEUE_HPP

#ifndef normqueue
#define normqueue normqueue
#endif


#include "simqueue.hpp"
#include <queue>
using namespace std;

template <class T>
class normqueue : public simqueue<T> {
protected:
  queue<T> _queue;
public:
  normqueue(){

  }

  ~normqueue(){

  }

  bool empty(){
    return _queue.empty();
  }

  int size(){
    return _queue.size();
  }

  void initialize(){

  }

  T front();
  
  T back();

  void push(T val);

  T pop();

};

template <class T>
T normqueue<T>::front(){
  return _queue.front();
}

template <class T>
T normqueue<T>::back(){
  return _queue.back();
}

template <class T>
void normqueue<T>::push(T val){
  _queue.push(val);
}

template <class T>
T normqueue<T>::pop(){
  T ret = _queue.front();
  _queue.pop();
  return ret;
}

#undef normqueue

#endif
