/*
  Network subclass wrapper to link booksim into GEMs system.
  For the actual network lookinto BooksimConsumer
*/


#ifndef BOOKSIMWRAPPER_H
#define BOOKSIMWRAPPER_H

#include "Global.h"
#include "Vector.h"
#include "Network.h"
#include "MessageBuffer.h"
#include "BooksimConsumer.hpp"
#include "gemstrafficmanager.hpp"
class MessageBuffer;



class BooksimWrapper : public Network {
public:
  // Constructors
  BooksimWrapper(int nodes);

  // Destructor
  ~BooksimWrapper();
  
  // Public Methods
  void printStats(ostream& out) const;
  void clearStats();
  void printConfig(ostream& out) const;
  void reset();
  void print(ostream& out) const;

  // returns the queue requested for the given component
  MessageBuffer* getToNetQueue(NodeID id, bool ordered, int network_num);
  MessageBuffer* getFromNetQueue(NodeID id, bool ordered, int network_num);
  


  // Methods used by Topology to setup the network
  void makeOutLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int link_weight, int bw_multiplier, bool isReconfiguration);
  void makeInLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int bw_multiplier, bool isReconfiguration);
  void makeInternalLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int link_weight, int bw_multiplier, bool isReconfiguration);


  Vector<Vector<MessageBuffer*> >*  GetInBuffers(){
    return &m_toNetQueues;
  }
  Vector<Vector<MessageBuffer*> >*  GetOutBuffers(){
    return &m_fromNetQueues;
  }
private:

  BooksimConsumer* booksim_consumer;
  int m_nodes ;
  int m_virtual_networks;

  
  // vector of queues from the components
  Vector<Vector<MessageBuffer*> > m_toNetQueues;
  Vector<Vector<MessageBuffer*> > m_fromNetQueues;
  
};

// Output operator declaration
ostream& operator<<(ostream& out, const BooksimWrapper& obj);

// ******************* Definitions *******************

// Output operator definition
extern inline 
ostream& operator<<(ostream& out, const BooksimWrapper& obj)
{
  obj.print(out);
  out << flush;
  return out;
}

#endif
