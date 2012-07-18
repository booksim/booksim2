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
  //voq currently only works for single vc
  if(gReservation){
    ctrl_vcs = RES_RESERVED_VCS+2*gAuxVCs;
    special_vcs = ctrl_vcs + 1 + gAuxVCs + gAdaptVCs;
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
  _spec_vc_size = config.GetInt("reservation_spec_vc_size");
  _spec_vc_size = (_spec_vc_size==0)?(_vc_size):(_spec_vc_size);
  _shared_size = config.GetInt( "shared_buf_size" );
  
 
  
  //this option is removed
  assert(config.GetInt("reservation_spec_voq") !=1);
  _vc.resize(num_vcs);

  for(int i = 0; i < num_vcs; ++i) {
    if(gReservation){
      /* if(config.GetInt("reservation_spec_voq") ==1){
	 if(i>=RES_RESERVED_VCS && i<RES_RESERVED_VCS+outputs){
	 _vc[i] = new VC(config, outputs, this,"",true);
	 } else { 
	 _vc[i] = new VC(config, outputs, this,"" );
	 }
	 } else */
      {
	//spec buffers is the sandwich
	if(i>=ctrl_vcs && i<special_vcs){
	  _vc[i] = new VC(config, outputs, this,"",true);
	} else { 
	  _vc[i] = new VC(config, outputs, this,"" );
	}
      }
    }else {
      _vc[i] = new VC(config, outputs, this,"" );
    }

  }
}
