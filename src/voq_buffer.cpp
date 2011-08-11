#include <sstream>
#include "globals.hpp"
#include "booksim.hpp"
#include "voq_buffer.hpp"
#include "reservation.hpp"
VOQ_Buffer::VOQ_Buffer(const Configuration& config, int outputs,
		       Module *parent, const string& name ):Buffer( parent, name ){



  assert(config.GetInt("voq")==1);
  int num_vcs = config.GetInt( "num_vcs" );
  _vc_size = config.GetInt( "vc_buf_size" );
  _shared_size = config.GetInt( "shared_buf_size" );
  
  //voq currently only works for single vc
  if(gReservation){
    assert(num_vcs==RES_RESERVED_VCS+2);
  } else if(gECN){
    assert(num_vcs==ECN_RESERVED_VCS+1);
  } else {
    assert(num_vcs==1);
  }
  //add the voq vcs, 1 is already included in vcs
  num_vcs+=outputs-1;
  _vc.resize(num_vcs);

  for(int i = 0; i < num_vcs; ++i) {
    ostringstream vc_name;
    vc_name << "vc_voq_" << i;
    _vc[i] = new VC(config, outputs, this, vc_name.str( ) );
  }
}
