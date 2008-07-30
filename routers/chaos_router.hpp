#ifndef _CHAOS_ROUTER_HPP_
#define _CHAOS_ROUTER_HPP_

#include <string>
#include <queue>

#include "module.hpp"
#include "router.hpp"
#include "allocator.hpp"
#include "routefunc.hpp"
#include "selectfunc.hpp"
#include "outputset.hpp"
#include "buffer_state.hpp"
#include "pipefifo.hpp"
#include "vc.hpp"

class ChaosRouter : public Router {

  tRoutingFunction   _rf;

  OutputSet **_input_route, **_mq_route;

  enum eQState {
    empty,         //            input avail
    filling,       //    >**H    ready to send
    full,          //  T****H    ready to send
    leaving,       //    T***>   input avail
    cut_through,   //    >***>
    shared         // >**HT**>
  };

  PipelineFIFO<Flit>   *_crossbar_pipe;

  int _multi_queue_size;
  int _buffer_size;

  queue<Flit *> *_input_frame;
  queue<Flit *> *_output_frame;
  queue<Flit *> *_multi_queue;

  int *_next_queue_cnt;

  queue<Credit *> *_credit_queue;

  eQState *_input_state;
  eQState *_multi_state;

  int *_input_output_match, *_input_mq_match, *_multi_match;

  int *_mq_age;

  bool *_output_matched;
  bool *_mq_matched;

  int _cur_channel;
  int _read_stall;

  bool _IsInjectionChan( int chan ) const;
  bool _IsEjectionChan( int chan ) const;

  bool _InputReady( int input ) const;
  bool _OutputFull( int out ) const;
  bool _OutputAvail( int out ) const;
  bool _MultiQueueFull( int mq ) const;

  int  _InputForOutput( int output ) const;
  int  _MultiQueueForOutput( int output ) const;
  int  _FindAvailMultiQueue( ) const;

  void _NextInterestingChannel( );
  void _OutputAdvance( );
  void _SendFlits( );
  void _SendCredits( );

public:
  ChaosRouter( const Configuration& config,
	    Module *parent, string name, int id,
	    int inputs, int outputs );

  virtual ~ChaosRouter( );

  virtual void ReadInputs( );
  virtual void InternalStep( );
  virtual void WriteOutputs( );

  void Display( ) const;
};

#endif _CHAOS_ROUTER_HPP_
