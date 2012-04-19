#include "HierAllocator.hpp"
#include <sstream>
#include "arbiter.hpp"

HierAllocator::HierAllocator(Module* parent, const string& name,
			     int input_leaf,
			     int input_root,
			     int output_leaf,
			     int output_root,
			     const string& input_arb_type,
			     const string& input_root_arb_type,
			     const string& output_arb_type,
			     const string& output_root_arb_type)
  : SparseAllocator( parent, name, input_leaf*input_root, output_leaf*output_root )
{
  _input_size = input_leaf*input_root;
  _input_leaf_size = input_leaf;
  _input_root_size = input_root;
  _output_size = output_leaf*output_root;
  _output_leaf_size = output_leaf;
  _output_root_size = output_root;

  _input_arb.resize(_input_size);

  for (int i = 0; i < _input_size; ++i) {
    ostringstream arb_name("arb_i");
    arb_name << i;
    _input_arb[i] = new HierArbiter(output_leaf,  
				    output_root,  
				    input_arb_type, input_root_arb_type);
    
  }

  _output_arb.resize(_output_size);

  for (int i = 0; i <_output_size; ++i) {
    ostringstream arb_name("arb_o");
    arb_name << i;
    _output_arb[i] = new HierArbiter(input_leaf,
				     input_root, 
				     output_arb_type, output_root_arb_type);
  }
}



void HierAllocator::Allocate() {
  
  set<int>::const_iterator port_iter = _in_occ.begin();
  while(port_iter != _in_occ.end()) {
    
    const int & input = *port_iter;

    // add requests to the input arbiter

    map<int, sRequest>::const_iterator req_iter = _in_req[input].begin();
    while(req_iter != _in_req[input].end()) {

      const sRequest & req = req_iter->second;
      
      _input_arb[input]->AddRequest(req.port, req.label, req.in_pri);

      ++req_iter;
    }

    // Execute the input arbiters and propagate the grants to the
    // output arbiters.

    int label = -1;
    const int output = _input_arb[input]->Arbitrate(&label, NULL);
    assert(output > -1);

    const sRequest & req = _out_req[output][input]; 
    assert((req.port == input) && (req.label == label));

    _output_arb[output]->AddRequest(req.port, req.label, req.out_pri);

    ++port_iter;
  }

  port_iter = _out_occ.begin();
  while(port_iter != _out_occ.end()) {

    const int & output = *port_iter;

    // Execute the output arbiters.
    
    const int input = _output_arb[output]->Arbitrate(NULL, NULL);

    if(input > -1) {
      assert((_inmatch[input] == -1) && (_outmatch[output] == -1));

      _inmatch[input] = output ;
      _outmatch[output] = input ;
      _input_arb[input]->UpdateState() ;
      _output_arb[output]->UpdateState() ;
    }
    ++port_iter;
  }
}



HierAllocator::~HierAllocator() {

  for (int i = 0; i < _input_size; ++i) {
    delete _input_arb[i];
  }

  for (int i = 0; i < _output_size; ++i) {
    delete _output_arb[i];
  }

}

void HierAllocator::Clear() {
  for ( int i = 0 ; i < _input_size ; i++ ) {
    if(_input_arb[i]-> _num_reqs)
      _input_arb[i]->Clear();
  }
  for ( int o = 0; o < _output_size; o++ ) {
    if(_output_arb[o]-> _num_reqs)
      _output_arb[o]->Clear();
  }
  SparseAllocator::Clear();
}
