#include "flowbuffer.hpp"



FlowBuffer::FlowBuffer(){
  _head = 0; _tail = 0; _size = 0; _capacity = INJECTION_BUFFER_SIZE;
  _spec_position = 0; _status= FLOW_STATUS_SPEC;
  _flit_buffer  = new Flit*[INJECTION_BUFFER_SIZE];
  _spec_sent = 0;
}

FlowBuffer::~FlowBuffer(){
  delete [] _flit_buffer;
}

void FlowBuffer::nack(){
  _spec_position = _head; 
  _spec_sent = 0;
}
void FlowBuffer::reset(){
  _spec_position = _head; _status= FLOW_STATUS_SPEC; _spec_sent = 0;
}

void FlowBuffer::inc_spec(){
  _spec_position = (_spec_position+1)%_capacity;
  _spec_sent++;
}
Flit* FlowBuffer::front() { 
  if(_size == 0){
    return NULL;
  } else {
    return _flit_buffer[_head];
  }
}
flow* FlowBuffer::front_flow(){
  flow* f = NULL;
  if(!_flow_buffer.empty())
    f = _flow_buffer.front();
  return f; 
}
Flit* FlowBuffer::back(){
  if(_size == 0){
    return NULL;
  } else {
    return _flit_buffer[(_tail+_capacity-1)%_capacity];
  }
}
Flit* FlowBuffer::get_spec(int flid){
  Flit* f = _flit_buffer[_spec_position];
  if(f && f->flid!=flid){
    f = NULL;
  }
  return f;
}
int FlowBuffer::size(){
  return _size;
}
bool FlowBuffer::empty(){
  return (_size == 0);
}
bool FlowBuffer::full(){
  return (_size==_capacity);
}
void FlowBuffer::push_flow(flow* f){
  _flow_buffer.push_back(f);
}
void FlowBuffer::pop_flow(){
  assert(!_flow_buffer.empty());
  _flow_buffer.pop_front();
}
void FlowBuffer::push_back(Flit * f){
  assert(_size<=_capacity);
  _flit_buffer[_tail] =f;
  _tail = (_tail+1)%_capacity;
  _size++;
}
void FlowBuffer::pop_front(){
  assert(_size !=0);
  _flit_buffer[_head] =NULL;
  _head = (_head+1)%_capacity; 
  _size --;
    
}
bool FlowBuffer::remove_packet(){
  assert(_size !=0);
  int pointer = _head; 
  bool done = false;
  bool flow_done = false;
  do{
    flow_done = _flit_buffer[pointer]->flow_tail;
    //end of packet or end of the buffer
    done = _flit_buffer[pointer]->tail;
    _flit_buffer[pointer]->Free();
  
    _flit_buffer[pointer] = NULL;
    _head = (_head+1)%_capacity; 
    _size--;
    pointer = (pointer+1)%_capacity; 
      
  }while(!done && pointer!=_tail);
  return flow_done;
}
