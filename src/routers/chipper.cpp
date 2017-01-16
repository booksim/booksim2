//	Ameya: Remove redundancies
#include <string>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <cassert>

#include "chipper.hpp"
#include "stats.hpp"
#include "globals.hpp"
#include "routefunc.hpp"

Chipper::Chipper( const Configuration& config,
		    Module *parent, const string & name, int id,
		    int inputs, int outputs )
  : Router( config,
	    parent, name,
	    id,
	    inputs, outputs )
{
	ostringstream module_name;
  
	// Routing
	string rf = "dor_next_mesh";
	map<string, cRoutingFunction>::iterator rf_iter = cRoutingFunctionMap.find(rf);
	if(rf_iter == cRoutingFunctionMap.end()) {
	  Error("Invalid routing function: " + rf);
	}
	_rf = rf_iter->second;

	assert(_inputs == _outputs);

	_input_buffer.resize(_inputs-1); 
	_output_buffer.resize(_outputs-1);

	_stage_1.resize(_inputs-1);
	_stage_2.resize(_inputs-1);

	_time = 0;
	_inject_slot = -1;
	last_channel = _inputs-1;
}

Chipper::~Chipper()
{
	for ( int i = 0; i < _inputs-1; ++i ) {
		while (!_input_buffer[i].empty())
		{
			(_input_buffer[i].begin()->second)->Free();
			_input_buffer[i].erase(_input_buffer[i].begin());
		}
	}
	
	for ( int i = 0; i < _inputs-1; ++i ) {
		while (!_stage_1[i].empty())
		{
			(_stage_1[i].begin()->second)->Free();
			_stage_1[i].erase(_stage_1[i].begin());
		}
	}
	
	for ( int i = 0; i < _inputs-1; ++i ) {
		while (!_stage_2[i].empty())
		{
			(_stage_2[i].begin()->second)->Free();
			_stage_2[i].erase(_stage_2[i].begin());
		}
	}
	
	for ( int o = 0; o < _outputs-1; ++o ) {
	  while (!_output_buffer[o].empty())
		{
			(_output_buffer[o].begin()->second)->Free();
			_output_buffer[o].erase(_output_buffer[o].begin());
		}
	}
}

void Chipper::AddInputChannel( FlitChannel *channel, CreditChannel * ignored)
{
	//	Ameya: credit channel ignored
	_input_channels.push_back( channel );
	channel->SetSink( this, _input_channels.size() - 1 ) ;
}

void Chipper::AddOutputChannel(FlitChannel * channel, CreditChannel * ignored)
{
	//	Ameya: credit channel ignored
	_output_channels.push_back( channel );
	_channel_faults.push_back( false );
	channel->SetSource( this, _output_channels.size() - 1 ) ;
}

void Chipper::Display( ostream & os ) const
{
	os << "Nothing to display" << endl;		//	Ameya: Just for sake of avoiding pure virual func
}

//	Ameya: Returns 1 if there is empty slot in _stage_1
int Chipper::GetInjectStatus()
{
	for ( int input = 0; input < _inputs - 1; ++input )
	{
		map<int,Flit*>::iterator f = _stage_1[input].find(GetSimTime());
		if(f == _stage_1[input].end())
		{
			// _inject_slot = input;
			return 1;
		}
	}
	// _inject_slot = -1;
	return 0;
}

// Ameya : Performs the function of reading flits from channel into input buffer
void Chipper::ReadInputs()
{
	int time = GetSimTime();
	Flit *f;
	for ( int input = 0; input < _inputs - 1; ++input )		//	Avoid _inject channel
	{
		f = _input_channels[input]->Receive();

	    if ( f )
	    {
	    	if(f->watch) {
				*gWatchOut << GetSimTime() << " | "
					<< "node" << GetID() << " | "
					<< "Flit " << f->id
					<< " arrived at input " << input << " | "
					<< "destination " << f->dest
					<< "." << endl;
			}
	    	_input_buffer[input].insert( pair<int, Flit *>(time, f) );
	    }
  	}	
}

// HH : Performs the function of sending flits from output buffer into channel
void Chipper::WriteOutputs()
{
	_SendFlits( ); //HH : Sending flits from output buffer into input channel, no credits sent, as in event_router
}

// HH Definition of _Sendflits in Chipper class same as that in class event_router
void Chipper::_SendFlits( )
{
	int time = GetSimTime();
	map<int,Flit*>::iterator f;
  	for ( int output = 0; output < _outputs - 1; ++output )		//	Avoid _eject channel
  	{
  		map<int,Flit*> & buffer_timed = _output_buffer[output];
  		f = buffer_timed.find( time );
  		if(f != buffer_timed.end() )
  		{
  			if((f->second)->watch) {
				*gWatchOut << GetSimTime() << " | "
					<< "node" << GetID() << " | "
					<< "Flit " << (f->second)->id
					<< " sent from output " << output << " | "
					<< "destination " << (f->second)->dest
					<< "." << endl;
			}
  			_output_channels[output]->Send( f->second );
  			buffer_timed.erase(f);
  		}
    }
 }

void Chipper::_InternalStep( )
{
	_time = GetSimTime();

	_EjectFlits();

	_input_to_stage1();

	_stage1_to_stage2();

	Permute();

	_stage2_to_output();

	CheckSanity();
}

// Added by HH
void Chipper::_EjectFlits(){
	Flit * received_flits[_inputs-1]; 	// To keep track of all the flits that need to be ejected
	Flit *f;							//	Ameya: syntax edits
	map<int, Flit *>::iterator it;
	int flit_to_eject = -1;
	int golden_cnt = 0; 
  	for ( int input = 0; input < _inputs-1; ++input )
  	{
  		it = _input_buffer[input].find(_time);
  		if(it == _input_buffer[input].end())
  		{
  			received_flits[input] = NULL;
  			continue; 
  		}
  		f = it->second;
	    if ( f->dest ==  GetID() )
	    {
	    	if(f->watch) {
				*gWatchOut << GetSimTime() << " | "
					<< "node" << GetID() << " | "
					<< "Flit " << f->id
					<< " waiting for eject at " << f->dest
					<< " with priority " << f->pri
					<< " and golden status " << f->golden
					<< "." << endl;
			}
			received_flits[input] = f;
			// Check for golden status and golden tie
			if(f->golden == 1 && !golden_cnt)
			{
				flit_to_eject = input; 
				golden_cnt++;
			}
			else if(f->golden == 1)
			{
				if(received_flits[flit_to_eject]->pri < f->pri) // Resolve golden tie based on older flit
				{
					flit_to_eject = input;
				}
				else
				{
					if(f->watch) {
						*gWatchOut << GetSimTime() << " | "
							<< "node" << GetID() << " | "
							<< "Flit " << f->id
							<< " trying to eject at " << f->dest
							<< " lost golden tie at time " << _time
							<< "." << endl;
					}
				}
		  	}
	    }
	    else
	    {
	    	received_flits[input] = NULL; // Flit not at destination
	    }
	}

    if(flit_to_eject == -1)
    {
    	int oldest_flit_index = -1;
    	int high_pri = -1;
	    for( int input = 0; input < _inputs-1; ++input )
	    {
	    	// Iterating through the array of stored flits that need to be ejected
	    	// to find the oldest flit and consequently retiring that flit	 
	    	if(received_flits[input] != NULL && received_flits[input]->pri > high_pri)
	    	{
	    		if(oldest_flit_index != -1)
	    		{
	    			if(received_flits[input]->watch) {
						*gWatchOut << GetSimTime() << " | "
							<< "node" << GetID() << " | "
							<< "Flit " << received_flits[input]->id
							<< " waiting for eject at " << received_flits[input]->dest
							<< " with priority " << received_flits[input]->pri 
							<< " beat flit " << received_flits[oldest_flit_index]->id
							<< " with priority " << high_pri
							<< "." << endl;
					}
	    		}
	    		high_pri = received_flits[input]->pri;
	    		oldest_flit_index = input; 
	    	}
	    }
    	if(high_pri > -1)
    	{
    		flit_to_eject = oldest_flit_index;
    		if(received_flits[flit_to_eject]->watch) {
				*gWatchOut << GetSimTime() << " | "
					<< "node" << GetID() << " | "
					<< "Flit " << received_flits[flit_to_eject]->id
					<< " accepted for eject at " << received_flits[flit_to_eject]->dest
					<< " | golden status " << received_flits[flit_to_eject]->golden
					<< "." << endl;
			}
    		_output_channels[last_channel]->Send(received_flits[flit_to_eject]);
    		_input_buffer[flit_to_eject].erase(_input_buffer[flit_to_eject].find(_time));
    		// HH : Receive flit with oldest_flit_index -> Need to apply the function that handles retire flit
    	}
	}
	else
	{
		//	Ameya: Eject golden flit
		if(f->watch) {
			*gWatchOut << GetSimTime() << " | "
				<< "node" << GetID() << " | "
				<< "Flit " << f->id
				<< " accepted for eject at " << f->dest
				<< " | golden status " << f->golden
				<< "." << endl;
		}
		_output_channels[last_channel]->Send(received_flits[flit_to_eject]);
		_input_buffer[flit_to_eject].erase(_input_buffer[flit_to_eject].find(_time));
	}	
}

void Chipper::_input_to_stage1()
{
    map<int, Flit*>::iterator it;
    for ( int input = 0; input < _inputs-1; ++input ){
        it =_input_buffer[input].find(_time);
        if(it == _input_buffer[input].end())
        	continue;
        if((it->second)->watch) {
			*gWatchOut << GetSimTime() << " | "
				<< "node" << GetID() << " | "
				<< "Flit " << (it->second)->id
				<< " headed for " << (it->second)->dest
				<< " written from _input_buffer to _stage_1 in slot "
				<< input
				<< "." << endl;
		}
        _stage_1[input].insert(make_pair(it->first+1, it->second));
        _input_buffer[input].erase(it);
    }
}

void Chipper::_stage1_to_stage2()
{
	map<int, Flit*>::iterator it;
	for ( int input = 0; input < _inputs-1; ++input ){      // Nandan: Adding the delay associated with the pipe
		it = _stage_1[input].find(_time);
		if(it == _stage_1[input].end())
		{
			if(_inject_slot == -1)
				_inject_slot = input;
			continue;
		}
		if((it->second)->watch) {
			*gWatchOut << GetSimTime() << " | "
				<< "node" << GetID() << " | "
				<< "Flit " << (it->second)->id
				<< " headed for " << (it->second)->dest
				<< " written from _stage_1 to _stage_2 in slot "
				<< input
				<< "." << endl;
		}
		_stage_2[input].insert(make_pair(it->first+1, it->second));
		_stage_1[input].erase(it);
    }

    if(_inject_slot > -1)
    {
    	assert(_inject_slot < _inputs - 1);
    	Flit *f = _input_channels[last_channel]->Receive();
    	if(f)
    	{
    		if(f->watch) {
				*gWatchOut << GetSimTime() << " | "
							<< "router" << GetID() << " | "
							<< "Receiving flit " << f->id
							<< " at time " << _time
							<< " with priority " << f->pri
							<< " and golden status " << f->golden
							<< "." << endl;
			}
    		_stage_2[_inject_slot].insert(make_pair(_time+1, f));
    		_inject_slot = -1;
    	}
    }
}

void Chipper::_stage2_to_output()
{
	map<int, Flit*>::iterator it;
    for ( int input = 0; input < _inputs-1; ++input ){
        it = _stage_2[input].find(_time);
        if(it == _stage_2[input].end())
        	continue;
        if((it->second)->watch) {
			*gWatchOut << GetSimTime() << " | "
				<< "node" << GetID() << " | "
				<< "Flit " << (it->second)->id
				<< " headed for " << (it->second)->dest
				<< " written from _stage_2 to _output_buffer in slot "
				<< input
				<< "." << endl;
		}
        _output_buffer[input].insert(make_pair(it->first+1, it->second));
        _stage_2[input].erase(it);
    }
}

//	Nandan:	Routing stage of CHIPPER
void Chipper::Permute()
{
	Partial_Permute(2,0,1);
	Partial_Permute(3,1,1);
	Partial_Permute(3,2,2);
	Partial_Permute(1,0,0);
	for(int i=0;i < _inputs-1;++i)
	{
		Flit *f;
		map<int, Flit*>::iterator it;
		it = _stage_2[i].find(_time);
		if(it == _stage_2[i].end())
		{
			continue;
		}
		else
		{
			f = it->second;
		}
		if(!(f->watch))
			continue;
		if(_rf(GetID(), f->dest, true) == i)
		{
			*gWatchOut << GetSimTime() << " | "
				<< "node" << GetID() << " | "
				<< "Flit " << f->id
				<< " headed for " << f->dest
				<< " optimally routed"
				<< " to node "<< (_output_channels[i]->GetSink())->GetID()
				<< "." << endl;
		}
		else
		{
			*gWatchOut << GetSimTime() << " | "
				<< "node" << GetID() << " | "
				<< "Flit " << f->id
				<< " headed for " << f->dest
				<< " sub-optimally routed"
				<< " to node "<< (_output_channels[i]->GetSink())->GetID()
				<< "." << endl;
		}
	}
}

void Chipper::Partial_Permute(int dir1, int dir2, int perm_num)
{
	Flit *f1;
	Flit *f2;
	map<int, Flit*>::iterator it1,it2;
	it1 = _stage_2[dir1].find(_time);
	if(it1 == _stage_2[dir1].end())
	{
		f1 = NULL;
	}
	else
	{
		f1 = it1->second;
	}
	it2 = _stage_2[dir2].find(_time);
	if(it2 == _stage_2[dir2].end())
	{
		f2 = NULL;
	}
	else
	{
		f2 = it2->second;
	}
	if((f1 == NULL)&&(f2 == NULL))
		return;
	if(f1 == NULL)
	{
		if(_rf(GetID(), f2->dest, true) > perm_num)
		{
			_stage_2[dir2].erase(it2);
			_stage_2[dir1].insert(pair<int, Flit *>(_time, f2) );
		}
		return;
	}
	if(f2 == NULL)
	{
		if(_rf(GetID(), f1->dest, true) <= perm_num)
		{
			_stage_2[dir1].erase(it1);
			_stage_2[dir2].insert(pair<int, Flit *>(_time, f1) );
		}
		return;
	}
	if((f1->golden == 1)&&(f2->golden == 1))
	{
		if(f1->pri >= f2->pri)
		{
			if(_rf(GetID(), f1->dest, true) <= perm_num)
			{
				_stage_2[dir1].erase(it1);
				_stage_2[dir2].erase(it2);
				_stage_2[dir2].insert( pair<int, Flit *>(_time, f1) );
				_stage_2[dir1].insert( pair<int, Flit *>(_time, f2) );
			}
		}
		else
		{
			if(_rf(GetID(), f2->dest, true) > perm_num)
			{
				_stage_2[dir1].erase(it1);
				_stage_2[dir2].erase(it2);
				_stage_2[dir2].insert( pair<int, Flit *>(_time, f1) );
				_stage_2[dir1].insert( pair<int, Flit *>(_time, f2) );
			}
		}
	}
	else if((f1->golden == 1))
	{
		if(_rf(GetID(), f1->dest, true) <= perm_num)
		{
			_stage_2[dir1].erase(it1);
			_stage_2[dir2].erase(it2);
			_stage_2[dir2].insert( pair<int, Flit *>(_time, f1) );
			_stage_2[dir1].insert( pair<int, Flit *>(_time, f2) );
		}
	}
	else if((f2->golden == 1))
	{
		if(_rf(GetID(), f2->dest, true) > perm_num)
		{
			_stage_2[dir1].erase(it1);
			_stage_2[dir2].erase(it2);
			_stage_2[dir2].insert( pair<int, Flit *>(_time, f1) );
			_stage_2[dir1].insert( pair<int, Flit *>(_time, f2) );
		}
	}
	else
	{
		if(f1->pri >= f2->pri)
		{
			if(_rf(GetID(), f1->dest, true) <= perm_num)
			{
				_stage_2[dir1].erase(it1);
				_stage_2[dir2].erase(it2);
				_stage_2[dir2].insert( pair<int, Flit *>(_time, f1) );
				_stage_2[dir1].insert( pair<int, Flit *>(_time, f2) );
			}
		}
		else
		{
			if(_rf(GetID(), f2->dest, true) > perm_num)
			{
				_stage_2[dir1].erase(it1);
				_stage_2[dir2].erase(it2);
				_stage_2[dir2].insert( pair<int, Flit *>(_time, f1) );
				_stage_2[dir1].insert( pair<int, Flit *>(_time, f2) );
			}
		}
	}	
}

void Chipper::CheckSanity()
{
	for ( int i = 0; i < _inputs-1; ++i ) {
		if(_input_buffer[i].size() > 2)
		{
			ostringstream err;
            err << "Flit pile up at input buffer of router: " << GetID();
            Error( err.str( ) );
		}
	}

	for ( int i = 0; i < _inputs-1; ++i ) {
		if(_stage_1[i].size() > 2)
		{
			ostringstream err;
            err << "Flit pile up at _stage_1 of router: " << GetID();
            Error( err.str( ) );
		}
	}

	for ( int i = 0; i < _inputs-1; ++i ) {
		if(_stage_2[i].size() > 2)
		{
			ostringstream err;
            err << "Flit pile up at _stage_2 of router: " << GetID();
            Error( err.str( ) );
		}
	}

	for ( int o = 0; o < _outputs-1; ++o ) {
		if(_output_buffer[o].size() > 2)
		{
			ostringstream err;
            err << "Flit pile up at output buffer of router: " << GetID();
            Error( err.str( ) );
		}
	}
}