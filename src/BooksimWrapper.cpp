#include "MachineType.h"
#include "BooksimWrapper.hpp"


BooksimWrapper::BooksimWrapper(int nodes){

  //Copied from 
  m_nodes = MachineType_base_number(MachineType_NUM);
  m_virtual_networks = NUMBER_OF_VIRTUAL_NETWORKS;


  booksim_consumer = new BooksimConsumer(m_nodes,  m_virtual_networks);
  
// Allocate to and from queues
  m_toNetQueues.setSize(m_nodes);
  m_fromNetQueues.setSize(m_nodes);
  for (int node = 0; node < m_nodes; node++) {
    m_toNetQueues[node].setSize(m_virtual_networks);
    m_fromNetQueues[node].setSize(m_virtual_networks);
    for (int j = 0; j < m_virtual_networks; j++) {
      m_toNetQueues[node][j] = new MessageBuffer;
      m_fromNetQueues[node][j] = new MessageBuffer;
      m_toNetQueues[node][j]->setConsumer(new InjectConsumer(booksim_consumer));
    }
  }

  booksim_consumer->RegisterMessageBuffers(   &m_toNetQueues, &m_fromNetQueues );
}

BooksimWrapper::~BooksimWrapper(){
  cout<<"Delete me\n";
  for (int i = 0; i < m_nodes; i++) {
    m_toNetQueues[i].deletePointers();
    m_fromNetQueues[i].deletePointers();
  }
  
  delete booksim_consumer;
  
}

MessageBuffer* BooksimWrapper::getToNetQueue(NodeID id, bool ordered, int network_num){
  return m_toNetQueues[id][network_num];
}

MessageBuffer* BooksimWrapper::getFromNetQueue(NodeID id, bool ordered, int network_num){
  return m_fromNetQueues[id][network_num];
}


void BooksimWrapper::printStats(ostream& out) const{
  booksim_consumer->printStats(out);
}
void BooksimWrapper::clearStats(){

}

void BooksimWrapper::printConfig(ostream& out) const{
  booksim_consumer->printConfig(out);
}

void BooksimWrapper::reset(){
 for (int node = 0; node < m_nodes; node++) {
    for (int j = 0; j < m_virtual_networks; j++) {
      m_toNetQueues[node][j]->clear();
      m_fromNetQueues[node][j]->clear();
    }
  }

}

void BooksimWrapper::print(ostream& out) const{
  out<<"BooksimWrapper: not much to report\n";
}

///useless stuff below here

void BooksimWrapper::makeOutLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int link_weight, int bw_multiplier, bool isReconfiguration){

}
void BooksimWrapper::makeInLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int bw_multiplier, bool isReconfiguration){

}
void BooksimWrapper::makeInternalLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int link_weight, int bw_multiplier, bool isReconfiguration){

}
