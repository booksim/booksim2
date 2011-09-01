#include "flowbuffer.hpp"
#include "trafficmanager.hpp"
#include "stats.hpp"
#include <vector>
//RES config stuff
extern bool FAST_RETRANSMIT_ENABLE;
extern int RESERVATION_PACKET_THRESHOLD;
extern int RESERVATION_CHUNK_LIMIT;

//ECN config  stuff
extern int IRD_RESET_TIMER;
extern bool ECN_TIMER_ONLY;
extern int IRD_SCALING_FACTOR;
extern int ECN_IRD_INCREASE;
extern int ECN_IRD_LIMIT;
extern bool ECN_AIMD;
extern vector<Stats*> gStatIRD;
extern bool RESERVATION_SPEC_OFF;


FlowBuffer::FlowBuffer(TrafficManager* p, int src, int id, int size, int mode, flow* f){

  parent = p;
  _dest=-1;
  _IRD = 0; 
  _IRD_timer = 0;
  _IRD_wait = 0;
  _total_wait = 0;
  Activate(src,id, size, mode,f);
}

void FlowBuffer::Activate(int src, int id, int size, int mode, flow* f){
 //ECN stuff need to be initilized once not every time or else benefits are lost
  if(mode == ECN_MODE && _dest!=f->dest){
    _IRD = 0; 
    _IRD_timer = 0;
    _IRD_wait = 0;
  }

  _active= true;
  _src = src;
  _id = id;
  _capacity = size;
  _mode = mode;

  Init(f);  
}
void FlowBuffer::Deactivate(){
  delete fl;
  _active= false;
  if(!_flow_queue.empty()){
    cerr<<"caution premature flow deactivation, hope you are running only transient\n";
    while(!_flow_queue.empty()){
      delete _flow_queue.front();
      _flow_queue.pop();
    }
  }
  _flit_status.clear();
  _flit_buffer.clear();
}

void FlowBuffer::Init( flow* f){
  fl = f;
  _ready = 0;

  _dest = f->dest;
  _reserved_time = -1;
  _vc = -1;
  _last_sn = -1;
  _tail_sent = true;
  _guarantee_sent = 0;
  if(_mode == RES_MODE && fl->flow_size>RESERVATION_PACKET_THRESHOLD){
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
    _res_outstanding = false;
    _reserved_slots = fl->flow_size;
    if(fl->flow_size>RESERVATION_CHUNK_LIMIT){
      _reservation_flit->payload = RESERVATION_CHUNK_LIMIT; 
      _reserved_slots = RESERVATION_CHUNK_LIMIT; 
      _total_reserved_slots=RESERVATION_CHUNK_LIMIT; 
    }

  } else {
    _status =FLOW_STATUS_NORM;
    _spec_sent = true;
  }
  
  _flit_status.clear();
  _flit_buffer.clear();
  
  
  fl->buffer=new  queue<Flit*>;
  parent->_GeneratePacket(fl);
  fl->buffer->front()->payload = fl->flow_size;  
  while(!fl->buffer->empty()){
    Flit *ff = fl->buffer->front();
    assert(ff);
    fl->buffer->pop();
    ff->flbid = _id;
    if(fl->flow_size>RESERVATION_PACKET_THRESHOLD)
      ff->walkin = false;
    _flit_status[ff->sn]=FLIT_NORMAL;
    _flit_buffer[ff->sn]=ff;
    _ready++;
  }




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

//generate packets for flows
void FlowBuffer::update(){
  if(_active){
    ////////////////////////////////
    ///update transiention
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
      if(GetSimTime()>=_reserved_time){
	_status = FLOW_STATUS_NORM;
      }
      break;
    default: 
      break;
    }

    ////////////////////////////////////
    //update stats
    _stats[FLOW_STAT_LIFETIME]++;
    if(send_norm_ready()){
      _stats[FLOW_STAT_NORM_READY]++;
    }else if(send_spec_ready()){
      _stats[FLOW_STAT_SPEC_READY]++;
    }else if(_ready==0){
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

    ///////////////////////
    //update the rest
    if(_ready<4 && fl->data_to_generate!=0){
      parent->_GeneratePacket(fl);
      fl->data_to_generate--;
      while(!fl->buffer->empty()){
	Flit *ff = fl->buffer->front();
	assert(ff);
	fl->buffer->pop();
	ff->flbid = _id;
	if(fl->flow_size>RESERVATION_PACKET_THRESHOLD)
	  ff->walkin = false;
	_flit_status[ff->sn]=FLIT_NORMAL;
	_flit_buffer[ff->sn]=ff;
	_ready++;
      }
    }

    //for large flows, reset and rereserve
    if(_mode ==RES_MODE && fl->flow_size>RESERVATION_PACKET_THRESHOLD){
      if(_reserved_slots==0 && _ready!=0 && _spec_outstanding ==0 && !_res_outstanding){
	_reservation_flit  = Flit::New();
	_reservation_flit->flid = fl->flid;
	_reservation_flit->flbid = _id;
	_reservation_flit->sn = _total_reserved_slots;
	_reservation_flit->id = -1;
	_reservation_flit->subnetwork = 0;
	_reservation_flit->cl = 0;
	_reservation_flit->type = Flit::ANY_TYPE;
	_reservation_flit->head = true;
	_reservation_flit->tail = true;
	_reservation_flit->vc = 0;
	_reservation_flit->res_type = RES_TYPE_RES;
	_reservation_flit->pri = FLIT_PRI_RES;

	_status= FLOW_STATUS_SPEC;
	_spec_sent = false;
	_res_outstanding=false;
	if(fl->flow_size-_total_reserved_slots<RESERVATION_CHUNK_LIMIT){
	  _reserved_slots = fl->flow_size-_total_reserved_slots;
	  _reservation_flit->payload = fl->flow_size-_total_reserved_slots;
	} else {
	  _reserved_slots = RESERVATION_CHUNK_LIMIT; 
	  _reservation_flit->payload = RESERVATION_CHUNK_LIMIT; 
	}

	_last_sn = _total_reserved_slots-1;
	_total_reserved_slots+=_reserved_slots; 
	_vc = -1;
	_reserved_time=-1;
      }
    }
  }

  /////////////////////////////////////////
  //ECN ONLY
  if(_mode==ECN_MODE){
    //update ECN fields
    //_IRD_wait inc whenever _tail_sent is asserted
    //_IRD_wait reset when _tail_sent is reasserted
    if(_tail_sent && _IRD>0){
      _IRD_wait++;
    _total_wait++;
    }
    if(_mode == ECN_MODE && _IRD >0){
      _IRD_timer++;
      if(_IRD_timer>=IRD_RESET_TIMER){
	gStatIRD[_src]->AddSample(_IRD);
	_IRD--;
	_IRD_timer = 0;
      }
    }
  }
}



//when ack return res mode
//1. packet could have already been sent normaly
//2. ack goes through

//when ack return ecn mode, "int sn" respreent the becn value

//no status change
bool FlowBuffer::ack(int sn){
  bool effective = false;
  if(_watch){
    cout<<"flow "<<fl->flid
	<<" received ack "<<sn<<endl;
  }
  if(_mode == RES_MODE){
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
  } else if(_mode == ECN_MODE){
    //if BECN is off
    if(sn ==0){
      if(!ECN_TIMER_ONLY){
	_IRD = _IRD>0?_IRD-1:0;
        _IRD_timer = 0;
      }
    }else {
      if(ECN_AIMD){
	if(_IRD==0){
	  _IRD=ECN_IRD_INCREASE;
	} else {
	  _IRD*=2;
	  if(_IRD>ECN_IRD_LIMIT)
	    _IRD=ECN_IRD_LIMIT;
	}
      } else {
	if(_IRD<ECN_IRD_LIMIT){
	  //IRD increase ECN on
	  _IRD+=ECN_IRD_INCREASE;
	  if(_IRD>ECN_IRD_LIMIT)
	    _IRD=ECN_IRD_LIMIT;
	}
      }
      gStatIRD[_src]->AddSample(_IRD);
    }
  } else {
    assert(false);
  }
  return effective;
}

//when nack return
//1. packet could have already been sent normaly
//2. nack goes through

//flow buffer status only change when in spec mode
bool FlowBuffer::nack(int sn){
  assert(_mode == RES_MODE);
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
      _reserved_slots++;
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
  assert(_mode == RES_MODE);
  if(_watch){
    cout<<"flow "<<fl->flid
	<<" received grant at time "<<time<<endl;
  }
  assert(_reserved_time == -1);
  _reserved_time = time;
  _res_outstanding=false;
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
      if( FAST_RETRANSMIT_ENABLE && _ready==0){ //search for spec packets
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
      }  else {
	assert(false);
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
      if( FAST_RETRANSMIT_ENABLE && _ready==0){ //search for spec packets
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
	  _reserved_slots++;
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
      _reserved_slots--;
      _guarantee_sent++;
      _last_sn = f->sn;
      _tail_sent = f->tail;
      //enable ECN waiting time
      if(_mode==ECN_MODE && _tail_sent){
	_IRD_wait = 0;
      }
    }
    break;

    //transitions are equivalent to spec at this stage
  case FLOW_STATUS_NACK_TRANSITION:
  case FLOW_STATUS_GRANT_TRANSITION:
  case FLOW_STATUS_SPEC:
    //first send the spec packet
    if(!_spec_sent){
      _reservation_flit->vc=RES_PACKET_VC;
      _reservation_flit->time = GetSimTime();
      f = _reservation_flit;
      f->src = _src;
      f->dest = fl->dest;
      _spec_sent = true;
      _res_outstanding=true;
    } else {
      if(_flit_buffer.count(_last_sn+1)!=0){
	f = Flit::New();
	memcpy(f,_flit_buffer[_last_sn+1], sizeof(Flit));
	_flit_status[_last_sn+1] = FLIT_SPEC;
	f->res_type = RES_TYPE_SPEC;
	f->pri = FLIT_PRI_SPEC;
	_ready--;
	_reserved_slots--;
	_spec_outstanding++;
	_last_sn = f->sn;
	_tail_sent = f->tail;
      } else {
	assert(false);
      }
    }
    break;
  case FLOW_STATUS_NACK:
  default:
    break;
  }

  if(f){
    
    assert(f->res_type == RES_TYPE_RES || _vc!=-1);
    if(_watch){
      cout<<"flow "<<fl->flid
	  <<" sent flit sn "<<f->sn
	  <<" id "<<f->id
	  <<" type "<<f->res_type<<endl;
    } 
  }
    
  return f;
}




//Is the flow buffer eligbible for injection arbitration
bool FlowBuffer::eligible(){
  return _vc!=-1 &&
    (send_norm_ready() ||
     send_spec_ready());
}



bool FlowBuffer::send_norm_ready(){
  switch(_status){
  case FLOW_STATUS_NORM:
    if(!_tail_sent){
      return true;
    } else if(_mode==ECN_MODE){
      //flit not ready unless IRD_wait is 0
      if(_IRD_wait >=_IRD*IRD_SCALING_FACTOR){
	return (_ready>0);
      } else {
	return false;
      }
    } else if(_mode==RES_MODE && fl->flow_size>RESERVATION_PACKET_THRESHOLD){
      if(_reserved_slots>0){
	return (_ready>0);
      } else {
	return false;
      }

    } else {
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
      if(RESERVATION_SPEC_OFF){
	return false;
      }  else {
	return (_ready>0 && _reserved_slots>0);
      }
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
  _flow_queue.pop();
}

