// $Id$
/*wavefront.cpp
 *
 *The wave front allocator
 *
 */
#include "booksim.hpp"
#include <iostream>

#include "wavefront.hpp"
#include "random_utils.hpp"

Wavefront::Wavefront( Module *parent, const string& name,
		      int inputs, int outputs ) :
  DenseAllocator( parent, name, inputs, outputs ),
  _pri(0), _num_requests(0), _last_in(-1), _last_out(-1),
  _square((inputs > outputs) ? inputs : outputs)
{
}

void Wavefront::AddRequest( int in, int out, int label, 
			    int in_pri, int out_pri )
{
  // count unique requests
  sRequest req;
  bool overwrite = ReadRequest(req, in, out);
  if(!overwrite || (req.in_pri < in_pri)) {
    _num_requests++;
    _last_in = in;
    _last_out = out;
  }
  DenseAllocator::AddRequest(in, out, label, in_pri, out_pri);
}

void Wavefront::Allocate( )
{
  int input;
  int output;

  // Clear matching

  for ( int i = 0; i < _inputs; ++i ) {
    _inmatch[i] = -1;
  }
  for ( int j = 0; j < _outputs; ++j ) {
    _outmatch[j] = -1;
  }

  if(_num_requests == 0)

    // bypass allocator completely if there were no requests
    return;
  
  if(_num_requests == 1) {

    // if we only had a single request, we can immediately grant it
    _inmatch[_last_in] = _last_out;
    _outmatch[_last_out] = _last_in;
    
  } else {

    // otherwise we have to loop through the diagonals of request matrix
    
    /*
    for ( int p = 0; p < _square; ++p ) {
      output = ( _pri + p ) % _square;
      
      // Step through the current diagonal
      for ( input = 0; input < _inputs; ++input ) {
	if ( ( output < _outputs ) && 
	     ( _inmatch[input] == -1 ) && 
	     ( _outmatch[output] == -1 ) &&
	     ( _request[input][output].label != -1 ) ) {
	  // Grant!
	  _inmatch[input] = output;
	  _outmatch[output] = input;
	}
	
	output = ( output + 1 ) % _square;
      }
    }
    */
    
    // dub: in PPIN, the wavefront allocator actually uses the upward diagonals,
    // not the downward ones
    for ( int p = 0; p < _square; ++p ) {
      for ( int q = 0; q < _square; ++q ) {
	input = (_pri + p - q + _square) % _square;
	output = q;
	
	if ( ( input < _inputs ) && ( output < _outputs ) && 
	     ( _inmatch[input] == -1 ) && ( _outmatch[output] == -1 ) &&
	     ( _request[input][output].label != -1 ) ) {
	  // Grant!
	  _inmatch[input] = output;
	  _outmatch[output] = input;
	}
      }
    }
  }
  
  _num_requests = 0;
  _last_in = -1;
  _last_out = -1;
  
  // Round-robin the priority diagonal
  _pri = ( _pri + 1 ) % _square;
}


