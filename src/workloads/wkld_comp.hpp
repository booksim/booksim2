/*
 * workload component
 *
 * C. Beckmann and H. Dogan (c) Intel, 2023
 *
 */
#pragma once

#include <assert.h>
#include <vector>
#include <string>
#include <swm.hpp>
#include "globals.hpp"
#include "wkld_msg.hpp"
#include "swm_globals.hpp"
#include "roi.hpp"

using namespace std;

typedef WorkloadMessage::msg_t msg_t;
//
// workload component.
// Base class for traffic generators or modifiers
//
class WorkloadComponent
{
      // get() may be called multiple times per cycle, _get_new() only once. Derived classes override
      virtual WorkloadMessagePtr _get_new(int src) { assert(false); return 0; }

   protected:
      WorkloadComponent *const _upstream;
      vector<WorkloadMessagePtr> _last_get;

   public:
      WorkloadComponent(WorkloadComponent *up = 0) : _upstream(up) {
          _last_get.resize(gNodes, 0);
      }
      virtual bool test(int src) = 0;
      virtual WorkloadMessagePtr get(int src) {
          return _last_get[src] ? _last_get[src] : (_last_get[src] = _get_new(src));
      }
      virtual void next(int src) { _last_get[src] = 0; }; // derived class next() must call this unless overriding get()
      virtual void eject(WorkloadMessagePtr m) {};

      virtual void Init(int pes, Configuration const *) { _last_get.resize(pes, 0); };
      virtual void FunctionalSim() {};
      static WorkloadComponent* New(const string &, int, const vector<string> &, Configuration const *, WorkloadComponent *);
      virtual ~WorkloadComponent() = default;

      // get an upstream component of the given type (if none, return NULL)
      template <class T> T * ComponentOfType() {
         for (auto p = this; p; p = p->_upstream)
            if (T* q = dynamic_cast<T*>(p))
               return q;
         return 0;
      }

   protected:
      class FactoryBase;
      static map<string, FactoryBase*> factories;
      class FactoryBase {
         public:
            FactoryBase(const string &kind) {
               WorkloadComponent::factories.insert({kind, this});
            }
            virtual WorkloadComponent* New(int, const vector<string> &, Configuration const *, WorkloadComponent *) = 0;
      };
      template<class WC> class Factory : FactoryBase {
         public:
            Factory(const string &kind) : FactoryBase(kind) {}
            virtual WorkloadComponent* New(int nodes, const vector<string> &options, Configuration const * config, WorkloadComponent *upstrm) {
               return new WC(nodes, options, config, upstrm);
            }
      };
};

// derived classes should inherit from this...
template <class T> class WComp : public WorkloadComponent
{
   static WorkloadComponent::Factory<T> _factory;
  public:
   WComp(WorkloadComponent *up = 0) : WorkloadComponent(up) {}
};
// ...and use this macro in the .cpp file to install in list of available types
#define PUBLISH_WORKLOAD_COMPONENT(_class_, _name_) \
   template<> WorkloadComponent::Factory<_class_> WComp<_class_>::_factory(_name_)
