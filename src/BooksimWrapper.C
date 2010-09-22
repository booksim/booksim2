#include "BooksimWrapper.hpp"

BooksimWrapper::BooksimWrapper(int nodes){
  
  m_nodes = MachineType_base_number(MachineType_NUM);
  m_virtual_networks = NUMBER_OF_VIRTUAL_NETWORKS;
  booksim_consumer = new BooksimConsumer(nodes);
}

BooksimWrapper::~BooksimWrapper(){
  
  
  
}

MessageBuffer* BooksimWrapper::getToNetQueue(NodeID id, bool ordered, int network_num){
  return 0;
}

MessageBuffer* BooksimWrapper::getFromNetQueue(NodeID id, bool ordered, int network_num){
  return 0;
}

void BooksimWrapper::makeOutLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int link_weight, int bw_multiplier, bool isReconfiguration){

}
void BooksimWrapper::makeInLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int bw_multiplier, bool isReconfiguration){

}
void BooksimWrapper::makeInternalLink(SwitchID src, NodeID dest, const NetDest& routing_table_entry, int link_latency, int link_weight, int bw_multiplier, bool isReconfiguration){

}


void BooksimWrapper::printStats(ostream& out) const{

}
void BooksimWrapper::clearStats(){

}
void BooksimWrapper::printConfig(ostream& out) const{

}
void BooksimWrapper::reset(){

}
void BooksimWrapper::print(ostream& out) const{

}
