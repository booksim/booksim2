#include "BooksimConsumer.hpp"  

BooksimConsumer::BooksimConsumer(){
  g_eventQueue_ptr->scheduleEvent(this, 1); // Execute in the next cycle.
  
}
BooksimConsumer::~BooksimConsumer(){


}
void BooksimConsumer::wakeup(){
  printf("booksim wakeup\n");

  g_eventQueue_ptr->scheduleEvent(this, 1); // Execute in the next cycle.
}
void BooksimConsumer::print(ostream& out) const{
  out<<"Booksim: in your simulator, consuming your packets\n";
}
