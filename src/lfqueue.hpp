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
  LFNode<T> *head;
  LFNode<T> *tail;
  
public:
  lfqueue(){
    head = new LFNode<T>(NULL);
    tail = head;
    length = 0;
  }
  
  bool empty() const {
    return length==0;
  }

  size_type size() const {
    return length;
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
  return head->value;
}

template <class T>
T lfqueue<T>::back(){
  return tail->value;
}

template <class T>
void lfqueue<T>::push(const T val){
  LFNode<T> *node = new LFNode<T>(val);
  while(true){
    LFNode<T> *last = tail;
    LFNode<T> *next = last->next;
    if(last==tail){
      if(next==NULL){
	if(last->compareAndSet(next,node)){
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
    LFNode<T> *first = head;
    LFNode<T> *last = tail;
    LFNode<T> *next = first->next;
    if(first==head){
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
