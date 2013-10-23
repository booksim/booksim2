#include <sstream>
#include "globals.hpp"
#include "booksim.hpp"
#include "voq_buffer.hpp"
#include "reservation.hpp"
#include "routefunc.hpp"

VOQ_Buffer::VOQ_Buffer(const Configuration& config, int outputs,
		       Module *parent, const string& name ):Buffer( parent, name ){
  assert(config.GetInt("voq")==1);
  int data_vcs         = config.GetInt( "num_vcs" );
  int special_vcs;

  if(gReservation){
    if(config.GetInt("reservation_spec_voq")==1){
      special_vcs =  gSpecVCStart; //spec gets included in data vc
    } else {
      special_vcs = gNSpecVCStart;
    }
   
  } else if(gECN){
    special_vcs=gNSpecVCStart;
  } else {
    special_vcs = 0;
  }

  data_vcs-=special_vcs;
  int num_vcs = special_vcs + outputs*data_vcs;

  _vc_size = config.GetInt( "vc_buf_size" );
  _spec_vc_size = _vc_size;
  _shared_size = config.GetInt( "shared_buf_size" );
  
  _vc.resize(num_vcs);
  
  for(int i = 0; i < num_vcs; ++i) {
    if(gReservation){
      {
	_vc[i] = new VC(config, outputs, this,"" );
      }
    }else {
      _vc[i] = new VC(config, outputs, this,"" );
    }
  }
}
