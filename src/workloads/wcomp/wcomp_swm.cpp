/*
 * workload component implementation
 *
 * H. Dogan (c) Intel, 2023
 * 
 */
#include "wkld_comp.hpp"
#include "swm_test1.hpp"
#include "swm_barrier.hpp"
#include "swm_reduction.hpp"
#include "swm_ig.hpp"
#include "swm_histo.hpp"
#include "swm_randperm.hpp"
#include "swm_permute_matrix.hpp"
#include "swm_transpose_matrix.hpp"
#include "swm_toposort.hpp"
#include "all_reduce.hpp"

static int reply = 0;
static int req = 0;
static int reply_gen = 0;

void stats_wcomp_swm()
{
  printf("T=%d wcomp_swm req=%d reply=%d reply_gen=%d\n",GetSimTime(),req,reply,reply_gen);
}

// scalable workload model - primary traffic generator
class ScalableWorkloadModel : public WComp<ScalableWorkloadModel>, RoI,
                              private GeneratorWorkloadMessage::Factory
{
   string _name;        // name of the SWM

   // similar to SwmInjectionProcess
   vector<SwmThread *> _thread;        // array of SWM thread objects
   // static data
   static map<string, SwmThread::factory_base*> _wfact; // workload factory classes

   // support for generating replies, in case Booksim does not
   const bool _gen_replies; // do we need to generate replies?
   vector<list<WorkloadMessagePtr> > _replies_pending; // replies waiting to be sent

   public:
   ScalableWorkloadModel(int nodes, const vector<string> &options, Configuration const * const config, WorkloadComponent *upstrm);

   void FunctionalSim();
   void Init(int pes, Configuration const * const config);

   bool test(int src)
   {
      if (!_replies_pending[src].empty()) return true;

      // handle (and filter out) marker dummy messages
      WorkloadMessagePtr m; MarkerMessage *mm;
      auto & t(_thread[src]);
      while (t->has_packet(GetSimTime()) &&
             (m = t->get_packet(), m->IsDummy()) &&
             (mm = dynamic_cast<MarkerMessage*>(m.get()))) {
         int marker = mm->Marker();
         SwmRoI(marker, src);
         if(IsRoI(src, marker)) {
            t->set_done();
            return false;
         }
         t->next(GetSimTime());
      }

      return _thread[src]->has_packet(GetSimTime());
   }
   WorkloadMessagePtr _get_new(int src) {
      if (!_replies_pending[src].empty())
         return _replies_pending[src].front();
      // else...
      return _thread[src]->get_packet();
   }
   void next(int src)
   {
      if (!_replies_pending[src].empty()){
         reply++;
         _replies_pending[src].pop_front();
      }
      else{
         req++;
         _thread[src]->next(GetSimTime());
}

      WorkloadComponent::next(src);
   }
   void eject(WorkloadMessagePtr m)
   {
      int  dest = m->Dest();
      if (m->IsReply()) {
         _thread[dest]->reply(GetSimTime(), m);
      }
      else {
         if (m->Type() == WorkloadMessage::SendRequest) { // handle incoming SEND message
            _thread[dest]->sendin(GetSimTime(), m);
         }
         if (_gen_replies) { // generate reply message if needed
            reply_gen++;
            _replies_pending[dest].push_back(m->Reply());
         }
      }
   }
};


// map from names to workload-specific factory objects
map<string, SwmThread::factory_base*> ScalableWorkloadModel::_wfact = {
   {"test1",       new SwmThread::factory<SwmTest1>},
   {"ig",          new SwmThread::factory<SwmIg>},
   {"histo",       new SwmThread::factory<SwmHisto>},
   {"barrier",     new SwmThread::factory<SwmBarrier>},
   {"reduce",      new SwmThread::factory<SwmReduction>},
   {"randperm",    new SwmThread::factory<SwmRandPerm>},
   {"permmatrix",  new SwmThread::factory<SwmPermMatrix>},
   {"transmatrix", new SwmThread::factory<SwmTransposeMatrix>},
   {"toposort",    new SwmThread::factory<SwmToposort>},
   {"allreduce",   new SwmThread::factory<SwmAllReduce>},
};


// instantiate ScalableWorkloadModel factory object
PUBLISH_WORKLOAD_COMPONENT(ScalableWorkloadModel, "SWM");

/*
 * Scalable Workload Model (SWM) -- traffic generator
 */
ScalableWorkloadModel::ScalableWorkloadModel(int nodes, const vector<string> &options, Configuration const * const config, WorkloadComponent *upstrm)
   : WComp<ScalableWorkloadModel>(upstrm), RoI(config), GeneratorWorkloadMessage::Factory(config, gInjectorTrafficClass),
     _name(options[0]), _thread(0),
     _gen_replies(0 == GetIntIndexed(config, "use_read_write", gInjectorTrafficClass))
{}


void ScalableWorkloadModel::Init(int pes, Configuration const * const config)
{
   _thread.resize(pes);
   _replies_pending.resize(pes);
   try {
      auto factory = _wfact.at(_name);
      for (int i=0; i<pes; ++i)
         _thread[i] = factory->make(this, i, pes, config);
   }
   catch (const out_of_range &) {
      cerr << "Unkown SWM workload type: " << _name << endl;
      exit(-1);
   }
   
   RoI::Init(pes);
   if(config->GetInt("roi")) {
      FunctionalSim();
   }
}


void ScalableWorkloadModel::FunctionalSim() {

   typedef uint64_t                    cycle_t;
   typedef WorkloadMessagePtr          packet_t;
   typedef multimap<cycle_t, packet_t> packet_list_t;
   typedef pair<cycle_t, packet_t>     plist_item_t;

   bool                    done = false;       // is simulation done?
   cycle_t                 now = 0;        // current simulation time
   packet_list_t           inflight;   // in-flight packets
   std::vector<bool> t_done(_thread.size());
   for(auto i:t_done)
      i = false;

   while(!gSimEnabled && !done) {

      done = true;

      // handle incoming packets from the fabric
      packet_list_t::iterator pi;
      while ((pi = inflight.find(now)) != inflight.end()) {
         done = false;
         packet_t pkt = pi->second;
         if (pkt->IsReply()) {
            // if a reply, provide reply to waiting process
            _thread[pkt->Dest()]->reply(now, pkt);
         }
         else {
            if (pkt->Type() == WorkloadMessage::SendRequest) {
               // provide incoming SEND to destination process
               _thread[pkt->Dest()]->sendin(now, pkt);
            }
            // create an ack reply packet back to initiator
            inflight.insert(plist_item_t(now + 1, pkt->Reply()));
         }
         inflight.erase(pi);
      }

      // get outgoing packets to the fabric
      for (auto t : _thread) {
         int tid = t->get_id();
         if (t->has_packet(now) && !t_done[tid]) {
            done = false;

            auto p = t->get_packet();
            MarkerMessage *mm;
            if (p->IsDummy() && (mm = dynamic_cast<MarkerMessage*>(p.get()))) {
               int marker = mm->Marker();
               SwmRoI(marker, tid);
               if(IsRoI(tid, marker)) {
                  t_done[tid] = true;
                  t->set_done();
               }
            }

            bool is_dummy = (p->Type() == WorkloadMessage::DummyRequest);
            if(!is_dummy && !t->is_quiet()) {
               inflight.insert(plist_item_t(now + 1, p));
            }
            
            if(!t->is_done())
               t->next(now);
         }
         else if (!t->is_done())
            done = false; 
      }

      now++;
   }

   // Drain the inflight messages
   while(!inflight.empty()) {

      // handle incoming packets from the fabric
      packet_list_t::iterator pi;
      while ((pi = inflight.find(now)) != inflight.end()) {
         packet_t & pkt = pi->second;
         if (pkt->IsReply()) {
            // if a reply, provide reply to waiting process
            _thread[pkt->Dest()]->reply(now, pkt);
         }
         else {
            if (pkt->Type() == WorkloadMessage::SendRequest) {
               // provide incoming SEND to destination process
               _thread[pkt->Dest()]->sendin(now, pkt);
            }
            // create an ack reply packet back to initiator
            inflight.insert(plist_item_t(now + 1, pkt->Reply()));
         }
         inflight.erase(pi);
      }
      now++;
   }

   //
   for (auto t : _thread) {
      t->SwmReset();
   }

   printf("Functional Simulation Completed\n");
}

