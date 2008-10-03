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
					const string &alloc_type, int inputs,
					int outputs )
  : Allocator( config, parent, name, inputs, outputs )
{

  _in_req  = new list<sRequest> [inputs] ;
  _out_req = new list<sRequest> [outputs] ;

  int num_vcs = config.GetInt("num_vcs") ;

  _input_arb = new Arbiter*[inputs];
  _output_arb = new Arbiter*[outputs];
  _spec_input_arb = new Arbiter*[inputs];
  _spec_output_arb = new Arbiter*[outputs];

  if ( alloc_type == "matrix" ) {
    for (int i = 0; i < inputs; ++i) {
      _input_arb[i] = new MatrixArbiter;
      _input_arb[i]->Init(num_vcs);
      _spec_input_arb[i] = new MatrixArbiter;
      _spec_input_arb[i]->Init(num_vcs);
    }
    for (int i = 0; i < outputs; ++i) {
      _output_arb[i] = new MatrixArbiter;
      _output_arb[i]->Init(inputs);
      _spec_output_arb[i] = new MatrixArbiter;
      _spec_output_arb[i]->Init(inputs);
    }
  } else if ( alloc_type == "round_robin" ) {
    for (int i = 0; i < inputs; ++i) {
      _input_arb[i] = new RoundRobinArbiter;
      _input_arb[i]->Init(num_vcs);
      _spec_input_arb[i] = new RoundRobinArbiter;
      _spec_input_arb[i]->Init(num_vcs);
    }
    for (int i = 0; i < outputs; ++i) {
      _output_arb[i] = new RoundRobinArbiter;
      _output_arb[i]->Init(inputs);
      _spec_output_arb[i] = new RoundRobinArbiter;
      _spec_output_arb[i]->Init(inputs);
    }
  }

  Clear() ;
}

SeparableAllocator::~SeparableAllocator() {

  delete[] _in_req ;
  delete[] _out_req ;

  for (int i = 0; i < _inputs; ++i) {
    delete _input_arb[i];
    delete _spec_input_arb[i];
  }
  for (int i = 0; i < _outputs; ++i) {
    delete _output_arb[i];
    delete _spec_output_arb[i];
  }

  delete[] _input_arb ;
  delete[] _spec_input_arb ;
  delete[] _output_arb ;
  delete[] _spec_output_arb ; 
}

void SeparableAllocator::Clear() {

  for ( int i = 0 ; i < _inputs ; i++ ) 
    _in_req[i].clear() ;

  for ( int i = 0 ; i < _outputs ; i++ )
    _out_req[i].clear() ;
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

  list<sRequest>::const_iterator match = _in_req[in].begin() ;

  while ( match != _in_req[in].end() ) {
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
  
  _in_req[in].push_front( req ) ;
  _out_req[out].push_front( req ) ;

}

void SeparableAllocator::RemoveRequest( int in, int out, int label ) {
  // Method not implemented yet
  assert( false ) ;

}

void SeparableAllocator::PrintRequests( ) const {

  bool header_done = false ;

  for ( int input = 0 ; input < _inputs ; input++ ) {
    if ( _in_req[input].empty() )
      continue ;
    
    if ( !header_done ) {
      cout << _fullname << endl ;
      header_done = true ;
    }

    list<sRequest>::const_iterator it  = _in_req[input].begin() ;
    list<sRequest>::const_iterator end = _in_req[input].end() ;

    cout << "  Input Port" << input << ":= " ;
    while ( it != end ) {
      const sRequest& req = *it ;
      cout << "(vc:" << req.label << "->" << req.port << "|" << req.in_pri << ") " ;
      it++ ;
    }
    cout << endl ;
  }

}

void SeparableAllocator::Allocate() {

  _ClearMatching() ;

//  cout << "SeparableAllocator::Allocate()" << endl ;
//  PrintRequests() ;

  for ( int input = 0 ; input < _inputs ; input++ ) {
   
    // Add the requesting virtual channels to the input arbiters.
    // Speculative requests are sent to one arbiter while non-speculative
    // requests are sent to a second arbiter.
    list<sRequest>::const_iterator it  = _in_req[input].begin() ;
    list<sRequest>::const_iterator end = _in_req[input].end() ;
    while ( it != end ) {
      const sRequest& req = *it ;
      if ( req.label > -1 ) {
	if ( req.in_pri > 0 ) {
	  _input_arb[input]->AddRequest( req.label, req.port, req.in_pri ) ;
	} else {
	  _spec_input_arb[input]->AddRequest( req.label, req.port, req.in_pri ) ;
	}
      }
      it++ ;
    }

    // Execute the input arbiters and propagate the grants to the
    // output arbiters. A speculative request is promoted only when
    // there was no non-speculative request at the input arbiter to 
    // prevent a speculative request that might be displaced by a non-
    // speculative request at the input port from competing for an output
    int out, spec_out, pri, spec_pri ;
    int vc = _input_arb[input]->Arbitrate( &out, &pri ) ;
    int spec_vc = _spec_input_arb[input]->Arbitrate( &spec_out, &spec_pri ) ;

    if ( vc > -1 ) {
      _output_arb[out]->AddRequest( input, vc, pri ) ;
    } else if ( spec_vc > -1 ) {
      _spec_output_arb[spec_out]->AddRequest( input, spec_vc, spec_pri ) ;
    }
  }

  // Execute the output arbiters. Non-speculative requests are granted 
  // ahead of speculative requests for the output port
  for ( int output = 0 ; output < _outputs ; output++ ) {

    int vc, spec_vc, pri, spec_pri ;
    int input      = _output_arb[output]->Arbitrate( &vc, &pri ) ;
    int spec_input = _spec_output_arb[output]->Arbitrate( &spec_vc, &spec_pri ) ;
  
    if ( input > -1 ) {
      assert( pri > 0 && _inmatch[input] == -1 && _outmatch[output] == -1 ) ;
      _inmatch[input]   = output ;
      _outmatch[output] = input ;
      _input_arb[input]->UpdateState() ;
      _output_arb[output]->UpdateState() ;

    } else if ( spec_input > -1 ) {

      assert( _inmatch[spec_input] == -1 && _outmatch[output] == -1 ) ;

      if ( spec_pri > -1 ) {

	_inmatch[spec_input] = output ;
	_outmatch[output]    = spec_input ;
	_spec_input_arb[spec_input]->UpdateState() ;
	_spec_output_arb[output]->UpdateState() ;

      }
    }
  }
}
