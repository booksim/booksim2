#ifndef LFQUEUE_HPP
#define LFQUEUE_HPP

#ifndef lfqueue
#define lfqueue lfqueue
#endif

#include "LFNode.hpp"

using namespace std;

template <class T>
class lfqueue {
public:
  typedef T value_type;
  typedef int size_type;
protected:
  size_type length;
  AtomicReference<LFNode<T>* > *head;
  AtomicReference<LFNode<T>* > *tail;
  
public:
  lfqueue(){
    LFNode<T> *tmp = new LFNode<T>(NULL);
    head = new AtomicReference<LFNode<T>* >(tmp);
    tail = head;
    length = 0;
  }
  
  bool empty() const {
    return length==0;
  }

  size_type size() const {
    return length;
  }

  void initialize() {
    LFNode<T> *tmp = new LFNode<T>(NULL);
    head = new AtomicReference<LFNode<T>* >(tmp);
    tail = head;
    length = 0;
  }

  ~lfqueue(){
    
  }

  T front();

  T back();

  void push(const T val);

  T pop();
};

template <class T>
T lfqueue<T>::front(){
  return (head->get())->value;
}

template <class T>
T lfqueue<T>::back(){
  return (tail->get())->value;
}

template <class T>
void lfqueue<T>::push(const T val){
  LFNode<T> *node = new LFNode<T>(val);
  while(true){
    LFNode<T> *last = tail->get();
    LFNode<T> *next = last->next->get();
    if(last==tail->get()){
      if(next==NULL){
	if(last->next->compareAndSet(next,node)){
	  tail->compareAndSet(last,node);
	  length++;
	  return;
	}
      }
      else{
	tail->compareAndSet(last,next);
      }
    }
  }
}

template <class T>
T lfqueue<T>::pop(){
  while(true){
    LFNode<T> *first = head->get();
    LFNode<T> *last = tail->get();
    LFNode<T> *next = first->next->get();
    if(first==head->get()){
      if(first==last){
	if(next==NULL){
	  return NULL;
	}
	tail->compareAndSet(last,next);
      }
      else{
	T val = next->value;
	if(head->compareAndSet(first,next)){
	  delete(first);
	  length--;
	  return val;
	}
      }
    }
  }
}

#undef lfqueue

#endif
