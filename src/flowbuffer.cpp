#include "flowbuffer.hpp"

extern bool  FAST_RETRANSMIT_ENABLE;


FlowBuffer::FlowBuffer(int src, int id, int size, bool res, flow* f){
  _src = src;
  _id = id;
  _capacity = size;
  _use_reservation = res;

  Init(f);
}

void FlowBuffer::Init( flow* f){
  fl = f;

  _vc = -1;
  _last_sn = -1;
  _tail_received = true;
  _tail_sent = true;
  _guarantee_sent = 0;
  _received = 0;
  _ready = 0;
  if(_use_reservation){
    _reservation_flit  = Flit::New();
    _reservation_flit->flid = fl->flid;
    _reservation_flit->flbid = _id;
    _reservation_flit->sn = 0;
    _reservation_flit->id = -1;
    _reservation_flit->subnetwork = 0;
    _reservation_flit->cl = 0;
    _reservation_flit->type = Flit::ANY_TYPE;
    _reservation_flit->head = true;
    _reservation_flit->tail = true;
    _reservation_flit->vc = 0;
    _reservation_flit->res_type = RES_TYPE_RES;
    _reservation_flit->pri = FLIT_PRI_RES;
    _reservation_flit->payload = fl->flow_size;

    _status= FLOW_STATUS_SPEC;
    _spec_sent = false;
  } else {
    _status =FLOW_STATUS_NORM;
    _spec_sent = true;
  }
  
  _flit_status.clear();
  _flit_buffer.clear();
  

  _watch = false;

  //stats variables
  _fast_retransmit = 0;
  _no_retransmit_loss = 0;
  _spec_outstanding = 0;
  _stats.clear();
  _stats.resize(FLOW_STAT_LIFETIME+1,0);
  _stats[FLOW_STAT_LIFETIME]=GetSimTime()-fl->create_time;
}


FlowBuffer::~FlowBuffer(){
  delete fl;
}

void FlowBuffer::update_stats(){
  _stats[FLOW_STAT_LIFETIME]++;
  

  if(send_norm_ready()){
    _stats[FLOW_STAT_NORM_READY]++;
  }else if(send_spec_ready()){
    _stats[FLOW_STAT_SPEC_READY]++;
  } else if(_received == fl->flow_size && _ready==0){
    _no_retransmit_loss++;
  } else if(_received != fl->flow_size && _ready==0){
    _stats[FLOW_STAT_NOT_READY]++;
  }

  switch(_status){
  case FLOW_STATUS_GRANT_TRANSITION:
  case FLOW_STATUS_NACK_TRANSITION:
  case FLOW_STATUS_SPEC:
    _stats[FLOW_STAT_SPEC]++;
    break;
  case FLOW_STATUS_WAIT:
    _stats[FLOW_STAT_WAIT]++;
    break;
  case FLOW_STATUS_NACK:
    _stats[FLOW_STAT_NACK]++;
    break;
  case FLOW_STATUS_NORM:
    _stats[FLOW_STAT_NORM]++;
    break;
  default:
    break;
  }

}

void FlowBuffer::update_transition(){
  switch(_status){
  case FLOW_STATUS_NACK_TRANSITION:
    if(_tail_sent){
      _status = FLOW_STATUS_NACK;
    }
    break;
  case FLOW_STATUS_GRANT_TRANSITION:
    if(_tail_sent){
      _vc=-1;
      _status = FLOW_STATUS_WAIT;
    } else {
      break;
    }
  case FLOW_STATUS_WAIT:
    if(GetSimTime()>=fl->rtime){
      _status = FLOW_STATUS_NORM;
    }
    break;
  default: 
    break;
  }
}

//when ack return
//1. packet could have already been sent normaly
//2. ack goes through

//no status change
bool FlowBuffer::ack(int sn){
  bool effective = false;
  if(_watch){
    cout<<"flow "<<fl->flid
	<<" received ack "<<sn<<endl;
  }
  for(int i = sn; _flit_buffer.count(i)!=0; ++i){
    if(_watch){
      cout<<"\tfree flit "<<i<<endl;
    }
    effective = true;
    bool tail = _flit_buffer[i]->tail;
    _guarantee_sent++;
    _spec_outstanding--;
    _flit_buffer[i]->Free();
    _flit_buffer.erase(i);
    _flit_status.erase(i);
    if(tail)
      break;
  }
  return effective;
}

//when nack return
//1. packet could have already been sent normaly
//2. nack goes through

//flow buffer status only change when in spec mode
bool FlowBuffer::nack(int sn){
  bool effective = false;
  if(_watch){
    cout<<"flow "<<fl->flid
	<<" received nack "<<sn<<endl;
  }
  if(_flit_buffer.count(sn)!=0){
    //change flit status
    for(int i = sn; _flit_buffer.count(i)!=0; ++i){
      if(_watch){
	cout<<"\tnack flit "<<i<<endl;
      }
    
      bool tail = _flit_buffer[i]->tail;
      _flit_status[i] = FLIT_NACKED;
      _ready++;
      _spec_outstanding--;
      if(tail)
	break;
    }
    
    _stats[FLOW_STAT_FINAL_NOT_READY] += _no_retransmit_loss;
    _no_retransmit_loss=0;

    //change buffer status
    if(_status == FLOW_STATUS_SPEC){
      effective = true;
      if(_tail_sent){
	_status = FLOW_STATUS_NACK;	
      } else {
	_status = FLOW_STATUS_NACK_TRANSITION;
      }
    }
  }
  return effective;
}

//only one grant can return
void FlowBuffer::grant(int time){
  if(_watch){
    cout<<"flow "<<fl->flid
	<<" received grant at time "<<time<<endl;
  }
  assert(fl->rtime == -1);
  fl->rtime = time;
  if(_tail_sent){
    _vc=-1;
    _status = FLOW_STATUS_WAIT;
  } else {
    _status = FLOW_STATUS_GRANT_TRANSITION;
  }
}

Flit* FlowBuffer::front(){
  Flit* f = NULL;

  switch(_status){
  case FLOW_STATUS_NORM:
    //not in the middle of a packet
    //search the buffer for the first available 
    if(_tail_sent){
      if( FAST_RETRANSMIT_ENABLE &&_received == fl->flow_size && _ready==0){ //search for spec packets
	for(map<int, int>::iterator i = _flit_status.begin();
	    i!=_flit_status.end(); 
	    i++){
	  if(i->second==FLIT_SPEC){
	    f= _flit_buffer[i->first];
	    assert(f->head);
	    break;
	  }
	}
      }else { //search for normal packets
	for(map<int, int>::iterator i = _flit_status.begin();
	    i!=_flit_status.end(); 
	    i++){
	  if(i->second!=FLIT_SPEC){
	    f= _flit_buffer[i->first];
	    f->res_type = RES_TYPE_NORM;
	    assert(f->head);
	    break;
	  }
	}
      }
    } else {
      
      //in the middle of a packet, pickup where last left off
      assert(_flit_buffer.count(_last_sn+1)!=0);
      f = _flit_buffer[_last_sn+1];
      f->res_type = RES_TYPE_NORM;
      
    }
    break;
    //transitions are equivalent to spec at this stage
  case FLOW_STATUS_NACK_TRANSITION:
  case FLOW_STATUS_GRANT_TRANSITION:
  case FLOW_STATUS_SPEC:
    if(!_spec_sent){
      f = _reservation_flit;
    } else {
      if(_flit_buffer.count(_last_sn+1)!=0){
	f = _flit_buffer[_last_sn+1];
	f->res_type = RES_TYPE_SPEC;
      }
    }
    break;
  case FLOW_STATUS_NACK:
  default:
    break;
  }
  return f;
}

Flit* FlowBuffer::send(){
  Flit* f = NULL;


  switch(_status){
  case FLOW_STATUS_NORM:
    //not in the middle of a packet
    //search the buffer for the first available 
    if(_tail_sent){
      if( FAST_RETRANSMIT_ENABLE && _received == fl->flow_size && _ready==0){ //search for spec packets
	map<int, int>::iterator i;
	for(i = _flit_status.begin();
	    i!=_flit_status.end(); 
	    i++){
	  if(i->second==FLIT_SPEC){
	    _fast_retransmit++;
	    f= _flit_buffer[i->first];
	    assert(f->head);
	    break;
	  }
	}
	//convert this packet to normal
	for(;
	    i!=_flit_status.end(); 
	    i++){
	  assert(i->second==FLIT_SPEC);
	  _ready++;
	  i->second = FLIT_NORMAL;
	  if(_flit_buffer[i->first]->tail)
	    break;
	}

      }else { //search for normal packets
	for(map<int, int>::iterator i = _flit_status.begin();
	    i!=_flit_status.end(); 
	    i++){
	  if(i->second!=FLIT_SPEC){
	    f= _flit_buffer[i->first];
	    assert(f->head);
	    break;
	  }
	}    
      }  
    } else {
      //in the middle of a packet, pickup where last left off
      assert(_flit_buffer.count(_last_sn+1)!=0);
      f = _flit_buffer[_last_sn+1];
    }
    if(f){
      _flit_buffer.erase(f->sn);
      _flit_status.erase(f->sn);
      f->res_type = RES_TYPE_NORM;
      f->pri = FLIT_PRI_NORM;
      _ready--;
      _guarantee_sent++;
      _last_sn = f->sn;
      _tail_sent = f->tail;
    }
    break;

    //transitions are equivalent to spec at this stage
  case FLOW_STATUS_NACK_TRANSITION:
  case FLOW_STATUS_GRANT_TRANSITION:
  case FLOW_STATUS_SPEC:
    //first send the spec packet
    if(!_spec_sent){
      _reservation_flit->vc=0;
      _reservation_flit->time = GetSimTime();
      f = _reservation_flit;
      f->src = _src;
      f->dest = fl->dest;
      _spec_sent = true;
    } else {
      if(_flit_buffer.count(_last_sn+1)!=0){
	f = Flit::New();
	memcpy(f,_flit_buffer[_last_sn+1], sizeof(Flit));
	_flit_status[_last_sn+1] = FLIT_SPEC;
	f->res_type = RES_TYPE_SPEC;
	f->pri = FLIT_PRI_SPEC;
	_ready--;
	_spec_outstanding++;
	_last_sn = f->sn;
	_tail_sent = f->tail;
      }
    }
    break;
  case FLOW_STATUS_NACK:
  default:
    break;
  }

  if(f){
    assert(_vc!=-1);
    if(_watch){
      cout<<"flow "<<fl->flid
	  <<" sent flit sn "<<f->sn
	  <<" id "<<f->id
	  <<" type "<<f->res_type<<endl;
    } 
  }
    
  return f;
}


Flit*  FlowBuffer::receive(){
  assert(receive_ready());
  assert(!fl->data.empty());
  Flit *f = fl->data.front();
  assert(f);
  fl->data.pop();

  f->flbid = _id;
  _flit_status[f->sn]=FLIT_NORMAL;
  _flit_buffer[f->sn]=f;
  _ready++;
  _received++;
  if(f->tail){
    _tail_received = true;
  } else {
    _tail_received = false;
  }
  return f;
}

//Is the flow buffer eligbible for injection arbitration
bool FlowBuffer::eligible(){
  return _vc!=-1 &&
    (send_norm_ready() ||
     send_spec_ready());
}

//can receive as long as
//0. tail for the current packet has not being received
//1. there is buffer space and
//2. there is more to be received
bool FlowBuffer::receive_ready(){
  return !_tail_received ||( ((int)_flit_buffer.size()<_capacity) && (_received < fl->flow_size));
}

bool FlowBuffer::send_norm_ready(){
  switch(_status){
  case FLOW_STATUS_NORM:
    if(!_tail_sent){
      return true;
    } else if(_received == fl->flow_size && _ready==0){ //there maybe outstanding speculative packet for retransmit
      assert(_guarantee_sent!=fl->flow_size);//otherwise this flow buffer shoudl be been freed
      return FAST_RETRANSMIT_ENABLE;
      
    } else{
      return (_ready>0);
    }
  default:
    break;
  }

  return false;
}

bool FlowBuffer::send_spec_ready(){
  switch(_status){
  case FLOW_STATUS_GRANT_TRANSITION:
  case FLOW_STATUS_NACK_TRANSITION:
  case FLOW_STATUS_SPEC:
    if(!_spec_sent){
      return true;
    } else if(!_tail_sent){
      return true;
    } else{
      return (_ready>0);
    }
  default:
    break;
  } 
  return false;
}

int FlowBuffer::done(){
  int  d = (_guarantee_sent == fl->flow_size)?FLOW_DONE_DONE:FLOW_DONE_NOT;
  if(_watch && d!=FLOW_DONE_NOT){
    cout<<"flow "<<fl->flid
	<<" ready to terminate\n";
  } 
  if(d == FLOW_DONE_DONE  && !_flow_queue.empty()){
    d=FLOW_DONE_MORE;
  }
  return  d;
}

void FlowBuffer::Reset(){
  assert(_guarantee_sent == fl->flow_size);
  assert(!_flow_queue.empty());
  delete fl;
  Init(_flow_queue.front());
  //  receive();
  _flow_queue.pop();
}

