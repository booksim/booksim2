/*
 * swm.cpp
 *
 * Booksim scalable workload model implementation
 *
 *  Created on: Jun 9, 2022
 *      Author: cjbeckma
 */
#include <swm.hpp>

//
// the SWM thread class implementation
//
//
//
//
//

// static members
int SwmThread::sim_terminate = 0;

// pipetrace
namespace pt
{
    // record types
    Pipe_Recordtype* SwmPe = pipe_new_recordtype("SWM_PE", "Scalable workload model Processing element");
    // data types
    Pipe_Datatype* SwmPeId     = pipe_new_datatype("PE",       Pipe_Integer, "SWM PE identifier");
    Pipe_Datatype* SwmPeTarget = pipe_new_datatype("target",   Pipe_Integer, "SWM message target PE");
    Pipe_Datatype* SwmMarkerId = pipe_new_datatype("markerID", Pipe_Integer, "SWM marker ID");
    // event types
    Pipe_Eventtype* SwmStart    = pipe_new_eventtype("swm_start",     '*', Pipe_Green,  "PE started");
    Pipe_Eventtype* SwmEnd      = pipe_new_eventtype("swm_end",       '*', Pipe_Red,    "PE finished");
    Pipe_Eventtype* SwmWork     = pipe_new_eventtype("swm_working",   'W', Pipe_Blue,   "PE working");
    Pipe_Eventtype* SwmOverhead = pipe_new_eventtype("swm_overhead",  'w', Pipe_Gray,   "PE communication overhead work");
    Pipe_Eventtype* SwmMsgPut   = pipe_new_eventtype("swm_msg_put",   'P', Pipe_Yellow, "PE has PUT message");
    Pipe_Eventtype* SwmMsgGet   = pipe_new_eventtype("swm_msg_get",   'G', Pipe_Yellow, "PE has GET message");
    Pipe_Eventtype* SwmMsgGetNb = pipe_new_eventtype("swm_msg_getnb", 'G', Pipe_Green,  "PE has GET message (nonblocking)");
    Pipe_Eventtype* SwmMsgSend  = pipe_new_eventtype("swm_msg_send",  'S', Pipe_Yellow, "PE has SEND message");
    Pipe_Eventtype* SwmMsgAccel = pipe_new_eventtype("swm_msg_accel", 'X', Pipe_Purple, "PE has ACCELREQ message");
    Pipe_Eventtype* SwmWait     = pipe_new_eventtype("swm_wait",      '~', Pipe_White,  "PE waiting for message");
    Pipe_Eventtype* SwmQuiet    = pipe_new_eventtype("swm_quiet",     '_', Pipe_White,  "PE quiet waiting");
    Pipe_Eventtype* SwmMarker   = pipe_new_eventtype("swm_marker",    'm', Pipe_Orange, "PE at marker instruction");
    Pipe_Eventtype* SwmYield    = pipe_new_eventtype("swm_yield",     'y', Pipe_Orange, "PE yield");
}

static int clear_stats_cnt = 0;
// Notify booksim to clear the stats
void SwmThread::clear_stats() 
{
   quiet();

   clear_stats_cnt++; 
   if(clear_stats_cnt == _np) {
      gClearStats = true;
      clear_stats_cnt = 0;
   }
}

// after derived class is initialized it is safe to call this to start coroutine
void SwmThread::start()
{
   _coro = new coro_t(std::bind(&SwmThread::wrapper, this, std::placeholders::_1));
}

void SwmThread::parse_args(int &argc, char ** &argv)
{
    string args = _config->GetStr("swm_args");
    vector<string> arg_tokens = tokenize_str(args);

    // Create the `argv` array.
    argc = arg_tokens.size()+1;
    argv = new char*[argc + 1];
    string name = "swm";
    argv[0] = new char[name.length()+1];
    strcpy(argv[0], name.c_str());
    for (int i = 1; i < argc; ++i) {
        argv[i] = new char[arg_tokens[i-1].length() + 1];
        strcpy(argv[i], arg_tokens[i-1].c_str());
    }
    argv[argc] = nullptr;
}

void SwmThread::wrapper(sink_t &sink)
{
    _sink = &sink;

    gSwm = true;
    if(!_config->GetInt("roi")) {
        gSimEnabled = true;
    }

    // parse arguments for the swm workloads
    int argc=0;
    char ** argv=nullptr;
    parse_args(argc, argv);

    // SWM behavior
    behavior(argc, argv);

    // wait for the completion of outstanding
    // requests and replies
    quiet();

    _state = done;

    sim_terminate++;
    if(sim_terminate == _np)
        gSimEnabled = false;
}


// SWM INTERFACE

// simulate some number of cycles of local work
void SwmThread::work(cycle_t cycles)
{
   _record_event(_time, pt::SwmWork, cycles);
   _state = ready;
   _time += cycles;
}

// generate a PUT message
void SwmThread::put(int size, int dest)
{
   _record_event(_time, pt::SwmOverhead, _put_overhead);
   _state = message;
   _put++;
   _time += _put_overhead;
   _record_event(_time, pt::SwmMsgPut, 1, pt::SwmPeTarget, dest);
   DBGPRINT("PUT to " << dest);
   (*_sink)(new GeneratorWorkloadMessage(this, _me, dest, WorkloadMessage::PutRequest, false, size));
}

// generate a GET request message
void SwmThread::get(int size, int dest)
{
   _record_event(_time, pt::SwmOverhead, _get_overhead);
   _state = message;
   _get++;
   _time += _get_overhead;
   _record_event(_time, pt::SwmMsgGet, 1, pt::SwmPeTarget, dest);
   DBGPRINT("GET from " << dest);
   (*_sink)(new GeneratorWorkloadMessage(this, _me, dest, WorkloadMessage::GetRequest, false, size));
}

// generate a non-blocking GET request message
void SwmThread::getnb(int size, int dest)
{
   _record_event(_time, pt::SwmOverhead, _get_overhead);
   _state = message;
   _get++;
   _time += _get_overhead;
   _record_event(_time, pt::SwmMsgGetNb, 1, pt::SwmPeTarget, dest);
   DBGPRINT("NBGET from " << dest);
   (*_sink)(new GeneratorWorkloadMessage(this, _me, dest, WorkloadMessage::NbGetRequest, false, size));
}

// generate a SEND message
void SwmThread::send(int size, int dest)
{
   _record_event(_time, pt::SwmOverhead, _send_overhead);
   _state = message;
   _send++;
   //_time += PUT_OVHD;
   DBGPRINT("SEND to " << dest);
   _time += _send_overhead;
   _record_event(_time, pt::SwmMsgSend, 1, pt::SwmPeTarget, dest);
   (*_sink)(new GeneratorWorkloadMessage(this, _me, dest, WorkloadMessage::SendRequest, false, size));
}

// RECV a message
void SwmThread::recv(int src)
{
   _record_event(_time, pt::SwmOverhead, _recv_overhead, pt::SwmPeTarget, src);
   //_time += _recv_overhead;
   // first see if it matches any previously received messages
   for (auto i = _recvd.begin(); i != _recvd.end(); ++i)
   {
      DBGPRINT("RECV from " << (*i)->Source());
      if ((*i)->Source() == src) {
         _time += _recv_overhead;
         _recvd.erase(i);
         return;
      }
   }
   // if not, put myself into wait state and yield a bogus message
   _state = wait;
   DBGPRINT("RECV wait for data to arrive state is wait src " <<  src);
   _record_event(_time, pt::SwmWait, 1, pt::SwmPeTarget, src);
   (*_sink)(new GeneratorWorkloadMessage(this, _me, src, WorkloadMessage::RecvRequest));
}

// wait for non-blocking get replies and acks
void SwmThread::quiet() 
{
   while(!_outstanding_acks.empty()) {
      _state = quiet_wait;
      auto msg = _outstanding_acks.front();
      _record_event(_time, pt::SwmQuiet, 1, pt::SwmPeTarget, msg->Dest());
      DBGPRINT("outstanding acks " << _outstanding_acks.size() << " waiting for reply from " << msg->Dest());
      (*_sink)(msg);
   }
}

// local accelerator request message
void SwmThread::ACCELREQ(WorkloadMessagePtr m)
{
   _record_event(_time, pt::SwmOverhead, _xlmsg_overhead);
   _state = message;
   _xlmsg++;
   _time += _xlmsg_overhead;
   _record_event(_time, pt::SwmMsgAccel);
   DBGPRINT("ACCELREQ: " << *m);
   (*_sink)(m);
}

// TODO: maybe have another state called "yield"
void SwmThread::thread_yield()
{
   // Dummy request to yield to the other threads 
   _record_event(_time, pt::SwmYield);
   _state = message;
   (*_sink)(new GeneratorWorkloadMessage(this, _me, _me, WorkloadMessage::DummyRequest));
}

void SwmThread::SwmMarker(int marker)
{
   _record_event(_time, pt::SwmMarker, 1, pt::SwmMarkerId, marker);
   _state = message;
   (*_sink)(new MarkerMessage(this, _me, marker));
}

void SwmThread::SwmEndMarker(int marker)
{
   _record_event(_time, pt::SwmMarker, 1, pt::SwmMarkerId, marker);
   _state = message;
   (*_sink)(new MarkerMessage(this, _me, marker));
}


// GENERATOR INTERFACE

// unconditionally run the model to generate the next packet
   inline
void SwmThread::go(cycle_t now)
{
   _state = ready;
   _time = now;
   (*_coro)(); // resume the coroutine
   if (! *_coro) {
      _state = done;
   }
}

// update state and maybe advance the model execution
void SwmThread::next(cycle_t now)
{
   auto w = _coro->get();
   if (_state == message && w->IsBlocking()){
      _state = wait;
   }
   else if (_state != wait) {
      if(!w->IsDummy())
         _outstanding_acks.push_back(w);

      if(!is_done())
         go(now);
   }
}


// is the model process waiting for a network reply?
inline
bool SwmThread::is_waiting(WorkloadMessagePtr m) const
{
   if (_state != wait) { return false; }
   auto w = _coro->get();
   return (m->IsBlocking() && w->Dest() == m->Source());
}


// provide a network reply, and advance model execution
void SwmThread::reply(cycle_t now, WorkloadMessagePtr reply)
{
   DBGPRINT("got reply " << reply->Source());
   _reply++;
   if (is_waiting(reply))  {
      for (auto i = _outstanding_acks.begin(); i != _outstanding_acks.end(); ++i) {
         if ((*i)->Dest() == reply->Source()) {
            _outstanding_acks.erase(i);
            break;
         }
      }
      go(now);
   }
   else { // non-blocking get replies that arrive before fence
      for (auto i = _outstanding_acks.begin(); i != _outstanding_acks.end(); ++i) {
         if ((*i)->Dest() == reply->Source()) {
            _outstanding_acks.erase(i);
            if(_state == quiet_wait) {
               go(now);
            }
            break; 
         }
      }
   }
}

// provide a network SEND, and advance model execution
void SwmThread::sendin(cycle_t now, WorkloadMessagePtr sent)
{
   DBGPRINT("got SEND from " << sent->Source() << " at time " << now);
   //
   auto w = _coro->get();
   bool is_matched = w->Type() == WorkloadMessage::RecvRequest && w->Dest() == sent->Source(); 
   if(_state == wait && is_matched) { 
      _time = max(now, _time) + _recv_overhead; // max so you still incur overhead if message arrives early
      go(_time); 
   }
   else {
      DBGPRINT("unmatched");
      _recvd.push_back(sent);
   }
}
