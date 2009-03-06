#ifndef SIMQUEUE_HPP
#define SIMQUEUE_HPP

#ifndef simqueue
#define simqueue simqueue
#endif

using namespace std;

template <class T>
class simqueue {
public:
  virtual bool empty() = 0;
  virtual int size() = 0;
  virtual void initialize() = 0;
  virtual T front() = 0;
  virtual T back() = 0;
  virtual void push(T) = 0;
  virtual T pop() = 0;
};

#undef simqueue

#endif
