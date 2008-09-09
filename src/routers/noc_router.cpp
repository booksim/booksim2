#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <assert.h>

#include "random_utils.hpp"
#include "noc_router.hpp"

NoCRouter::NoCRouter(const Configuration & config, Module * parent, string name,
		     int id, int inputs, int outputs) :
  Router(config, parent, name, id, inputs, outputs),
  bufferMonitor(inputs),
  switchMonitor(inputs, outputs)
{
  _num_vcs     = config.GetInt("num_vcs");
  _vc_buf_size = config.GetInt("vc_buf_size");
  _speculative = config.GetInt("speculative");
  
  int rqb_vc = config.GetInt("read_request_begin_vc");
  int rqe_vc = config.GetInt("read_request_end_vc");    
  int rrb_vc = config.GetInt("read_reply_begin_vc");    
  int rre_vc = config.GetInt("read_reply_end_vc");      
  int wqb_vc = config.GetInt("write_request_begin_vc"); 
  int wqe_vc = config.GetInt("write_request_end_vc");   
  int wrb_vc = config.GetInt("write_reply_begin_vc");   
  int wre_vc = config.GetInt("write_reply_end_vc");     
  
  // Routing
  _rf = GetRoutingFunction(config);
  
  // Alloc VC's
  for(int ip = 0; ip < _inputs; ip++) {
    for(int ivc = 0; ivc < _num_vcs; ivc++) {
      VC * vc = new VC;
      vc->_Init(config, _outputs);
      ostringstream vc_name;
      vc_name << "vc_i" << ip << "_v" << ivc;
      vc->SetName(this, vc_name.str());
      _input_vcs.push_back(vc);
    }
  }
  
  // Alloc next VCs' buffer state
  for(int op = 0; op < _outputs; op++) {
    BufferState * output_state = new BufferState;
    output_state->_Init(config);
    ostringstream vc_name;
    vc_name << "next_vc_o" << op;
    output_state->SetName(this, vc_name.str());
    _output_states.push_back(output_state);
  }
  
  string alloc_type;
  
  // Alloc allocators
  config.GetStr("vc_allocator", alloc_type);
  _vc_allocator = Allocator::NewAllocator(config, this, "vc_allocator",
					  alloc_type, _num_vcs * _inputs, 1,
					  _num_vcs * _outputs, 1);
  
  if(!_vc_allocator) {
    cout << "ERROR: Unknown vc_allocator type " << alloc_type << endl;
    exit(-1);
  }
  
  config.GetStr("sw_allocator", alloc_type);
  _sw_allocator = Allocator::NewAllocator(config, this, "sw_allocator",
					  alloc_type, _inputs, 1, _outputs, 1);
  
  if(!_sw_allocator) {
    cout << "ERROR: Unknown sw_allocator type " << alloc_type << endl;
    exit(-1);
  }
  
  // dub: do we need this?
  _sw_rr_offset = new int [_inputs];
  for(int i = 0; i < _inputs; ++i) {
    _sw_rr_offset[i] = 0;
  }
  
  // Alloc pipelines (to simulate processing/transmission delays)
  _crossbar_pipe = 
    new PipelineFIFO<Flit>(this, "crossbar_pipeline", _outputs,
 			   _st_prepare_delay + _st_final_delay);
  
  _credit_pipe =
    new PipelineFIFO<Credit>(this, "credit_pipeline", _inputs, _credit_delay);
  
  // Input and output queues
  for(int ip = 0; ip < _inputs; ip++) {
    _input_buffer.push_back(new queue<Flit *>);
    _in_cred_buffer.push_back(new queue<Credit *>);
  }
  
  for(int op = 0; op < _outputs; op++) {
    _output_buffer.push_back(new queue<Flit *>);
    _out_cred_buffer.push_back(new queue<Credit *>);
  }
  
}

NoCRouter::~NoCRouter()
{
  if(_print_activity){
    cout << _name << ".bufferMonitor:" << endl; 
    cout << bufferMonitor << endl;
    
    cout << _name << ".switchMonitor:" << endl; 
    cout << "Inputs=" << _inputs;
    cout << "Outputs=" << _outputs;
    cout << switchMonitor << endl;
  }
  
  for(int ip = 0; ip < _inputs; ip++) {
    for(int ivc = 0; ivc < _num_vcs; ivc++) {
      delete _input_vcs[ip * _num_vcs + ivc];
    }
  }
  
  for(int op = 0; op < _outputs; op++) {
    delete _output_states[op];
  }
  
  delete _vc_allocator;
  delete _sw_allocator;
  
  delete [] _sw_rr_offset;
  
  delete _crossbar_pipe;
  delete _credit_pipe;
  
  for(int ip = 0; ip < _inputs; ip++) {
    delete _input_buffer[ip];
    delete _in_cred_buffer[ip];
  }
  
  for(int op = 0; op < _outputs; op++) {
    delete _output_buffer[op];
    delete _out_cred_buffer[op];
  }
}
  
void NoCRouter::ReadInputs()
{
  _ReceiveFlits();
  _ReceiveCredits();
}

void NoCRouter::InternalStep()
{
  _InputQueuing();
  _Route();
  _VCAlloc();
  _SWAlloc();

  for(int ip = 0; ip < _inputs; ip++) {
    for(int ivc = 0; ivc < _num_vcs; ivc++) {
      _input_vcs[ip * _num_vcs + ivc]->AdvanceTime();
    }
  }
  
  _crossbar_pipe->Advance();
  _credit_pipe->Advance();
  
  _OutputQueuing();
}

void NoCRouter::WriteOutputs()
{
  _SendFlits();
  _SendCredits();
  if(_trace) {
    int load = 0;
    cout << "Router " << this->GetID() << endl;
    //need to modify router to report the buffere dept
    //cout<<"Input Channel "<<in_channel<<endl;
    //load +=r->GetBuffer(in_channel);
    cout << "Rload " << load << endl; // dub: FIXME!?!
  }
}

void NoCRouter::_ReceiveFlits()
{
  bufferMonitor.cycle();
  
  for(int ip = 0; ip < _inputs; ip++) { 
    Flit * f = (*_input_channels)[ip]->ReceiveFlit();
    
    if(f) {
      _input_buffer[ip]->push(f);
      bufferMonitor.write(ip, f);
    }
  }
}

void NoCRouter::_ReceiveCredits()
{
  for(int op = 0; op < _outputs; op++) {  
    Credit * c = (*_output_credits)[op]->ReceiveCredit();
    
    if(c) {
      _out_cred_buffer[op]->push(c);
    }
  }
}

void NoCRouter::_InputQueuing()
{
  for(int ip = 0; ip < _inputs; ip++) {
    if(!_input_buffer[ip]->empty()) {
      Flit * f = _input_buffer[ip]->front();
      _input_buffer[ip]->pop();
      
      VC * cur_vc = _input_vcs[ip * _num_vcs + f->vc];
      
      if(!cur_vc->AddFlit(f)) {
	Error("VC buffer overflow");
      }
      
      if(f->watch) {
	cout << "Received flit at " << _fullname << endl;
	cout << *f;
      }
    }
  }
  
  for(int ip = 0; ip < _inputs; ip++) {
    for(int ivc = 0; ivc < _num_vcs; ivc++) {
      
      VC * cur_vc = _input_vcs[ip * _num_vcs + ivc];
      
      if(cur_vc->GetState() == VC::idle) {
	Flit * f = cur_vc->FrontFlit();
	
	if(f) {
	  if(!f->head) {
	    Error("Received non-head flit at idle VC");
	  }
	  
	  cur_vc->Route(_rf, this, f, ip);
	  cur_vc->SetState(VC::routing);
	}
      }
    }
  }  
  
  for(int op = 0; op < _outputs; op++) {
    if(!_out_cred_buffer[op]->empty()) {
      Credit * c = _out_cred_buffer[op]->front();
      _out_cred_buffer[op]->pop();
      
      _output_states[op]->ProcessCredit(c);
      delete c;
    }
  }
}

void NoCRouter::_Route()
{
  for(int ip = 0; ip < _inputs; ip++) {
    for(int ivc = 0; ivc < _num_vcs; ivc++) {
      
      VC * cur_vc = _input_vcs[ip * _num_vcs + ivc];
      
      if((cur_vc->GetState() == VC::routing) &&
	 (cur_vc->GetStateTime() >= _routing_delay)) {
	
	if(_speculative)
	  cur_vc->SetState(VC::vc_spec);
	else
	  cur_vc->SetState(VC::vc_alloc);
	
      }
    }
  }
}

void NoCRouter::_AddVCRequests(VC* cur_vc, int input_index, bool watch)
{
  const OutputSet * route_set = cur_vc->GetRouteSet();
  int out_priority = cur_vc->GetPriority();

  for(int op = 0; op < _outputs; op++) {
    int vc_cnt = route_set->NumVCs(op);
    BufferState * output_state = _output_states[op];

    for(int vc_index = 0; vc_index < vc_cnt; vc_index++) {
      int in_priority;
      int ovc = route_set->GetVC(op, vc_index, &in_priority);

      if(watch) {
	cout << "  trying vc " << ovc << " (out = " << op << ") ... ";
      }
     
      // On the input input side, a VC might request several output 
      // VCs.  These VCs can be prioritized by the routing function
      // and this is reflected in "in_priority".  On the output,
      // if multiple VCs are requesting the same output VC, the priority
      // of VCs is based on the actual packet priorities, which is
      // reflected in "out_priority".

      if(output_state->IsAvailableFor(ovc)) {
	_vc_allocator->AddRequest(input_index, op * _num_vcs + ovc, 1, 
				  in_priority, out_priority);
	if(watch) {
	  cout << "available" << endl;
	}
      } else if(watch) {
	cout << "busy" << endl;
      }
    }
  }
}

void NoCRouter::_VCAlloc()
{
  bool watched = false;

  _vc_allocator->Clear();

  for(int ip = 0; ip < _inputs; ip++) {
    for(int ivc = 0; ivc < _num_vcs; ivc++) {

      VC * cur_vc = _input_vcs[ip * _num_vcs + ivc];

      if(((cur_vc->GetState() == VC::vc_alloc) ||
	  (cur_vc->GetState() == VC::vc_spec)) &&
	 (cur_vc->GetStateTime() >= _vc_alloc_delay)) {

	Flit * f = cur_vc->FrontFlit();
	assert(f); // dub: add null check

	if(f->watch) {
	  cout << "VC requesting allocation at " << _fullname << endl;
	  cout << "  input: " << ip 
	       << "  vc: " << ivc << endl;
	  cout << *f;
	  watched = true;
	}

	_AddVCRequests(cur_vc, ip * _num_vcs + ivc, f->watch);
      }
    
    }
    
  }

  if(watched) {
    _vc_allocator->PrintRequests();
  }

  _vc_allocator->Allocate();

  // Winning flits get a VC

  for(int op = 0; op < _outputs; op++) {
    for(int ovc = 0; ovc < _num_vcs; ovc++) {

      BufferState * output_state;

      int input_and_vc = _vc_allocator->InputAssigned(op * _num_vcs + ovc);

      if(input_and_vc != -1) {
	VC * cur_vc  = _input_vcs[input_and_vc];
	BufferState * output_state = _output_states[op];

	if(_speculative)
	  cur_vc->SetState(VC::vc_spec_grant);
	else
	  cur_vc->SetState(VC::active);

	cur_vc->SetOutput(op, ovc);
	output_state->TakeBuffer(ovc);

	Flit * f = cur_vc->FrontFlit();
	assert(f); // dub: add null check
	
	if(f->watch) {
	  cout << "Granted VC allocation at " << _fullname 
	       << " (input index " << input_and_vc << ")" << endl;
	  cout << *f;
	}
      }
    }
  }
}

void NoCRouter::_SWAlloc()
{
  _sw_allocator->Clear();

  for(int ip = 0; ip < _inputs; ip++) {
    
    // Arbitrate (round-robin) between multiple 
    // requesting VCs at the same input (handles 
    // the case when multiple VC's are requesting
    // the same output port)

    
    
    for(int ivc_base = 0; ivc_base < _num_vcs; ivc_base++) {
      
      int ivc = (ivc_base + _sw_rr_offset[ip]) % _num_vcs;

      VC * cur_vc = _input_vcs[ip * _num_vcs + ivc];
      
      //
      // Non-speculative requests are sent to the allocator with a priority 
      // assignment of 1, distinguishing these requests from speculative
      // switch requests. The allocator is responsible for correctly
      // handling the two classes of requests
      //
      if((cur_vc->GetState() == VC::active) && !cur_vc->Empty()) {
	
	BufferState * output_state = _output_states[cur_vc->GetOutputPort()];
	
	if(!output_state->IsFullFor(cur_vc->GetOutputVC())) {
	  
	  int op = cur_vc->GetOutputPort();
	  
	  // We could have requested this same input-output pair in a previous
	  // iteration, only replace the previous request if the current
	  // request has a higher priority (this is default behavior of the
	  // allocators).  Switch allocation priorities are strictly 
	  // determined by the packet priorities.
	  
	  if(_speculative){
	    _sw_allocator->AddRequest(ip, op, ivc, 
				      1 /*cur_vc->GetPriority() */, 
				      1 /*cur_vc->GetPriority() */);
	  } else {
	    //make sure priority is other wise correct
	    _sw_allocator->AddRequest(ip, op, ivc, 
				      cur_vc->GetPriority(), 
				      cur_vc->GetPriority());
	  }
	}
      }
      
      //
      // The following models the speculative VC allocation aspects 
      // of the pipeline. An input VC with a request in for an egress
      // virtual channel will also speculatively bid for the switch
      // regardless of whether the VC allocation succeeds. These
      // speculative requests are marked as such so as to prevent them
      // from interfering with non-speculative bids
      //
      bool enter_spec_sw_req = !cur_vc->Empty() &&
	((cur_vc->GetState() == VC::vc_spec) ||
	 (cur_vc->GetState() == VC::vc_spec_grant));
      
      if(enter_spec_sw_req) {
	
	int op = cur_vc->GetOutputPort();
	
	// Speculative requests are sent to the allocator with a priority
	// of 0 regardless of whether there is buffer space available
	// at the downstream router because request is speculative. 
	_sw_allocator->AddRequest(ip, op, ivc, 
				  0, 
				  0);
	
      }
    }
  }
  
  _sw_allocator->Allocate();

  // Promote virtual channel grants marked as speculative to active
  // now that the speculative switch request has been processed. Those
  // not marked active will not release flits speculatiely sent to the
  // switch to reflect the failure to secure buffering at the downstream
  // router
  for(int ip = 0; ip < _inputs; ip++) {
    for(int ivc = 0; ivc < _num_vcs; ivc++) {
      VC * cur_vc = _input_vcs[ip * _num_vcs + ivc];
      if(cur_vc->GetState() == VC::vc_spec_grant) {
	cur_vc->SetState(VC::active);	
      } 
    }
  }

  // Winning flits cross the switch

  _crossbar_pipe->WriteAll(NULL);

  //////////////////////////////
  // Switch Power Modelling
  //  - Record Total Cycles
  //
  switchMonitor.cycle();
  
  for(int ip = 0; ip < _inputs; ip++) {
    Credit * c = NULL;
    
    int op = _sw_allocator->OutputAssigned(ip);
    
    if(op >= 0) {
      
      int ivc = _sw_allocator->ReadRequest(ip, op);
      VC * cur_vc = _input_vcs[ip * _num_vcs + ivc];
      
      // Detect speculative switch requests which succeeded when VC 
      // allocation failed and prevenet the switch from forwarding
      if(cur_vc->GetState() == VC::active) {
	
	assert((cur_vc->GetState() == VC::active) && 
	       (!cur_vc->Empty()) && 
	       (cur_vc->GetOutputPort() == op));
	
	BufferState * output_state = _output_states[cur_vc->GetOutputPort()];
	
	if(output_state->IsFullFor(cur_vc->GetOutputVC()))
	  continue;
	
	// dub: redundant?
	assert(!output_state->IsFullFor(cur_vc->GetOutputVC()));
	
	// Forward flit to crossbar and send credit back
	Flit * f = cur_vc->RemoveFlit();
	
	f->hops++;
	
	//
	// Switch Power Modelling
	//
	switchMonitor.traversal(ip, op, f);
	bufferMonitor.read(ip, f);
	
	if(f->watch) {
	  cout << "Forwarding flit through crossbar at " << _fullname << ":"
	       << endl;
	  cout << *f;
	  cout << "  input: " << ip 
	       << "  output: " << op << endl;
	}
	
	if(!c) {
	  c = _NewCredit(_num_vcs);
	}
	
	c->vc[c->vc_cnt] = f->vc;
	c->vc_cnt++;
	
	f->vc = cur_vc->GetOutputVC();
	output_state->SendingFlit(f);
	
	_crossbar_pipe->Write(f, op);
	
	if(f->tail) {
	  cur_vc->SetState(VC::idle);
	}
	
	_sw_rr_offset[ip] = (f->vc + 1) % _num_vcs;
      }
    }
    _credit_pipe->Write(c, ip);
  }
}

void NoCRouter::_OutputQueuing()
{
  for(int op = 0; op < _outputs; op++) {
    Flit * f = _crossbar_pipe->Read(op);
    
    if(f) {
      _output_buffer[op]->push(f);
    }
  }  
  
  for(int ip = 0; ip < _inputs; ip++) {
    Credit * c = _credit_pipe->Read(ip);
    
    if(c) {
      _in_cred_buffer[ip]->push(c);
    }
  }
}

void NoCRouter::_SendFlits()
{
  for(int op = 0; op < _outputs; op++) {
    Flit * f = NULL;
    if(!_output_buffer[op]->empty()) {
      f = _output_buffer[op]->front();
      _output_buffer[op]->pop();
    }
    if(_trace) {
      cout << "Outport " << op << endl;
      cout << "Stop Mark" << endl;
    }
    (*_output_channels)[op]->SendFlit(f);
  }
}

void NoCRouter::_SendCredits()
{
  for(int ip = 0; ip < _inputs; ip++) {
    Credit * c = NULL;
    if(!_in_cred_buffer[ip]->empty()) {
      c = _in_cred_buffer[ip]->front();
      _in_cred_buffer[ip]->pop();
    }
    (*_input_credits)[ip]->SendCredit(c);
  }
}

void NoCRouter::Display() const
{
  for(int ip = 0; ip < _inputs; ip++) {
    for(int ivc = 0; ivc < _num_vcs; ivc++) {
      _input_vcs[ip * _num_vcs + ivc]->Display();
    }
  }
}

int NoCRouter::GetCredit(int op, int ovc_begin, int ovc_end) const
{
  if(op >= _outputs) {
    cout << " ERROR  - big output  GetCredit : " << op << endl;
    exit(-1);
  }
  
  BufferState * output_state = _output_states[op];
  
  int tmpsum = 0;
  if(ovc_begin == -1) {
    for(int ovc = 0; ovc < _num_vcs; ovc++){
      tmpsum += output_state->Size(ovc);
    }
    return tmpsum;
  } else {
    for(int ovc = ovc_begin; ovc <= ovc_end; ovc++)  {
      tmpsum += output_state->Size(ovc);
    }
    return tmpsum;
  }
}

int NoCRouter::GetBuffer(int ip) const {
  int size = 0;
  for(int ivc = 0; ivc < _num_vcs; ivc++){
    VC * cur_vc = _input_vcs[ip * _num_vcs + ivc];
    size += cur_vc->GetSize();
  }
  return size;
}


// ----------------------------------------------------------------------
//
//   Switch Monitor
//
// ----------------------------------------------------------------------
NoCRouter::SwitchMonitor::SwitchMonitor(int inputs, int outputs) {
  // something is stomping on the arrays, so padding is applied
  const int Offset = 16;
  _cycles  = 0;
  _inputs  = inputs;
  _outputs = outputs;
  const int n = 2 * Offset + (inputs+1) * (outputs+1) * Flit::NUM_FLIT_TYPES;
  _event = new int [n];
  for(int i = 0; i < n; i++) {
    _event[i] = 0;
  }
  _event += Offset;
}

int NoCRouter::SwitchMonitor::index(int input, int output, int flitType) const {
  return flitType + Flit::NUM_FLIT_TYPES * (output + _outputs * input);
}

void NoCRouter::SwitchMonitor::cycle() {
  _cycles++;
}

void NoCRouter::SwitchMonitor::traversal(int input, int output, Flit* flit) {
  _event[index(input, output, flit->type)]++;
}

ostream& operator<<(ostream& os, const NoCRouter::SwitchMonitor& obj) {
  for(int i = 0; i < obj._inputs; i++) {
    for(int o = 0; o < obj._outputs; o++) {
      os << "[" << i << " -> " << o << "] ";
      for(int f = 0; f < Flit::NUM_FLIT_TYPES; f++) {
	os << f << ":" << obj._event[obj.index(i,o,f)] << " ";
      }
      os << endl;
    }
  }
  return os;
}

// ----------------------------------------------------------------------
//
//   Flit Buffer Monitor
//
// ----------------------------------------------------------------------
NoCRouter::BufferMonitor::BufferMonitor(int inputs) {
  // something is stomping on the arrays, so padding is applied
  const int Offset = 16;
  _cycles = 0;
  _inputs = inputs;

  const int n = 2*Offset + 4 * inputs  * Flit::NUM_FLIT_TYPES;
  _reads  = new int [n];
  _writes = new int [n];
  for(int i = 0; i < n; i++) {
    _reads[i]  = 0; 
    _writes[i] = 0;
  }
  _reads += Offset;
  _writes += Offset;
}

int NoCRouter::BufferMonitor::index(int input, int flitType) const {
  if(input < 0 || input > _inputs) 
    cerr << "ERROR: input out of range in BufferMonitor" << endl;
  if(flitType < 0 || flitType> Flit::NUM_FLIT_TYPES) 
    cerr << "ERROR: flitType out of range in flitType" << endl;
  return flitType + Flit::NUM_FLIT_TYPES * input;
}

void NoCRouter::BufferMonitor::cycle() {
  _cycles++;
}

void NoCRouter::BufferMonitor::write(int input, Flit* flit) {
  _writes[index(input, flit->type)]++;
}

void NoCRouter::BufferMonitor::read(int input, Flit* flit) {
  _reads[index(input, flit->type)]++;
}

ostream& operator<<(ostream& os, const NoCRouter::BufferMonitor& obj) {
  for(int i = 0; i < obj._inputs; i++) {
    os << "[ " << i << " ] ";
    for(int f = 0; f < Flit::NUM_FLIT_TYPES; f++) {
      os << "Type=" << f
	 << ":(R#" << obj._reads[obj.index(i, f)]  << ","
	 << "W#" << obj._writes[obj.index(i, f)] << ")" << " ";
    }
    os << endl;
  }
  return os;
}

