#ifndef _CHIPPER_HPP_
#define _CHIPPER_HPP_

#include <string>
#include <queue>
#include <vector>

#include "module.hpp"
#include "router.hpp"
#include "routefunc.hpp"

class Chipper : public Router {
	//	Each stage buffer is maintained as a map of <arrival_time:Flit*> 
	//	but the length should not exceed 2 flit widths
	//	This is necessary to allow for code to depict
	//	segments working in parallel
	vector<map<int, Flit *> > _input_buffer;
	vector<map<int, Flit *> > _output_buffer;
	vector<map<int, Flit *> > _stage_1;
	vector<map<int, Flit *> > _stage_2;
	//	No need for extra flags showing occupancy as each buffer is timed

	int _time;
	int _inject_slot;
	int last_channel;

	cRoutingFunction   _rf;		//	Temporary (check necessity later) Ameya

  	virtual void _InternalStep();
  	void _IncomingFlits( );
  	void _SendFlits( );
  	void _EjectFlits();
  	void _input_to_stage1();
  	void _stage1_to_stage2();
  	void Permute();
  	void _stage2_to_output();
  	void Partial_Permute(int dir1, int dir2, int perm_num);
  	void CheckSanity();

public:
	Chipper(	const Configuration& config,
	       Module *parent, const string & name, int id,
	       int inputs, int outputs	);
	virtual ~Chipper();

	virtual void AddInputChannel( FlitChannel *channel, CreditChannel * ignored);
	virtual void AddOutputChannel(FlitChannel * channel, CreditChannel * ignored);

	int GetInjectStatus();
	virtual void ReadInputs();
	// virtual void Evaluate( );
	virtual void WriteOutputs();

	//	Ameya: Just for sake of avoiding pure virual func
	virtual int GetUsedCredit(int o) const {return 0;}
	virtual int GetBufferOccupancy(int i) const {return 0;}

	virtual vector<int> UsedCredits() const { return vector<int>(); }
	virtual vector<int> FreeCredits() const { return vector<int>(); }
	virtual vector<int> MaxCredits() const { return vector<int>(); }
	
	//	Ameya: Just for sake of avoiding pure virual func
	virtual void Display( ostream & os = cout ) const;	//	Recheck implementation
};

#endif
