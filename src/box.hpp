/** 
 *  @author github.com/Stfort52
 */
#ifndef _BOX_HPP_
#define _BOX_HPP_

#include <stdint.h>
#include <stack>

template <class T>
class Box
{
public:
  Box() {};
  ~Box() {};

  virtual T *NewItem();
  virtual void RetireItem(T *t);
  virtual void RetireAll();
  virtual void DestroyAll();

  virtual uint32_t BoxSize();

protected:
  std::stack<T *> all_items;
  std::stack<T *> free_items;
};

template <class T>
T *Box<T>::NewItem()
{
  T *t;
  if (free_items.empty())
  {
    t = new T();
    all_items.push(t);
  } 
  else
  {
    t = free_items.top();
    t->Reset();
    free_items.pop();
  }
  return t;
}

template <class T> 
void Box<T>::RetireItem(T *t)
{
  free_items.push(t);
} 

template <class T> 
void Box<T>::RetireAll()
{
  free_items = all_items;
} 

template <class T>
void Box<T>::DestroyAll()
{
  while(!all_items.empty())
  {
    auto f = all_items.top();
    all_items.pop();
    delete f;
  }
  while(!free_items.empty())
  {
    free_items.pop();
  }
}

template <class T>
uint32_t Box<T>::BoxSize()
{
  return all_items.size();
}


#endif /* _BOX_HPP_ */