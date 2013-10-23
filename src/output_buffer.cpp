#include "globals.hpp"
#include "output_buffer.hpp"
#include "reservation.hpp"
#include <limits>

int OutputBuffer::_bubbles = 0;
int OutputBuffer::_control_capacity=-1;
int OutputBuffer::_vcs=-1;
vector<int> OutputBuffer::_buffer_capacity;
vector<bool> OutputBuffer::_spec_vc;

OutputBuffer::OutputBuffer(const Configuration& config,
			   Module *parent, const string& name)
  :Module(parent,name){
  //static variables
  if(_vcs==-1)
    _vcs = config.GetInt("num_vcs");
  if(_buffer_capacity.size()==0){
    _buffer_capacity.resize(_vcs, config.GetInt("output_buffer_size"));
  }
  if( _control_capacity==-1){
    cout<<"Caution, output buffer groups all control vcs together"<<endl;
    _control_capacity =  config.GetInt("output_buffer_control_size");
  }
  if( _spec_vc.size()==0)
    _spec_vc.resize(_vcs,false);
    
 
  _buffers.resize( _vcs);
  _buffer_time.resize( _vcs);

  _last_buffer = 0;

  _control_tail = true;
  _buffer_tail.resize( _vcs, true);

  _buffer_slots.resize(_vcs, 0);

  _watch = 0;

  _nonspec_slots=0;

  _total =0;
}

OutputBuffer::~OutputBuffer(){

}


Flit* OutputBuffer::SendFlit(){
  Flit *f = NULL;
  
  //Check control buffers
  if(!_control_buffer.empty() ){
    if(_watch)
      *gWatchOut<<"Send: Control pop"<<endl;
    f= _control_buffer.front();
    _control_tail = f->tail;
    _control_buffer.pop();
    _total--;
  }
  
  //Check normal buffers
  if(f==NULL){
    int buf_idx = -1;
    //check continuation
    if(!_buffer_tail[_last_buffer] && 
       !_buffers[_last_buffer].empty()){
      buf_idx = _last_buffer;
    } else {//oldest
      int age =numeric_limits<int>::max();
      for(int i = 0; i<_vcs; i++){
	int idx = (i+_last_buffer)%_vcs;	
	if(!_buffers[idx].empty()){
	  if(!_buffer_tail[idx]){
	    //another continuation, this should happen very rarely
	    buf_idx = idx;
	    if(_watch)
	      *gWatchOut<<"Send: secondary continuation\n";
	    break;
	  }
	  //find the oldest
	  if(age>_buffer_time[idx].front()){
	    age = _buffer_time[idx].front();
	    buf_idx = idx;
	  }
	}
      }
    }

    if(buf_idx>=0){
      if(_watch && _buffer_tail[_last_buffer]){
	*gWatchOut<<"Send: VC "<<buf_idx<<" selected Time "<<_buffer_time[buf_idx].front()<<endl;
      }
      _last_buffer = buf_idx;
      f = _buffers[_last_buffer].front();
      _buffers[_last_buffer].pop();
      _total--;
      _buffer_tail[_last_buffer] = f->tail;
      if(f->tail){
	//output buffer slot freed (packets)
	Release(_last_buffer);
	_buffer_time[_last_buffer].pop();
	if(_watch){
	  *gWatchOut<<"Send: VC "<<buf_idx
	      <<" pop next time "<<_buffer_time[_last_buffer].front()<<endl;
	}
	_last_buffer = (_last_buffer+1)%_vcs;
      }
    }    
    if(f){
      if(f->watch){
	_watch--;
      }
    }
  }
  return f;
}

//send a data packet
void OutputBuffer::QueueFlit(int vc, Flit* f){
  assert(f);
  assert(f->vc == vc);
  if(f->watch)
    _watch++;

  //check fragmentation
  if(!f->head && !_buffers[vc].empty()) {
    assert(f->pid == _buffers[vc].back()->pid);
  }
  _buffers[vc].push(f);
  _total++;
  //age based output arbitration
  if(f->head){
    _buffer_time[vc].push(GetSimTime());
  }

  if(f->watch){
    *gWatchOut<<"Queuing VC "<<vc<<" size "<<_buffers[vc].size()
	      <<" time "<<GetSimTime()<<endl;
  }
}

//send a contorl packet to the output
void OutputBuffer::QueueControlFlit(Flit* f){
  assert(f);
  if(f->watch)
    _watch++;
  _control_buffer.push(f);
  _total++;
}


int OutputBuffer::ControlSize(){
  return _control_buffer.size();
}

//output buffer size (flits)
int OutputBuffer::Size(int vc){
  return _buffers[vc].size();
}



//taking a packet slot form the output buffer, done at the end of VC allocation
void OutputBuffer::Take(int vc){
  assert((_buffer_slots[vc]< _buffer_capacity[vc]));
  _buffer_slots[vc]++;
  if(!_spec_vc[vc]){
    _nonspec_slots++;
  }
}
//removea packet slot, done when the packet tail leaves the output buffer
void OutputBuffer::Release(int vc){
  _buffer_slots[vc]--;
  if(!_spec_vc[vc]){
    _nonspec_slots--;
  }
}


//this tracks the number of packets
bool OutputBuffer::Full(int vc){
  assert(_buffer_slots[vc] <=_buffer_capacity[vc]);
  assert(_nonspec_slots>=0);
  //speculative vc is "Full" if there is pending nonspeculative packets
  if(_spec_vc[vc]){
    return (_buffer_slots[vc] >= _buffer_capacity[vc]) || _nonspec_slots>0; //this secondary condition needs to be adjsuted
  } else {
    return (_buffer_slots[vc] >= _buffer_capacity[vc]);
  }
}

//this tracks the number of packets
bool OutputBuffer::ControlFull(){
  return (_control_buffer.size() >= (size_t)_control_capacity);
}


//static function called by iq_router to set the speculative vc
void OutputBuffer::SetSpecVC(int vc, int size){
  assert(gReservation);
  assert(size_t(vc) < _buffer_capacity.size());
  if(!_spec_vc[vc]){
    cout<<"Output buffer:Spec vc "<<vc<<endl;
    _buffer_capacity[vc] = size;
    _spec_vc[vc] = true;
  }
}
