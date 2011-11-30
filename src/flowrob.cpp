#include "flowrob.hpp"

FlowROB::FlowROB(int flow_size){

 _status = RES_STATUS_NONE;
  _flid = -1;
  _flow_size = flow_size;
  _max_reorder = 0;
  _flow_creation_time=-1;

  _sn_ceiling=0;
}

FlowROB::~FlowROB(){

}

bool FlowROB::sn_check(int sn){
  return (sn<_sn_ceiling);
}


Flit* FlowROB::insert(Flit* f){

  if(f->res_type != RES_TYPE_RES && f->sn==0){ 
    _flow_creation_time = f->time;
  }
  Flit* rf=NULL;
  switch(_status){
  case RES_STATUS_NONE:
    assert(f->res_type == RES_TYPE_RES);
    _sn_ceiling = f->sn+f->payload;
    _status = RES_STATUS_ASSIGNED;
    _flid = f->flid;
    rf = f;
    
    break;
  case RES_STATUS_REORDER:
  case RES_STATUS_ASSIGNED:
    //second res for this flow
    if(f->res_type == RES_TYPE_RES){
      //this is possible when reservation comes with the last packet
      if(_sn_ceiling>f->sn){
	f->Free();
	rf = NULL;
	break;
      }
      _sn_ceiling = f->sn+f->payload;
      rf = f;
      break;
    }
    //is it a duplicate
    if(_rob.count(f->sn)!=0){
      f->Free();
      rf = NULL;
    } else {
      if(f->head){
	//this is possible when reservation comes with the last packet
	if(f->res_type== RES_TYPE_NORM && f->payload!=-1){
	  if(_sn_ceiling == f->sn+f->payload+32){
	    f->payload=-1;
	  } else {
	    _sn_ceiling = f->sn+f->payload+32;
	  }
	}
	_pid.insert(f->pid);
	if(!_rob.empty()){
	  if(f->sn > (*_rob.rbegin())+1){
	    int reorderness = f->sn - (*_rob.rbegin())+1;
	    _max_reorder = (reorderness>_max_reorder)?reorderness:_max_reorder;
	  }
	} else {
	  int reorderness = f->sn;
	  _max_reorder = (reorderness>_max_reorder)?reorderness:_max_reorder;
	}
	_rob.insert(f->sn);
      }  else { //this ensures header didn't get dropped and then reservation packet sneaks in
	if(_pid.count(f->pid)!=0){
	  if(!_rob.empty()){
	    if(f->sn > (*_rob.rbegin())+1){
	      int reorderness = f->sn - (*_rob.rbegin())+1;
	      _max_reorder = (reorderness>_max_reorder)?reorderness:_max_reorder;
	    }
	  }
	  _rob.insert(f->sn);
	} else {
	  f->Free();
	  rf = NULL;
	  break;
	}
      }
      rf = f;
    }
    break;
  default:
    break;
  }

  return rf;
}

bool FlowROB::done(){
  //reservation has not arrived yet, I don't knwo the flow size
  return ((int)_rob.size() == _flow_size);
}

int FlowROB::range(){
  return _max_reorder;
}
