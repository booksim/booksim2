#include "HierArbiter.hpp"
#include <assert.h>

HierArbiter::HierArbiter(int leaf, int root,
			 const string& _leaf_arb_type,
			 const string& _root_arb_type){
  
  _num_reqs = 0;
  _leaf_size = leaf;
  _root_size = root;

  _port = -1; 
  _vc = -1;
  _leaf_arb.resize(root);
  for(int i = 0; i<root; i++){
    _leaf_arb[i] = Arbiter::NewArbiter(NULL, 
				       "leaf arb",
				       _leaf_arb_type, leaf);
  }
  _root_arb = Arbiter::NewArbiter(NULL, 
				  "root arb",
				  _root_arb_type, root);
  
}
void HierArbiter::AddRequest(int input, int id, int pri){
  _num_reqs++ ;
  int req_port = input/_leaf_size;
  int req_vc = input%_leaf_size;
  //The leaf stage ignores priority
  _leaf_arb[req_port]->AddRequest(req_vc, id, 1);
  _root_arb->AddRequest(req_port, id, pri);
}

void HierArbiter::UpdateState(){
  assert(_port>=0);
  assert(_vc >= 0);
  _root_arb->UpdateState();
  _leaf_arb[_port]->UpdateState();
}

int HierArbiter::Arbitrate( int* id , int* pri ){
  //
  _port= _root_arb->Arbitrate(NULL, pri);
  if(_port>=0){
    assert(_port<(int)_leaf_arb.size());
    _vc =  _leaf_arb[_port]->Arbitrate(id, NULL);
    assert(_vc>=0);
    return _port*_leaf_size+_vc;
  } else {
    return -1;
  }
}
void HierArbiter::Clear(){

  for(size_t i = 0; i<_leaf_arb.size(); i++){
    if(_leaf_arb[i]-> _num_reqs)
      _leaf_arb[i]->Clear();
  }
  if(_root_arb-> _num_reqs)
    _root_arb->Clear();

  _num_reqs = 0 ;  
  _port = -1;
  _vc = -1;
}

HierArbiter::~HierArbiter(){
  for(size_t i = 0; i<_leaf_arb.size(); i++){
    delete _leaf_arb[i];
  }
  _leaf_arb.clear();
  delete _root_arb;
}
