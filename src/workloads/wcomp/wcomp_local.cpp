/*
 * workload component for short-cutting local on-node traffic
 *
 * C. Beckmann (c) Intel, 2023
 */
#include "globals.hpp"
#include "wkld_comp.hpp"
#include <vector>
#include <deque>
#include <string>

//////////////////
// On-node latency - traffic modifier
class LocalShortcut : public WComp<LocalShortcut>
{
    const int _local_latency;   // latency of local requests and replies
    const bool _gen_replies;    // do we need to generate local replies?

    // delay line containing <message,time> pairs
    typedef pair<WorkloadMessagePtr, int> qitem_t;
    deque<qitem_t> _local_inflight;

  public:
    LocalShortcut(int nodes, const vector<string> &options, Configuration const * const config, WorkloadComponent * upstrm)
      : WComp<LocalShortcut>(upstrm),
        _local_latency(stoi(options[0])), // set local latency from parameter
        _gen_replies(0 != config->GetInt("use_read_write")) // if Booksim generates replies, we need to for local requests
    {}
    void Init(int nodes, Configuration const *config) {
        _upstream->Init(nodes, config);
    }
    bool test(int src) {
        int now = GetSimTime();
        // handle locally completing requests
        if (!_local_inflight.empty()) {
            auto & head = _local_inflight.back();
            if (now >= head.second) {
                _upstream->eject(head.first);
                if (_gen_replies && !head.first->IsReply()) // generate local reply if needed
                    _local_inflight.push_front(qitem_t(head.first->Reply(), now + _local_latency));
                _local_inflight.pop_back();
            }
        }
        // check for new requests
        if (!_upstream->test(src)) return false;
        auto m = _upstream->get(src);
        // if it's local, and not a dummy message, insert into delay line
        if (m->Dest() == m->Source() && !m->IsDummy()) {
            _local_inflight.push_front(qitem_t(m, now + _local_latency));
            _upstream->next(src);
            return false;
        }
        // not local, notify downstream
        else return true;
    }
    void next(int src) {
        WorkloadComponent::next(src);
        _upstream->next(src);
    }
    void eject(WorkloadMessagePtr m) {
        _upstream->eject(m);
    }
  private:
    virtual WorkloadMessagePtr _get_new(int src) {
        return _upstream->get(src);
    }
};

PUBLISH_WORKLOAD_COMPONENT(LocalShortcut, "local");
