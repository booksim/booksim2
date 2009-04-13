#ifndef LFQUEUE_HPP
#define LFQUEUE_HPP

#ifndef lfqueue
#define lfqueue lfqueue
#endif

#include "simqueue.hpp"
#include "LFNode.hpp"
#include <iostream>

using namespace std;

template <class T>
class lfqueue : public simqueue<T> {
public:
  typedef T value_type;
  typedef int size_type;
protected:
  volatile size_type length;
  AtomicReference<LFNode<T>* > *head;
  AtomicReference<LFNode<T>* > *tail;
  
public:
  lfqueue(){
    LFNode<T> *tmp = new LFNode<T>(NULL);
    head = new AtomicReference<LFNode<T>* >(tmp);
    tail = new AtomicReference<LFNode<T>* >(tmp);
    length = 0;
  }
  
  bool empty() {
    return (length)==0;
  }

  size_type size() {
    return length;
  }

  void initialize() {
    LFNode<T> *tmp = new LFNode<T>(NULL);
    head = new AtomicReference<LFNode<T>* >(tmp);
    tail = new AtomicReference<LFNode<T>* >(tmp);
    length = 0;
  }

  ~lfqueue(){
    delete(head);
    delete(tail);
  }

  T front();

  T back();

  void push(T val);

  T pop();
};

template <class T>
T lfqueue<T>::front(){
  LFNode<T>* tmp = head->get()->next->get();
  if(tmp==(LFNode<T>*)1)
    return NULL;
  return tmp->value;
}

template <class T>
T lfqueue<T>::back(){
  if(head->get()==tail->get())
    return NULL;
  return (tail->get())->value;
}

template <class T>
void lfqueue<T>::push(T val){
  //cout << "Pushing value onto queue" << endl;
  LFNode<T> *node = new LFNode<T>(val);
  while(true){
    //cout << "Iterating push" << endl;
    LFNode<T> *last = tail->get();
    LFNode<T> *next = last->next->get();
    if(last==tail->get()){
      if(next==(LFNode<T>*)1){ //1==PSEUDONULL
	if(last->next->compareAndSet(next,node)){
	  tail->compareAndSet(last,node);
	  length++;
	  //cout << "Value added " << *(tail->get()->value) << endl;
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
  //cout << "Popping value off of queue." << endl;
  //cout << "The queue size is " << (added-removed) << endl;
  while(true){
    LFNode<T> *first = head->get();
    LFNode<T> *last = tail->get();
    LFNode<T> *next = first->next->get();
    //cout << first->value << "  " << last->value << endl;
    if(first==head->get()){
      if(first==last){
	if(next==(LFNode<T>*)1){ //1==PSUEDONULL
	  //cout << "Returning null." << endl;
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
