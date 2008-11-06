#ifndef _IQ_ROUTER_HPP_
#define _IQ_ROUTER_HPP_

#include <string>
#include <queue>

#include "module.hpp"
#include "router.hpp"
#include "vc.hpp"
#include "allocator.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer_state.hpp"
#include "pipefifo.hpp"

class SwitchMonitor {
  int  _cycles ;
  int  _inputs ;
  int  _outputs ;
  int* _event ;
  int index( int input, int output, int flitType ) const ;
public:
  SwitchMonitor( int inputs, int outputs ) ;
  void cycle() ;
  int* GetActivity(){return _event;}
  int NumInputs(){return _inputs;}
  int NumOutputs(){return _outputs;}
  void traversal( int input, int output, Flit* flit ) ;
  friend ostream& operator<<( ostream& os, const SwitchMonitor& obj ) ;
  
} ;

class BufferMonitor {
  int  _cycles ;
  int  _inputs ;
  int* _reads ;
  int* _writes ;
  int index( int input, int flitType ) const ;
public:
  BufferMonitor( int inputs ) ;
  void cycle() ;
  void write( int input, Flit* flit ) ;
  void read( int input, Flit* flit ) ;
  int* GetReads(){return _reads;}
  int* GetWrites(){return _writes;}
  int NumInputs(){return _inputs;}
  friend ostream& operator<<( ostream& os, const BufferMonitor& obj ) ;
} ;

class IQRouter : public Router {
  int  _vcs ;
  int  _vc_size ;
  int  _speculative ;
  int  _filter_spec_grants ;

  VC          **_vc;
  BufferState *_next_vcs;

  Allocator *_vc_allocator;
  Allocator *_sw_allocator;

  int *_sw_rr_offset;

  tRoutingFunction   _rf;

  PipelineFIFO<Flit>   *_crossbar_pipe;
  PipelineFIFO<Credit> *_credit_pipe;

  queue<Flit *> *_input_buffer;
  queue<Flit *> *_output_buffer;

  queue<Credit *> *_in_cred_buffer;
  queue<Credit *> *_out_cred_buffer;

  int _hold_switch_for_packet;
  int *_switch_hold_in;
  int *_switch_hold_out;
  int *_switch_hold_vc;

  void _ReceiveFlits( );
  void _ReceiveCredits( );

  void _InputQueuing( );
  void _Route( );
  void _VCAlloc( );
  void _SWAlloc( );
  void _OutputQueuing( );

  void _SendFlits( );
  void _SendCredits( );
  
  void _AddVCRequests( VC* cur_vc, int input_index, bool watch = false );
  // ----------------------------------------
  //
  //   Router Power Modelling
  //
  // ----------------------------------------
public:
  SwitchMonitor switchMonitor ;
  
public:

  BufferMonitor bufferMonitor ;
  
public:
  IQRouter( const Configuration& config,
	    Module *parent, string name, int id,
	    int inputs, int outputs );
  
  virtual ~IQRouter( );
  
  virtual void ReadInputs( );
  virtual void InternalStep( );
  virtual void WriteOutputs( );
  
  void Display( ) const;
  virtual int GetCredit(int out, int vc_begin, int vc_end ) const;
  virtual int GetBuffer(int i) const;
  
  SwitchMonitor* GetSwitchMonitor(){return &switchMonitor;}
  BufferMonitor* GetBufferMonitor(){return &bufferMonitor;}
};

#endif
