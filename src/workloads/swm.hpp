/*
 * swm.hpp
 *
 * Scalable workload model traffic generator for Booksim
 *
 *  Created on: Jun 9, 2022
 *      Author: cjbeckma
 */

#pragma once

#include <assert.h>
#include <iostream>
#include <vector>
#include <list>
#include <boost/coroutine/all.hpp>
#include "config_utils.hpp"
#include "misc_utils.hpp"
#include "globals.hpp"
#include "swm_globals.hpp"
#include "wkld_msg.hpp"
#include "pt.hpp"



// pipetrace
namespace pt
{
    // record types
    extern Pipe_Recordtype* SwmPe;
    // data types
    extern Pipe_Datatype* SwmPeId;
    // event types
    extern Pipe_Eventtype* SwmStart;
    extern Pipe_Eventtype* SwmEnd;
}

//
// class representing a single thread or process
//

class SwmThread : public GeneratorWorkloadMessage::Factory, pt::Traceable {
  public:
    typedef uint64_t cycle_t;                   // time in cycles

  protected:
    // SWM behavioral interface
    //   you can write a workload behavioral routine as a method on this class.
    //   It has access to the following API for defining the workload behavior:
    const int _me;                              // my process ID
    const int _np;                              // total number of processes

    int _put = 0;                               // stats for counting fabric requests/replies
    int _get = 0;
    int _send = 0;
    int _reply = 0;
    int _xlmsg = 0;

    void      thread_yield();                   // yield to another thread by sending dummy message
    void      work(cycle_t);                    // simulate <c> cycles of local work (i.e. delay for c cycles)
    void      put(int sz, int tgt);             // PUT request of size <sz> bytes to PE <tgt>. Generates a WRITE request
    void      get(int sz, int tgt);             // GET request of size <sz> bytes from <tgt>. Generates a READ request, and blocks until response received
    void      getnb(int sz, int tgt);           // Nonblocking GET request of size <sz> bytes from <tgt>. Generates a READ request, does not block
    void      send(int sz, int tgt);            // Send a message of size <sz> bytes to PE <tgt>
    void      recv(int tgt);                    // Receive a message from <tgt>.  Blocks until msg received.
    void      quiet();                          // Used to wait for reply messages
    void      reset()                           { _state = message; _time = 0; }
    cycle_t   get_time() const                  { return _time; }
    void      set_time(cycle_t time)            {  _time = time;}

//#if DEBUG > 0
//#   define    DBGPRINT(x) \
    //std::cout << "CYC " << get_time() << " PE " << _me << ' ' << x << std::endl
//#else
//#   define    DBGPRINT(x)
//#endif

#   define    DBGPRINT(x) \
    //std::cout << "CYC " << get_time() << " PE " << _me << ' ' << x << std::endl

  public:

    // Generator interface
    //   a traffic injector invokes this to run the model and generate message traffic:
    //
    int       get_id() const                       { return _me; }
    int       get_num_pes() const                  { return _np; }
    bool      has_packet(cycle_t now) const        { return _state == message && _time <= now; }
    WorkloadMessagePtr get_packet() const          { return _coro->get(); }
    void      next(cycle_t);                       // after getting outgoing packet, run and generate next packet
    void      reply(cycle_t, WorkloadMessagePtr);  // provide reply and run
    void      sendin(cycle_t, WorkloadMessagePtr); // provide incoming SEND and run
    bool      is_done() const                      { return _state == done; }
    void      set_done()                           { _state = done; }
    bool      is_quiet() const                     { return _state == quiet_wait; }
    Configuration const * getConfig() const        { return _config; }

    // public interface for put/get/send/recv
    void      PUT(int sz, int tgt) { put(sz, tgt); }     // PUT request of size <sz> bytes to PE <tgt>. Generates a WRITE request
    void      GET(int sz, int tgt) { get(sz, tgt); }     // GET request of size <sz> bytes from <tgt>. Generates a READ request,
    // and blocks until response received
    void      GETNB(int sz, int tgt) { getnb(sz, tgt); } // Nonblocking GET request of size <sz> bytes from <tgt>. Generates a READ request,
    void      SEND(int sz, int tgt) { send(sz, tgt); }   // Send a message of size <sz> bytes to PE <tgt>
    void      RECV(int tgt) { recv(tgt); }               // Receive a message from <tgt>.  Blocks until msg received.
    void      QUIET() { quiet(); }
    void      WORK(cycle_t w) { work(w); }
    void      YIELD() { thread_yield(); }
    void      ACCELREQ(WorkloadMessagePtr);              // accelerator request message

    void    SwmMarker(int marker);
    void    SwmEndMarker(int marker);
    void    SwmReset() { reset(); }
    cycle_t getTime() { return get_time(); }

    // Notify booksim to clear the stats
    void clear_stats();

    // construction and initialization
    struct factory_base {                                // factory base class
      virtual SwmThread *make(GeneratorWorkloadMessage::Factory *, int, int, Configuration const * const) = 0;
    };
    template <class SUB>
      struct factory : public factory_base {             // workload-specific factory
        virtual SwmThread *make(GeneratorWorkloadMessage::Factory *f, int id, int np, Configuration const * const config) { return new SUB(f, id, np, config); }
      };
  protected:
    SwmThread(GeneratorWorkloadMessage::Factory *f, int id, int np, Configuration const * const config) // private constructor
      : GeneratorWorkloadMessage::Factory(*f),
        pt::Traceable(pt::SwmPe, true),
        _me(id), _np(np), _config(config),
        _msg_overhead(config->GetInt("swm_msg_overhead")), _put_overhead(_msg_overhead), _get_overhead(_msg_overhead),
        _send_overhead(_msg_overhead), _recv_overhead(_msg_overhead), _xlmsg_overhead(_msg_overhead),
        _state(ready), _time(0), _coro(0), _sink(0)
    {
        _record_data(pt::SwmPeId, _me);
    }

    void start();                                        // start coroutine running after derived class initialized

  private:
    // behavioral routine - the workload model
    virtual void behavior(int argc, char * argv[]) = 0;  // derived classes provide workload behavior

    // internal types
    //
    enum    state_t { ready, message, wait, quiet_wait, done };
    typedef boost::coroutines::asymmetric_coroutine<WorkloadMessagePtr>::pull_type coro_t;
    typedef boost::coroutines::asymmetric_coroutine<WorkloadMessagePtr>::push_type sink_t;

    // static members
    static int sim_terminate;

    // internal data members
    Configuration const * const   _config;
    const cycle_t                 _msg_overhead;         // additional work cycles for sending various kinds of messages
    const cycle_t                 _put_overhead;
    const cycle_t                 _get_overhead;
    const cycle_t                 _send_overhead;
    const cycle_t                 _recv_overhead;
    const cycle_t                 _xlmsg_overhead;
    state_t                       _state;
    cycle_t                       _time;
    coro_t *                      _coro;
    sink_t *                      _sink;
    std::list<WorkloadMessagePtr> _recvd;                // unmatched received messages
    std::list<WorkloadMessagePtr> _outstanding_acks;     // keep track of acks and replies

    // internal methods
    void parse_args(int &argc, char ** &argv);
    void wrapper(sink_t &sink);
    bool is_waiting(WorkloadMessagePtr) const;           // is the process waiting for a reply or a send?
    void go(cycle_t);                                    // run the model to generate the next packet
};


// macros for declaration boilerplate
#define SWM_CLASS(_c_) class _c_ : public SwmThread
#define SWM_INIT(_c_)  public: _c_(GeneratorWorkloadMessage::Factory *f, int id, int np, Configuration const * const config) \
                               : SwmThread(f, id, np, config) { start(); }


//
// helper class for ROI markers
//
class MarkerMessage : public GeneratorWorkloadMessage {
  public:
    // put marker ID in dest field, but return self for Dest()
    int Marker() const { return GeneratorWorkloadMessage::Dest(); }
    int Dest() const { return GeneratorWorkloadMessage::Source(); }
    MarkerMessage(Factory *f, int src, int marker)
      : GeneratorWorkloadMessage(f, src, marker, DummyRequest, false, 0, 0)
    {}
};
