/*
 * workload component for taking multiple PE per node info into account
 *
 * H. Dogan (c) Intel, 2023
 * 
 */

#include "wkld_comp.hpp"

//////////////////
// multiple PEs per node - traffic modifier
class MppnEndpoint : public WComp<MppnEndpoint>
{
  static int _pe_per_node; // configuration info, global to all objects

  static int _pe_begin(int s) { return s * _pe_per_node; }
  static int _pe_end(int s) { return _pe_begin(s+1); }
  static int _node(int pe) { return pe / _pe_per_node; }

  // encapsulated message with PE info
  class Message : public ModifierWorkloadMessage<Message>
  {
    friend class MppnEndpoint;
    public:
    Message(WorkloadMessagePtr msg) : ModifierWorkloadMessage<Message>(msg) {}
    int Source() const { return _node(_contents->Source()); }
    int Dest()   const { return _node(_contents->Dest()); }
  };

  bool _is_pe_info_list_empty(int src) {
    return _pe_info_list[src].empty();
  }
  void _add_pe_info(int src, int pe) {
    auto pe_info = std::make_pair(pe,new Message(_upstream->get(pe)));
   _pe_info_list[src].push_back(pe_info);
  }
  std::pair<int,Message*> _get_pe_info(int src) {
    assert(!_pe_info_list[src].empty());
    auto pe_info = _pe_info_list[src].front();
    _pe_info_list[src].pop_front();
    assert(_node(pe_info.first) == src);
    return pe_info;
  }

  int _pe_ready;
  std::vector<std::list<std::pair<int,Message*>>> _pe_info_list;
 public:
  MppnEndpoint(int nodes, const vector<string> &options, Configuration const * const config, WorkloadComponent * upstrm)
    : WComp<MppnEndpoint>(upstrm), _pe_ready(-1), _pe_info_list(nodes)
  {
    _pe_per_node = std::stoi(options[0]);
  }
  void Init(int nodes, Configuration const * const config)
  {
    _upstream->Init(nodes*_pe_per_node, config);
  }

  bool test(int src) {

    if(_is_pe_info_list_empty(src)) {
      // Add all PEs from the upstream to the list
      for(int i=0; i<_pe_per_node; ++i) {
        int pe = _pe_begin(src) + i;
        if(_upstream->test(pe)) {
          _add_pe_info(src, pe);
          _upstream->next(pe);
        }
      }
    }
    // If the list is still empty, there are no PEs available.
    if(_is_pe_info_list_empty(src)) {
      _pe_ready = -1;
      return false;
    } else {
      // There are PEs available, so return true.
      return true;
    }
  }

  WorkloadMessagePtr _get_new(int src)
  {
    auto pe_info  = _get_pe_info(src);
    _pe_ready = pe_info.first; 
    return pe_info.second;
  }
  void next(int src)
  {
    WorkloadComponent::next(src);
    assert(_pe_ready >= 0);
    _pe_ready = -1;
  }
  void eject(WorkloadMessagePtr m)
  {
    auto msg = dynamic_cast<Message*>(m.get());
    assert(msg);
    _upstream->eject(msg->_contents);
  }
};


int MppnEndpoint::_pe_per_node = 1;

// instantiate Mppn factory object
PUBLISH_WORKLOAD_COMPONENT(MppnEndpoint, "Mppn");
