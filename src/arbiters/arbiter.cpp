// ----------------------------------------------------------------------
//
//  Arbiter: Base class for Matrix and Round Robin Arbiter
//
// ----------------------------------------------------------------------

#include "arbiter.hpp"
#include "roundrobin_arb.hpp"
#include "matrix_arb.hpp"

#include <assert.h>

using namespace std ;

Arbiter::Arbiter( Module *parent, const string &name, int size )
  : Module( parent, name ),
    _input_size(size), _request(0), _num_reqs(0), _last_req(-1)
{
  _request = new entry_t[size];
  for ( int i = 0 ; i < size ; i++ ) 
    _request[i].valid = false ;
}

Arbiter::~Arbiter()
{
  if ( _request ) 
    delete[] _request ;
}

void Arbiter::AddRequest( int input, int id, int pri )
{
  assert( 0 <= input && input < _input_size ) ;
  if(!_request[input].valid || (_request[input].pri < pri)) {
    _last_req = input ;
    if(!_request[input].valid) {
      _num_reqs++ ;
      _request[input].valid = true ;
    }
    _request[input].id = id ;
    _request[input].pri = pri ;
  }
}

Arbiter *Arbiter::NewArbiter( Module *parent, const string& name,
			      const string &arb_type, int size)
{
  Arbiter *a = NULL;
  if(arb_type == "round_robin") {
    a = new RoundRobinArbiter( parent, name, size );
  } else if(arb_type == "matrix") {
    a = new MatrixArbiter( parent, name, size );
  } else assert(false);
  return a;
}
