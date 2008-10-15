// ----------------------------------------------------------------------
//
//  SeparableAllocator: Separable Allocator
//
// ----------------------------------------------------------------------

#include "separable.hpp"

#include "booksim.hpp"
#include "roundrobin_arb.hpp"
#include "matrix_arb.hpp"

#include <vector>
#include <iostream>
#include <string.h>

SeparableAllocator::SeparableAllocator( const Configuration& config,
					Module* parent, const string& name,
					const string& arb_type,
					int inputs, int outputs )
  : Allocator( config, parent, name, inputs, outputs )
{
  
  _requests  = new list<sRequest> [inputs] ;
  
  _input_arb = new Arbiter*[inputs];
  _output_arb = new Arbiter*[outputs];
  
  if ( arb_type == "matrix" ) {
    for (int i = 0; i < inputs; ++i) {
      _input_arb[i] = new MatrixArbiter;
      _input_arb[i]->Init(outputs);
    }
    for (int i = 0; i < outputs; ++i) {
      _output_arb[i] = new MatrixArbiter;
      _output_arb[i]->Init(inputs);
    }
  } else if ( arb_type == "round_robin" ) {
    for (int i = 0; i < inputs; ++i) {
      _input_arb[i] = new RoundRobinArbiter;
      _input_arb[i]->Init(outputs);
    }
    for (int i = 0; i < outputs; ++i) {
      _output_arb[i] = new RoundRobinArbiter;
      _output_arb[i]->Init(inputs);
    }
  } else {
    assert(false);
  }
  
  Clear() ;
}

SeparableAllocator::~SeparableAllocator() {

  delete[] _requests ;

  for (int i = 0; i < _inputs; ++i) {
    delete _input_arb[i];
  }
  delete[] _input_arb ;

  for (int i = 0; i < _outputs; ++i) {
    delete _output_arb[i];
  }
  delete[] _output_arb ;
}

void SeparableAllocator::Clear() {

  for ( int i = 0 ; i < _inputs ; i++ ) 
    _requests[i].clear() ;

}

int SeparableAllocator::ReadRequest( int in, int out ) const {
  sRequest r ;
  if ( !ReadRequest( r, in, out) ) {
    return -1 ;
  } 
  return r.label ;
}

bool SeparableAllocator::ReadRequest( sRequest &req, int in, int out ) const {

  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  // Only those requests with non-negative priorities should be
  // returned to supress failing speculative allocations. This is
  // a bit of a hack, but is necessary because the router pipeline
  // queuries the results of the allocation through the requests
  int max_pri = -1 ;

  list<sRequest>::const_iterator match = _requests[in].begin() ;

  while ( match != _requests[in].end() ) {
    if ( match->port == out && match->in_pri > max_pri ) {
      req = *match ;
      max_pri = req.in_pri ;
    }
    match++ ;
  }

  return ( max_pri > -1 ) ;

}

void SeparableAllocator::AddRequest( int in, int out, int label, int in_pri,
				     int out_pri ) {

  assert( ( in >= 0 ) && ( in < _inputs ) &&
	  ( out >= 0 ) && ( out < _outputs ) );

  sRequest req ;
  req.port    = out ;
  req.label   = label ;
  req.in_pri  = in_pri ;
  req.out_pri = out_pri ;
  
  _requests[in].push_front( req ) ;

}

void SeparableAllocator::RemoveRequest( int in, int out, int label ) {
  // Method not implemented yet
  assert( false ) ;

}

void SeparableAllocator::PrintRequests( ) const {

  bool header_done = false ;

  for ( int input = 0 ; input < _inputs ; input++ ) {
    if ( _requests[input].empty() )
      continue ;
    
    if ( !header_done ) {
      cout << _fullname << endl ;
      header_done = true ;
    }

    list<sRequest>::const_iterator it  = _requests[input].begin() ;
    list<sRequest>::const_iterator end = _requests[input].end() ;

    cout << "  Input Port" << input << ":= " ;
    while ( it != end ) {
      const sRequest& req = *it ;
      cout << "(vc:" << req.label << "->" << req.port << "|" << req.in_pri << ") " ;
      it++ ;
    }
    cout << endl ;
  }

}
