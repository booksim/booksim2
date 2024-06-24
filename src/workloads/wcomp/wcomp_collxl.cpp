/*
 * workload component for modeling a hardware collectives accelerator
 *
 * C. Beckmann (c) Intel, 2023
 *
 */

#include "globals.hpp"
#include "wcomp_collxl.hpp"

PUBLISH_WORKLOAD_COMPONENT(CollectiveAccel, "collxl");

// collective remote sync algorithms, indexed by name
const map<string, CollectiveAccel::AccelNode::collx_fptr_t>
    CollectiveAccel::_str2barr({
        {"linear",       &CollectiveAccel::AccelNode::_remote_barrier_linear},
        {"tree",         &CollectiveAccel::AccelNode::_remote_barrier_tree},
        {"all2all",      &CollectiveAccel::AccelNode::_remote_barrier_all2all},
        {"dissem",       &CollectiveAccel::AccelNode::_remote_barrier_dissem},
        {"butterfly",    &CollectiveAccel::AccelNode::_remote_barrier_btfly}  }),
    CollectiveAccel::_str2red({
        {"ring",         &CollectiveAccel::AccelNode::_remote_allreduce_ring},
        {"tree",         &CollectiveAccel::AccelNode::_remote_allreduce_tree},
        {"recdbl",       &CollectiveAccel::AccelNode::_remote_allreduce_recdbl},
        {"rabenseifner", &CollectiveAccel::AccelNode::_remote_allreduce_rabenseifner}  }),
    CollectiveAccel::_str2bcast({
        {"linear",       &CollectiveAccel::AccelNode::_remote_bcast_linear},
        {"tree",         &CollectiveAccel::AccelNode::_remote_bcast_tree}  });

// get method pointer from options string
CollectiveAccel::AccelNode::collx_fptr_t CollectiveAccel::_opt2f(const vector<string>& opts, size_t i, const string &def, const map<string, AccelNode::collx_fptr_t>& s2f)
{
    const string &s(opts.size() > i ? opts.at(i) : def);
    try { return s2f.at(s); }
    catch(std::out_of_range&) { throw std::logic_error("collxl: bad algorithm: " + s); }
}

CollectiveAccel::CollectiveAccel(int n, const vector<string> &opts, Configuration const * const cfg, WorkloadComponent * up)
  : WComp<CollectiveAccel>(up),
    GeneratorWorkloadMessage::Factory(cfg, gInjectorTrafficClass),
    _remote_barrier(  _opt2f(opts, 0, "linear", _str2barr )),
    _remote_allreduce(_opt2f(opts, 1, "ring",   _str2red  )),
    _remote_bcast(    _opt2f(opts, 2, "linear", _str2bcast)),
    _barrier_radix(cfg->GetInt("k")), _barrier_root(0), _nppn(0), _compute_lat(10),
    _gen_replies(0 == GetIntIndexed(cfg, "use_read_write", gInjectorTrafficClass))
{
    if (opts.size() > 3)
        throw std::logic_error("usage: collxl(<barrier-alg>,<reduce-alg>,<broadcast-alg>)");
    for (int i = 0; i < n; i++)
        _nodes.emplace_back(*this, i);

    _replies_pending.resize(n);
}

void CollectiveAccel::Init(int n, Configuration const * const cfg)
{
    _upstream->Init(n, cfg); // initialize upstream component
    for (auto& node : _nodes) // start per-node coroutines
        node.init();
}

bool CollectiveAccel::test(int src)
{
    if (!_replies_pending[src].empty()) {
      return true;
    }

    auto& n(_nodes[src]);
    if (n.has_msg()) return true; // any outgoing sync messages?

    _handle_reply(n);

    bool up = _upstream->test(src);
    WorkloadMessagePtr m; Request* rm;
    if (up && (m = _upstream->get(src), rm = _req_msg(m))) { // new sync request
        _upstream->next(src);
        _nppn = rm->num_pes / _nodes.size(); // FIX! get this at init?
        return n.reqin(m); // steps state machine
    }
    else { // ordinary fabric request
        n.next();
        return up;
    }
}

WorkloadMessagePtr CollectiveAccel::_get_new(int src)
{
    if (!_replies_pending[src].empty())
       return _replies_pending[src].front();
    auto& n(_nodes[src]);
    return n.has_msg() ? n.get_msg() : _upstream->get(src);
}

void CollectiveAccel::next(int src)
{
    WorkloadComponent::next(src);

    if(!_replies_pending[src].empty())
      _replies_pending[src].pop_front();
    else {
      auto& n(_nodes[src]);
      if (n.has_msg()) n.next_msg();
      else _upstream->next(src);
    }
}

void CollectiveAccel::eject(WorkloadMessagePtr m)
{
    if (_accel_msg(m)) {
        auto& n(_nodes[m->Dest()]);
        _handle_reply(n);
        // do not send fabric replies to state machine, but always step it
        if (m->IsReply()) n.reply(m); //n.next();
        else {
          n.sendin(m);
          if(_gen_replies) {
            _replies_pending[m->Dest()].push_back(m->Reply());
          }
        }
    }
    else _upstream->eject(m);
}


// print accelerator request message
std::ostream & CollectiveAccel::Request::Print(std::ostream & os, bool deep) const
{
    static const char *type2str[] = {"NO_OP", "BARRIER", "ALLREDUCE", "BROADCAST", "PREFIX"};
    return IsReply()
        ?   (os << "CollAccel->" << Dest() << ' ' << type2str[operation] << ' ' << num_pes << " (reply) [" << Size() << "]")
        :   (os << Source() << "->CollAccel "     << type2str[operation] << ' ' << num_pes <<         " [" << Size() << "]");
}

// print accelerator fabric message
std::ostream & CollectiveAccel::AccelMessage::Print(std::ostream & os, bool deep) const
{
    return IsReply()
        ?   os << Source() << "->" << Dest() << "(CollAccel) SEND" << " (reply) [" << Size() << "]"
        :   os << Source() << "(CollAccel)->" << Dest() << " SEND" <<         " [" << Size() << "]";
}


// collective routines, indexed by collective op type
const map<CollectiveAccel::collx_op_t, CollectiveAccel::AccelNode::collx_fptr_t> CollectiveAccel::AccelNode::_op_f({
    {CX_BARRIER,   &CollectiveAccel::AccelNode::_barrier},
    {CX_ALLREDUCE, &CollectiveAccel::AccelNode::_allreduce},
    {CX_BCAST,     &CollectiveAccel::AccelNode::_bcast}  });

// start co-routine
void CollectiveAccel::AccelNode::init()
{
    _xl_time = 0;
    _coro = new coro_t(std::bind(&CollectiveAccel::AccelNode::_coro_f, this, std::placeholders::_1));
}

// handle a request message from the local node, return true if you have an outgoing fabric message
bool CollectiveAccel::AccelNode::reqin(WorkloadMessagePtr m)
{
    _inflight.push_back(m);
    next();
    return has_msg();
}


// handle incoming fabric messages
void CollectiveAccel::AccelNode::sendin(WorkloadMessagePtr m)
{
  if(m->Type() == WorkloadMessage::SendRequest)
    _net_inq.push_back(m);
  // else ... need to take care of puts here maybe add some latency for memory accesses

  next();
}

void CollectiveAccel::AccelNode::reply(WorkloadMessagePtr m)
{
  for (auto i = _net_replyq.begin(); i != _net_replyq.end(); ++i) {
    if ((*i)->Dest() == m->Source()) {
      _net_replyq.erase(i);
      break;
    }
  }
  next();
}



void CollectiveAccel::AccelNode::_send_to(int dst, msg_t typ, int dsize)
{
   auto msg = new AccelMessage(&_xl, _me, dst, typ, dsize);
   _net_outq.push_back(msg);

   // this keeps track of the outstanding replies
   _net_replyq.push_back(msg);
}

void CollectiveAccel::AccelNode::_recv(sink_t& sink, int src) {

  bool ret = false;
  while(!ret) {
      for(auto msg = _net_inq.begin(); msg != _net_inq.end(); ++msg) {
         if ((*msg)->Source() == src) {
            _net_inq.erase(msg);
            ret = true;
            break;
         }
      }
      sink(0);
  }
}

void CollectiveAccel::AccelNode::_recv_multiple(sink_t& sink, int nodes) {

  while((int)_net_inq.size() < nodes)
    sink(0);
  _net_inq.clear();

}

void CollectiveAccel::AccelNode::_recv_and_reduce(sink_t& sink,
                        int num_rdcs, int count, int type_size)
{
  for(int i=0; i<num_rdcs; ++i) {
      while(_net_inq.empty())
         sink(0);
      _net_inq.pop_front();
      _local_reduction(sink, 1, count, type_size);
  }
}


void CollectiveAccel::AccelNode::_recv_replies(sink_t& sink)
{
   // wait for all the replies
   while(!_net_replyq.empty())
     sink(0);
   _net_replyq.clear();
}

void CollectiveAccel::AccelNode::_wait_local_pes(sink_t& sink)
{
  while (_inflight.size() < _xl._nppn)
    sink(0);
}

void CollectiveAccel::AccelNode::_notify_local_pes(sink_t& sink)
{
  // notify local PEs
  for (size_t p = 0; p < _xl._nppn; ++p) {
    auto r = _inflight.front()->Reply();
    _inflight.pop_front();
    sink(r);
  }
}


void CollectiveAccel::AccelNode::_local_reduction(sink_t& sink, int num_rdcs, int count, int type_size) {

   int latency = 0;
   for(int i=0; i<num_rdcs; ++i) {
     int cacheline = 64;
     int stride = cacheline / type_size;
     for(int i=0; i<count; i+=stride) {
       latency += _xl._compute_lat;
     }
   }
   _xl_time = GetSimTime() + latency;
}

// synchronization state machine.  Yields a local reply (or NULL).
// Outgoing fabric messages are placed in _net_outq as a side effect.
void CollectiveAccel::AccelNode::_coro_f(sink_t& sink)
{
    for (;;) {
        // wait for an input request
        while (_inflight.empty()) sink(0);
        assert(_xl._nppn > 0); // ensure initialized
        _cur_op = _req_msg(_inflight.front())->operation;

        // call the appropriate handler
        (this->*(_op_f.at(_cur_op)))(sink);
    }
}

//---------------------------- barrier syncs ----------------------------//

// perform one barrier synchronization
void CollectiveAccel::AccelNode::_barrier(sink_t& sink)
{
  if(_me == _xl._barrier_root)
    // wait for a request from each local PE
    _wait_local_pes(sink);
    // FIX? sanity check the requests to make sure they match?

  // invoke the configured algorithm
  (this->*(_xl._remote_barrier))(sink);

  // there might be outstanding replies
  // need to wait for their completion
  _recv_replies(sink);


  // notify local PEs
  _notify_local_pes(sink);
}

// perform the remote part of a barrier sync, using a linear alorithm
void CollectiveAccel::AccelNode::_remote_barrier_linear(sink_t& sink)
{

  const int nnodes = _xl._nodes.size();
  const int barrier_root = _xl._barrier_root;

  if (_me == barrier_root) { // root node
    // wait for children:
    _recv_multiple(sink, nnodes-1);

    // barrier done! notify children:
    for (int i=0; i<nnodes; ++i)
      if(i != _xl._barrier_root)
        _send_to(i);
  }
  else { // non-root node

    // notify root
    _send_to(_xl._barrier_root);

    // wait for notification from root
    _recv(sink, _xl._barrier_root);
  }
}

// perform the remote part of a barrier sync, using an all-to-all alorithm
void CollectiveAccel::AccelNode::_remote_barrier_all2all(sink_t& sink)
{
    const int nnodes = _xl._nodes.size();

    // send to each other node
    for (int rn = 0; rn < nnodes; ++rn)
        if (rn != _me)
           _send_to(rn);

    // receive from each other node
    _recv_multiple(sink, nnodes-1);

    // there might be outstanding replies
    // need to wait for their completion
    //_recv_replies(sink);
}


// build tree for tree barrier algorithm
int * CollectiveAccel::AccelNode::_build_tree(int radix, int *parent, int *num_children)
{
  int tmp_radix, my_root = 0;
  const int nnodes = _xl._nodes.size();
  *num_children = 0;
  for (int i = 1 ; i <= nnodes ; i *= radix) {
    tmp_radix = (nnodes / i < radix) ?
      (nnodes / i) + 1 : radix;
    my_root = (_me / (tmp_radix * i)) * (tmp_radix * i);
    if (my_root != _me) break;
    for (int j = 1 ; j < tmp_radix ; ++j) {
      if (_me + i * j < nnodes) {
        (*num_children)++;
      }
    }
  }

  int * children = (int*) malloc(sizeof(int) * (*num_children));

  int k = *num_children - 1;
  for (int i = 1 ; i <= nnodes ; i *= radix) {
    tmp_radix = (nnodes / i < radix) ?
      (nnodes / i) + 1 : radix;
    my_root = (_me / (tmp_radix * i)) * (tmp_radix * i);
    if (my_root != _me) break;
    for (int j = 1 ; j < tmp_radix ; ++j) {
      if (_me + i * j < nnodes) {
        children[k--] = _me + i * j;
      }
    }
  }
  *parent = my_root;
  return children;
}


// tree barrier
void CollectiveAccel::AccelNode::_remote_barrier_tree(sink_t& sink)
{
  int radix = _xl._barrier_radix;
  int parent, num_children; //*children;
  int * children = _build_tree(radix, &parent, &num_children);

  if (num_children != 0) {
    /* Not a pure leaf node */
    int i;

    /* wait for num_children calling up the tree */
    _recv_multiple(sink, num_children);

    if (parent == _me) {
      /* The root of the tree */

      /* Send acks down to children */
      for (i = 0 ; i < num_children ; ++i) {
        _send_to(children[i]);
      }

    } else {
      /* Middle of the tree */

      /* send ack to parent */
      _send_to(parent);

      /* wait for ack from parent */
      _recv(sink, parent);

      /* Send acks down to children */
      for (i = 0 ; i < num_children ; ++i) {
        _send_to(children[i]);
      }
    }

  } else {
    /* Leaf node */

    /* send message up psync tree */
    _send_to(parent);

    /* wait for ack down psync tree */
    _recv(sink, parent);
  }

  // there might be outstanding replies
  // need to wait for their completion
  //_recv_replies(sink);

  free(children);
}

// dissemination barrier
void CollectiveAccel::AccelNode::_remote_barrier_dissem(sink_t& sink)
{

  //int distance, to, i;
  int distance, to;
  int coll_rank = _me;
  const int nnodes = _xl._nodes.size();

  /* need log2(num_procs) int slots.  max_num_procs is
     2^(sizeof(int)*8-1)-1, so make the math a bit easier and assume
     2^(sizeof(int) * 8), which means log2(num_procs) is always less
     than sizeof(int) * 8. */
  /* Note: pSync can be treated as a byte array rather than an int array to
   * get better cache locality.  We chose int here for portability, since SUM
   * on INT is required by the SHMEM atomics API. */
  for (distance = 1 ; distance < nnodes ; distance <<= 1) {
    to = ((coll_rank + distance) % nnodes);
    int from = coll_rank-distance;
    if(from < 0)
      from = nnodes - distance + coll_rank;

    //
    _send_to(to);


    // wait until we receive a message from another node
    _recv(sink, from);
  }

  // there might be outstanding replies
  // need to wait for their completion
  //_recv_replies(sink);
}

// butterfly barrier
void CollectiveAccel::AccelNode::_remote_barrier_btfly(sink_t& sink)
{
  //int distance, i;
  int distance;
  const int nnodes = _xl._nodes.size();

  for (distance = 1 ; distance < nnodes ; distance <<= 1) {

    int grp_id = _me / (distance*2);
    int to = ((_me + distance) % (distance*2)) + grp_id * (distance*2);
    //
    _send_to(to);

    // wait until we receive a message from another node
    while((int)_net_inq.size() == 0)
      sink(0);
    _net_inq.clear();
  }

  // there might be outstanding replies
  // need to wait for their completion
  //_recv_replies(sink);
}

//---------------------------- reductions ----------------------------//


// perform one barrier synchronization
void CollectiveAccel::AccelNode::_allreduce(sink_t& sink)
{
    // wait for a request from each local PE
    _wait_local_pes(sink);
    // FIX? sanity check the requests to make sure they match?
    //

    // Perform reduction for the local PEs
    auto m = _inflight.front();
    auto r = _req_msg(m);
    _local_reduction(sink, _xl._nppn-1, r->count, r->type_size);

    // invoke the configured algorithm
    (this->*(_xl._remote_allreduce))(sink);


    // Send data back to local PEs
    _notify_local_pes(sink);
}


void CollectiveAccel::AccelNode::_remote_allreduce_linear(sink_t& sink)
{

  int nnodes = _xl._nodes.size();
  int count = _req_msg(_inflight.front())->count;
  int type_size = _req_msg(_inflight.front())->type_size;


  if(_me == _xl._barrier_root) {

    for (int node = 0; node < nnodes; ++node) {
      if(node != _xl._barrier_root)
        _send_to(node, WorkloadMessage::SendRequest, sizeof(long));
    }

    _recv_and_reduce(sink, nnodes-1, count, type_size);

  } else {

      // wait for root's notification to send the data
      _recv(sink, _xl._barrier_root);

      // send data to barrier root
      _send_to(_xl._barrier_root, WorkloadMessage::PutRequest, count*type_size);

      // notify barrier root
      _send_to(_xl._barrier_root, WorkloadMessage::SendRequest, sizeof(long));
  }

  (this->*(_xl._remote_bcast))(sink);
}

void CollectiveAccel::AccelNode::_remote_allreduce_tree(sink_t& sink)
{

  int parent, num_children;
  int radix = _xl._barrier_radix;
  int count = _req_msg(_inflight.front())->count;
  int type_size = _req_msg(_inflight.front())->type_size;
  int * children = _build_tree(radix, &parent, &num_children);


  if (count == 0) return;

  if (0 != num_children) {
    int i;

    /* update our target buffer with our contribution.  The put
       will flush any atomic cache value that may currently
       exist. */
    _send_to(_me, WorkloadMessage::PutRequest, count * type_size);

    for (i = 0 ; i < num_children ; ++i) {
      _send_to(children[i], WorkloadMessage::SendRequest, sizeof(long));
    }

    /* Wait for data from other nodes */
    // Perform reduction for the local PEs
    _recv_and_reduce(sink, num_children, count, type_size);
  }

  if (parent != _me) {
    /* wait for clear to send */
    _recv(sink, parent);

    _send_to(parent, WorkloadMessage::PutRequest, count * type_size);
    _send_to(parent, WorkloadMessage::SendRequest, sizeof(long));
  }

  (this->*(_xl._remote_bcast))(sink);
}

void CollectiveAccel::AccelNode::_remote_allreduce_ring(sink_t& sink)
{
  const int nnodes = _xl._nodes.size();
  int count = _req_msg(_inflight.front())->count;
  int type_size = _req_msg(_inflight.front())->type_size;
  int peer_to_send = (_me+1) % nnodes;
  int peer_to_recv = ((_me - 1) < 0 ? nnodes-1 : _me - 1);

  for (int i=0; i<nnodes-1; i++) {
    size_t chunk_in  = (_me - i - 1 + nnodes) % nnodes;
    size_t chunk_out = (_me - i + nnodes) % nnodes;

    /* Evenly distribute extra elements across first count % PE_size chunks */
    size_t chunk_in_extra  = (int) chunk_in  < count % nnodes;
    size_t chunk_out_extra = (int) chunk_out < count % nnodes;
    size_t chunk_in_count  = (int) count/nnodes + chunk_in_extra;
    size_t chunk_out_count = (int) count/nnodes + chunk_out_extra;


    _send_to(peer_to_send, WorkloadMessage::PutRequest, chunk_out_count * type_size);

    _send_to(peer_to_send, WorkloadMessage::SendRequest, sizeof(long));

    // wait until we receive a message from another node
    _recv(sink, peer_to_recv);

    // Add local reduction latency
    _local_reduction(sink, 1, chunk_in_count, type_size);

  }


  for (int i = 0; i < nnodes - 1; i++) {
    size_t chunk_out = (_me + 1 - i + nnodes) % nnodes;
    size_t chunk_out_extra = (int) chunk_out < count % nnodes;
    size_t chunk_out_count = count/nnodes + chunk_out_extra;

    _send_to(peer_to_send, WorkloadMessage::PutRequest, chunk_out_count * type_size);

    _send_to(peer_to_send, WorkloadMessage::SendRequest, sizeof(long));

    // wait until we receive a message from another node
    _recv(sink, peer_to_recv);
  }

  // there might be outstanding replies
  // need to wait for their completion
  _recv_replies(sink);

}

void CollectiveAccel::AccelNode::_remote_allreduce_recdbl(sink_t& sink)
{

  // Read request information from request message
  const int nnodes = _xl._nodes.size();
  int count = _req_msg(_inflight.front())->count;
  int type_size = _req_msg(_inflight.front())->type_size;

  //
  int my_id = _me;
  int log2_proc = 1, pow2_proc = 2;
  int i = nnodes >> 1;
  size_t wrk_size = type_size*count;


  if (count == 0) {
    return;
  }

  while (i != 1) {
    i >>= 1;
    pow2_proc <<= 1;
    log2_proc++;
  }

   /* Algorithm: reduce N number of PE's into a power of two recursive
   * doubling algorithm have extra_peers do the operation with one of the
   * power of two PE's so the information is in the power of two algorithm,
   * at the end, update extra_peers with answer found by power of two team
   *
   * -target is used as "temp" buffer -- current_target tracks latest result
   * give partner current_result,
   */

  /* extra peer exchange: grab information from extra_peer so its part of
   * pairwise exchange */
  if (my_id >= pow2_proc) {
    int peer = (my_id - pow2_proc);


    // wait until we receive a message from peer
    _recv(sink, peer);

    // Put data to your peer
    _send_to(peer, WorkloadMessage::PutRequest, wrk_size);

    // Notify your peer
    _send_to(peer, WorkloadMessage::SendRequest, sizeof(long));

    // wait until we receive a message from peer
    _recv(sink, peer);

  } else {
    if (my_id < nnodes - pow2_proc) {
      int peer = (my_id + pow2_proc);

      // Notify your extra peer
      _send_to(peer, WorkloadMessage::SendRequest, sizeof(long));

      // wait until we receive a message from peer
      _recv(sink, peer);

      // Perform reduction for the extra peer's data
      _local_reduction(sink, 1, count, type_size);
    }

    /* Pairwise exchange: (only for PE's that are within the power of 2
     * set) with every iteration, the information from each previous
     * exchange is passed forward in the new interation */

    for (i = 0; i < log2_proc; i++) {
      int peer = (my_id ^ (1 << i));

      if (_me < peer) {

         _send_to(peer, WorkloadMessage::SendRequest, sizeof(long));
         // wait until we receive a message from peer
         _recv(sink, peer);


         // Put data to your peer
         _send_to(peer, WorkloadMessage::PutRequest, wrk_size);

         // Notify your peer
         _send_to(peer, WorkloadMessage::SendRequest, sizeof(long));
      }
      else {

        // wait until we receive a message from peer
        _recv(sink, peer);

        // Put data to your peer
        _send_to(peer, WorkloadMessage::PutRequest, wrk_size);

        // Notify your peer
        _send_to(peer, WorkloadMessage::SendRequest, sizeof(long));

        // wait until we receive a message from peer
        _recv(sink, peer);
      }

      // add latency for local reduction
      _local_reduction(sink, 1, count, type_size);
      //shmem_internal_reduce_local(op, datatype, count,
      //    target, current_target);
    }


    /* update extra peer with the final result from the pairwise exchange */
    if (my_id < nnodes - pow2_proc) {
      int peer = (my_id + pow2_proc);

      // Put data to extra peer
      _send_to(peer, WorkloadMessage::PutRequest, wrk_size);

      // Notify the extra peer
      _send_to(peer, WorkloadMessage::SendRequest, sizeof(long));
    }
  }
}

void CollectiveAccel::AccelNode::_remote_allreduce_rabenseifner(sink_t&)
{ throw std::logic_error("collxl: Rabenseifner all-reduce not implemented"); }

//---------------------------- broadcasts ----------------------------//

void CollectiveAccel::AccelNode::_bcast(sink_t& sink)
{

  (this->*(_xl._remote_bcast))(sink);

}


void CollectiveAccel::AccelNode::_remote_bcast_linear(sink_t& sink)
{


   int count = _req_msg(_inflight.front())->count;
   int type_size = _req_msg(_inflight.front())->type_size;
   const int nnodes = _xl._nodes.size();
   const int root   = _xl._barrier_root;
   const int size   = count * type_size;

   if(_me == root) {
     // Send data
     for (int i=1; i<nnodes; ++i) {
         _send_to(i, WorkloadMessage::PutRequest, size);
     }
     // Notify children
     for (int i=1; i<nnodes; ++i) {
         _send_to(i, WorkloadMessage::SendRequest, sizeof(long));
     }
   } else {
     // wait until we receive a message from root
     _recv(sink, root);
   }

}

void CollectiveAccel::AccelNode::_remote_bcast_tree(sink_t& sink)
{

   int count = _req_msg(_inflight.front())->count;
   int type_size = _req_msg(_inflight.front())->type_size;

   int len = count * type_size;

   int radix = _xl._barrier_radix;
   int parent, num_children; //*children;
   int * children = _build_tree(radix, &parent, &num_children);

   if (0 != num_children) {
    int i;

    if (parent != _me) {
      /* wait for data arrival message if not the root */
      _recv(sink, parent);
    }

    /* send data to all leaves */
    for (i = 0 ; i < num_children ; ++i) {
      _send_to(children[i], WorkloadMessage::PutRequest, len);
    }

    /* send completion ack to all peers */
    for (i = 0 ; i < num_children ; ++i) {
      _send_to(children[i], WorkloadMessage::SendRequest, sizeof(long));
    }
    //_thread->QUIET();

  } else {
    /* wait for data arrival message */
    _recv(sink, parent);
  }

}
