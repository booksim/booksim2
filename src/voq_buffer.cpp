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
  int ctrl_vcs;
  int special_vcs;

  if(gReservation){
    ctrl_vcs = RES_RESERVED_VCS+RES_RESERVED_VCS*gAuxVCs;
    if(config.GetInt("reservation_spec_voq")==1){
      special_vcs =  ctrl_vcs; //spec gets included in data vc
    } else {
      special_vcs = ctrl_vcs + 1 + gAuxVCs + gAdaptVCs;
    }
   
  } else if(gECN){
    ctrl_vcs=ECN_RESERVED_VCS+gAuxVCs;;
    special_vcs=ctrl_vcs;
  } else {
    ctrl_vcs= 0;
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
