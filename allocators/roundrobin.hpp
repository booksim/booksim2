// ----------------------------------------------------------------------
//
//  RoundRobin: RoundRobin Arbiter based Allocator
//
// ----------------------------------------------------------------------
#include "allocator.hpp"
#include "roundrobin_arb.hpp"
#include <list>

// ----------------------------------------------------------------------
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:10:51 $
//  $Id: roundrobin.hpp,v 1.1 2007/05/17 17:10:51 jbalfour Exp $
// ----------------------------------------------------------------------
class RoundRobin : public Allocator {

  int  _num_vcs ;
  int* _matched ;

  RoundRobinArbiter* _input_arb ;
  RoundRobinArbiter* _output_arb ;

  RoundRobinArbiter* _spec_input_arb ;
  RoundRobinArbiter* _spec_output_arb ;

  list<sRequest>* _in_req ;
  list<sRequest>* _out_req ;

public:
  
  RoundRobin( const Configuration& config, Module* parent,
	      const string& name, int inputs, int outputs ) ;
  
  ~RoundRobin() ;

  //
  // Allocator Interface
  //
  virtual void Clear() ;
  virtual int  ReadRequest( int in, int out ) const ;
  virtual bool ReadRequest( sRequest& req, int in, int out ) const ;
  virtual void AddRequest( int in, int out, int label = 1, 
			   int in_pri = 0, int out_pri = 0 ) ;
  virtual void RemoveRequest( int in, int out, int label = 1 ) ;
  virtual void Allocate() ;
  virtual void PrintRequests() const ;

} ;
