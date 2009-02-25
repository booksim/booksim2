#ifndef LFQUEUE_HPP
#define LFQUEUE_HPP

#ifndef lfqueue
#define lfqueue lfqueue
#endif

using namespace std;

template <class T>
class lfqueue {
public:
  typedef T value_type;
  typedef int size_type;
protected:
  size_type length;
  value_type head;
  value_type tail;
  
public:
  lfqueue(){

  }
  
  bool empty() const {
    return length==0;
  }

  size_type size() const {
    return length;
  }

  void pop() {

  }

  ~lfqueue(){

  }

  T front();

  T back();

  void push(const T x);
};

template <class T>
T lfqueue<T>::front(){
  return head;
}

template <class T>
T lfqueue<T>::back(){
  return tail;
}

template <class T>
void lfqueue<T>::push(const T x){

}

#undef lfqueue

#endif
