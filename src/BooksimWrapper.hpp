/*
  Network subclass wrapper to link booksim into GEMs system.
  For the actual network lookinto BooksimConsumer
*/


#ifndef BOOKSIMWRAPPER_H
#define BOOKSIMWRAPPER_H

#include "booksim.hpp"
#include "globals.hpp"
#include <iostream>
#include <vector>

#include "network/network.h"
#include "network/networkSim.h"
using namespace SS_Network;

class BooksimWrapper;

class BooksimInterface: public Interface{
public:
  BooksimInterface(string name, SystemConfig* sysCon, Fwk::Log* log, int id);
  //Enqueue a message from a link, source is a pointer to the object that is enqueing the message
  virtual void messageIs(SS_Network::Message *msg, const Link *source){}
  //return the top message from a virtual channel, returns null if no message
  virtual SS_Network::Message* message(int virtualChannel) const{return 0;}
  //returns the number of messages buffered for output in a virtual channel
  virtual int count(int virtualChannel) const{return 0;}
  //pop the top message from the queue
  virtual void pop(int virtualChannel){}
  //Perform a routing step. Depending on the implementation this may output something or not
  virtual void route();
  //If this returns zero, that means something happens next cycle
  //If it returns Time::Future, it means the interface is idle
  virtual Time nextEventTime(){return Time::Future();}
  virtual int id() const {return _id;}
  void setParent(BooksimWrapper* a){parent = a;}

  
  //stats
  typedef std::map<string, int> Stats;
  virtual void report(Fwk::Log* log) const {}
  virtual void report(Fwk::Log* log, Stats &s) const {report(log);}
  virtual void clearStats(){}
private:
  BooksimWrapper* parent;
};



class BooksimCoreInterface : public CoreInterface{
public:
  BooksimCoreInterface(string name, SystemConfig* sysCon, Fwk::Log* log, int id, Activity::Ptr a);

 

  //Enqueue a message from the core
  virtual void messageIs(SS_Network::Message *msg);
  //return the top message from a virtual channel, returns null if no message
private:
  Activity::Ptr mainbooksima;
};





class BooksimWrapper : public NetworkSim {
public:
  // Constructors
  BooksimWrapper(SystemConfig* sysCon, Fwk::Log* log);
  BooksimWrapper(Activity::Manager* manager, int basePriority, SystemConfig* sysCon, Fwk::Log* log);

  // Destructor
  ~BooksimWrapper();
  
  //netsim stuff
  virtual void init(NetworkType type);
  virtual CoreInterface* coreInterface(int idx); 
  virtual void coreInterfaceIs(CoreInterface* iface, int idx);

  // Public Methods
  void printStats(ostream& out) const;
  void clearStats();
  void printConfig(ostream& out) const;
  void reset();
  void print(ostream& out) const;

  Interface* getCore(int i){return _coreInterfaces[i].ptr();}
private:

  BooksimInterface* mainbooksim;
  Activity::Ptr mainbooksima;
  InterfaceActivityReactor::Ptr mainbooksimr;
};


#endif
