#include <sstream>
#include "globals.hpp"
#include "booksim.hpp"
#include "voq_buffer.hpp"
#include "reservation.hpp"
#include "routefunc.hpp"

VOQ_Buffer::VOQ_Buffer(const Configuration& config, int outputs,
		       Module *parent, const string& name, int ctrl_vcs, int special_vcs, int data_vcs ):Buffer( parent, name ){
  assert(config.GetInt("voq")==1);
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
