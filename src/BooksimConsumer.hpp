#ifndef BOOKSIMCONSUMER_HPP
#define BOOKSIMCONSUMER_HPP

#include "Global.h"
#include "Consumer.h"
#include "booksim_config.hpp"
#include "gemstrafficmanager.hpp"
#include "network.hpp"
#include <vector>
#include "Vector.h"
#include "MessageBuffer.h"
class BooksimWrapper;

class BooksimConsumer : public Consumer{

public:
  BooksimConsumer(int nodes, int vcs);
  ~BooksimConsumer();
  void wakeup();
  void print(ostream& out) const;
  void printStats(ostream& out) const;
  void printConfig(ostream& out) const;

  void setWrapper(BooksimWrapper* w){
    wrapper = w;
  }
  
  void RegisterMessageBuffers(  Vector<Vector<MessageBuffer*> >* in,   Vector<Vector<MessageBuffer*> >* out);
private:
  BooksimWrapper* wrapper;
  GEMSTrafficManager* manager;
  Configuration* booksimconfig;
  vector<BSNetwork*> net;
  int next_report_time;
};
#endif


