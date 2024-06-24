/*
 * workload component for Small Message Coalescing  
 *
 * C. Beckman (c) Intel, 2023
 * 
 */

#include "wcomp_smc.hpp"


//////////////////
// small message coalescing - traffic modifier

// Small Message coalescing injection process
PUBLISH_WORKLOAD_COMPONENT(SmallMessageCoalescing, "SMC");
vector<float> SmallMessageCoalescing::_state;

SmallMessageCoalescing::SmallMessageCoalescing(int nodes, const vector<string> &options, Configuration const * config, WorkloadComponent *upstrm)
: WComp<SmallMessageCoalescing>(upstrm),
  GeneratorWorkloadMessage::Factory(config, gInjectorTrafficClass),
  _nodes(nodes),
  _coal_degree(config->GetInt("coalescing_degree")),
  _sm_max_coal_age(config->GetInt("sm_max_coal_age")),
  _utilization_threshold(config->GetFloat("outport_util_threshold")),
  _coal_type(gSm ? CoalType::SW_COAL : CoalType::ENDPT_ONLY),
  _cbufs(nodes),
  _end_cbufs(nodes),
  _dest_used(nodes,-1)
{
  if (!options.empty()) { //configure switch or endpoint only coalescing: component(...,SMC(switch/end),...)
    if (options[0].compare("switch") == 0) { gSm = true; }
    else if (options[0].compare("end") == 0) { gSmEnd = true; }
    else { cout << "Usage: SMC(switch/end)" << endl; exit(-1); }
  } else { //or component(..., SMC,...)
    gSm = true;
  }

  _state.resize(nodes,0.0);
  if (gSm) {
    for (auto & cbn : _cbufs) {
      cbn = new MultiMessage(_coal_degree);
    }
    DBG2("cbuf size %ld node %d ",_cbufs.size(),nodes);
  } else {
    for (auto & cbn : _end_cbufs) {
      cbn.resize(nodes); //per destination
      for (auto & cbnd : cbn)
        cbnd = new MultiMessage(_coal_degree);
    }
    DBG2("cbuf size %ld node %d ",_end_cbufs.size(),nodes);
  }
}

bool SmallMessageCoalescing::_is_cb_full(MultiMessagePtr cb, const int &source)
{
  bool res = false;
  if (cb->IsFull()) { //static coalescing
    res = true;
  } else if (!cb->IsEmpty()) {
    if ((_sm_max_coal_age > 0) && (cb->GetCoalTime(GetSimTime()) >= _sm_max_coal_age)) { //opp-coal: age limit
      cb->SetFull();
      res = true;
    } else if (_get_state(source) < _utilization_threshold) { //opp-coal: port utilization
      cb->SetFull();
      res = true;
    }
  }

  return res;
}

bool SmallMessageCoalescing::test(int source)
{
  bool res = false;
  int dest = -1;
  assert((source >= 0) && (source < _nodes));
  if (_upstream->test(source)) {
    auto m = _upstream->get(source);
    _upstream->next(source);
    dest = m->Dest();
    SmallMessagePtr sm = new SmallMessage(this,m);
    sm->ctime = GetSimTime();
    sm->coal_type = gSm ? CoalType::SW_COAL : CoalType::ENDPT_ONLY; // TBD: maybe add inj coal_type based on config, to merge the two injection process into one.
    auto &cb = gSm ? _cbufs[source] : _end_cbufs[source][dest];
    bool result = cb->AddSM(sm);
    sm->_record_event(pt::SmCoal); // pipetrace
    if (!result) {
      ERR("cbuf[%d] full, can't add msg",source);
    }
  }
  //This applies to endpoint only coalescing because its coal buffers are indexed
  //by source and dest. switch coalescing buffers are indexed by src only.
  if (gSmEnd && (dest == -1)) {
    //_dest_used tracks the destination when endpoint only coalescing buffer was full.
    //Round robin to the next node from the last _dest_used as the starting
    //index to check if the coalescing buffer is full.
    if (_dest_used[source] == -1) dest = 0;
    else dest = (_dest_used[source] + 1)%_nodes;

    for (int c=0,i=dest; c < _nodes; c++, i=(i+1)%_nodes) {
      auto &cb  = _end_cbufs[source][i];
      res = _is_cb_full(cb, source);
      if (res) { _dest_used[source] = i; break; }
    }
    return res;
  }

  auto &cb = gSm ? _cbufs[source] : _end_cbufs[source][dest];
  res = _is_cb_full(cb, source);
  if (gSmEnd && res) { _dest_used[source] = dest; } //cbuf full
  return res;
}

// Pass small message coalescing info to Traffic Manager (TM) when TM is ready
// to generate packet because coalescing buffer is full.
WorkloadMessagePtr SmallMessageCoalescing::_get_new(int source)
{
  int dest = 0;
  if (gSmEnd) {
    dest = _dest_used[source];
    if (dest == -1) {
      ERR("Endpoint coalescing error");
      exit(-1);
    }
  }

  auto &cb = gSm ? _cbufs[source] : _end_cbufs[source][dest];
  DBG2("src %d dest %d num_sm %d size %d\n", source, dest, cb->GetNumSm(), cb->Size());

  auto multi_msg = cb->GetMsg();
  for (auto sm : multi_msg) {
    sm->endpt_cb_time = sm->GetCoalTime(GetSimTime());
  }
  WorkloadMessagePtr pkt = cb;
  cb = new MultiMessage(_coal_degree);
  return pkt;
}

// eject() is the reverse process of test().
// test() creats SM which encapsulates the WorkloadMessage, add SM to the
// coalescing buffer. eject() takes the WorkloadMessage from each SM out
// of the multi-msg.
void SmallMessageCoalescing::eject(WorkloadMessagePtr m)
{
  auto msg = WorkloadMessageContents<MultiMessage>(m);
  assert(msg);
  for (auto sm: msg->GetMsg()) {
    _upstream->eject(sm->Contents());
  }
}
