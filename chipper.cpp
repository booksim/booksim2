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

int retire_index = 4; // HH : For mesh, ejection is through _output_channels[4];

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

	string rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
	map<string, tRoutingFunction>::iterator rf_iter = gRoutingFunctionMap.find(rf);
	if(rf_iter == gRoutingFunctionMap.end()) {
	  Error("Invalid routing function: " + rf);
	}
	_rf = rf_iter->second;

	assert(_inputs == _outputs);

	_input_buffer.resize(_inputs-1); 
	_output_buffer.resize(_outputs-1);

	_stage_1.resize(_inputs-1);
	_stage_2.resize(_inputs-1);
}

Chipper::~Chipper()
{
	for ( int i = 0; i < _inputs; ++i ) {
		while (!_input_buffer[i].empty())
		{
			(_input_buffer[i].begin()->second)->Free();
		  _input_buffer[i].erase(_input_buffer[i].begin());
		}
	}

	for ( int i = 0; i < _inputs; ++i ) {
		while (!_stage_1[i].empty())
		{
			(_stage_1[i].begin()->second)->Free();
		  _stage_1.erase(_stage_1.begin());
		}
	}

	for ( int i = 0; i < _inputs; ++i ) {
		while (!_stage_2[i].empty())
		{
			(_stage_2[i].begin()->second)->Free();
		  _stage_2.erase(_stage_2.begin());
		}
	}

	for ( int o = 0; o < _outputs; ++o ) {
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

void Chipper::ReadInputs() // HH : Performs the function of reading flits from channel into input buffer
{
	//	HH : Receiving flits from channel into input buffer, no credits received, as in event_router
}

// HH Definition of _ReceiveFlits: inserts flit into buffer from channel corresponding to current sim time
int Chipper::_IncomingFlits( )
{
  Flit *f;
  map< int,Flit* > buffer_timed;
  receive_time = GetSimTime();
  for ( int input = 0; input < _inputs; ++input ) { 
    f = _input_channels[input]->Receive(); 
		if ( f ) {
     		_input_buffer[input].insert( pair(receive_time,f) );
    }
  }
}

void Chipper::WriteOutputs() // HH : Performs the function of sending flits from output buffer into channel
{
	_SendFlits( ); //HH : Sending flits from output buffer into input channel, no credits sent, as in event_router
}

// HH Definition of _Sendflits in Chipper class same as that in class event_router
void Chipper::_SendFlits( int pair_time)
{
	map<int,Flit*> buffer_timed;
	Flit *f;
  for ( int output = 0; output < _outputs; ++output ) {
    //if ( !_output_buffer[output].empty( ) ) {
      //Flit *f = _output_buffer[output].front( );
      //_output_buffer[output].pop( );
      //_output_channels[output]->Send( f );
  		buffer_timed = _output_buffer.back;
  		f = buffer_timed.find( pair_time);
  		if(f){
  			_output_channels[output]->Send( f );
  		}
    }
 }

void Chipper::_InternalStep( )
{
  // Receive incoming flits
  	 int pair_time = _IncomingFlits( );
  	 //HH : Need to make the time the flits are received global in order to know when the input flits were 
// were paired into the map
  	 //Have to pass this onto subsequent functions to find the correct flit from each buffer
  	 // I am thinking pair_time should be updated at each stage with GetSimTime and then passed onto next stage
  // Eject flits which have reached their desitnation
	_EjectFlits();

	_Buffer_to_stage1();

	_stage1_to_stage2();



}

// Added by HH
	void _EjectFlits(){
		array Flit *f received_flits[_inputs]; // To keep track of all the flits that need to be ejected
		int flit_to_eject = -1;
		int golden_cnt = 0; 
	  for ( int input = 0; input < _inputs-1; ++input ) {      
	  	// One input channel is for the router
	   // f = _input_buffer[input].pop;

	    if ( f->dest ==  GetID() ) {
	  	  received_flits[input] = f;
	  	  // Check for golden status and golden tie
	  	  if(f->golden == 1 && !golden_cnt)
	  	  {
	  	  	flit_to_eject = input; 
	  	  	golden_cnt++;
	  	  }
	  	  else if(f->golden == 1)
	  	  {
	  	  	if(received_flits[flit_to_eject]->pri > f->pri) // Resolve golden tie based on older flit
	  	  	{
	  	  		flit_to_eject = input;
	  	  		break;
	  	  	}
	  	  }
	    }
	    else{
	    	received_flits[input] = NULL; // Flit not at destination
	    }
	    if(flit_to_eject == -1){
		    for( int input = 0; input < _inputs-1; ++input ){
		    	// Iterating through the array of stored flits that need to be ejected
		    	// to find the oldest flit and consequently retiring that flit
		    	int oldest_flit_index;
		    	int high_pri = -1; 
		    	if(received_flits[input] != NULL && received_flits[input]->pri > high_pri)
		    	{
		    		high_pri = received_flits[input]->pri;
		    		oldest_flit_index = input; 
		    	}
		    	if(high_pri > -1)
		    	{
		    		flit_to_eject = oldest_flit_index;
		    		_output_channels[retire_index] = received_flits[flit_to_eject];
		    		// HH : Receive flit with oldest_flit_index -> Need to apply the function that handles retire flit
		    	}
		    }
		}
	}


	void Chipper::_Buffer_to_stage1()
{
    // map<int,  Flit *> intermediate_1; 
    int input = 0;
    map<int, Flit*>::iterator it;
    for ( int input = 0; input < _inputs; ++input ){
        // intermediate = _input_buffer[input];
        // for( map<int, Flit*>::iterator it = intermediate_1.begin() ; it = intermediate_1.end() ; ++it ){      //Nandan: Adding the delay associated with the pipe
        //      if(pair_time == it->first){
        //      it->first = it->first + 1;
        //      }
        // }
        it =_input_buffer[input].find(GetSimTime());
        if(it == _input_buffer[input].end())
        	continue;
        _stage_1[input].insert(make_pair(it->first+1, it->second));
        _input_buffer[input].erase(it);
    }
}

void Chipper::_stage1_to_stage2()
{ 
     // msp<int, Flit *> intermediate_2;
     int input = 0;
     for ( int input = 0; input < _inputs; ++input ){      // Nandan: Adding the delay associated with the pipe
         // intermediate = _stage_1[input];
         // for( map<int, Flit*>::iterator it = intermediate_2.begin() ; it = intermediate_2.end() ; ++it ){
              // if(pair_time == it->first){
             // it->first = it->first + 1;
             // }
         // }
         it = _stage_1[input].find(GetSimTime());
         if(it == _stage_1[input].end())
         	continue;
         _stage_2[input].insert(make_pair(it->first+1, it->second));
         _stage_1[input].erase(it);
    }
}

void Chipper::Permute()
{
	// int router_number = GetID();
	// int west = _LeftNode(router_number, 0);
	// int east = _RightNode(router_number, 0);
	// int north = _LeftNode(router_number, 1);
	// int south = _RightNode(router_number, 1);
	// vector<int> direction;
	// vector<int> priority;
	// vector<bool> golden;
	// direction.resize(_inputs-1);
	// priority.resize(_inputs-1);
	// for(input = 0; input < _inputs; ++input)
	// {

		// if(golden[input] == true)
	Partial_Permute(2,0,1);
	Partial_Permute(3,1,1);
	Partial_Permute(3,2,2);
	Partial_Permute(1,0,0);
	// }
}

void Chipper::Partial_Permute(int dir1, int dir2, int perm_num)
{
	Flit *f1;
	Flit *f2;
	map<int, Flit*> iterator it1,it2;
	it1 = _stage_2[dir1].find(GetSimTime());
	it2 = _stage_2[dir2].find(GetSimTime());
	*f1 = it1->second;
	*f2 = it2->second;
	if((f1->golden == 1)&&(f2->golden == 1))
	{
		if(f1->pri >= f2->pri)
		{
			if(_rf(GetID(), f1->dest, true) > perm_num)
			{

			}
			else
			{
				_stage_2[dir2].make_pair(it1->first, it1->second);
				_stage_2[dir1].make_pair(it2->first, it2->second);
				_stage_2[dir1].erase(it1);
				_stage_2[dir2].erase(it2);
			}
		}
		else
		{
			if(_rf(GetID(), f2->dest, true) > perm_num)
			{
				_stage_2[dir2].make_pair(it1->first, it1->second);
				_stage_2[dir1].make_pair(it2->first, it2->second);
				_stage_2[dir1].erase(it1);
				_stage_2[dir2].erase(it2);
			}
			else
			{
			
			}
		}
	}
	else if((f1->golden == 1))
	{
		if(_rf(GetID(), f1->dest, true) > perm_num)
		{
		
		}
		else
		{
			_stage_2[dir2].make_pair(it1->first, it1->second);
			_stage_2[dir1].make_pair(it2->first, it2->second);
			_stage_2[dir1].erase(it1);
			_stage_2[dir2].erase(it2);
		}
	}
	else if((f2->golden == 1))
	{
		if(_rf(GetID(), f2->dest, true) > perm_num)
		{
			_stage_2[dir2].make_pair(it1->first, it1->second);
			_stage_2[dir1].make_pair(it2->first, it2->second);
			_stage_2[dir1].erase(it1);
			_stage_2[dir2].erase(it2);
		}
		else
		{
			
		}
	}
	else
	{
		if(f1->pri >= f2->pri)
		{
			if(_rf(GetID(), f1->dest, true) > perm_num)
			{
			
			}
			else
			{
				_stage_2[dir2].make_pair(it1->first, it1->second);
				_stage_2[dir1].make_pair(it2->first, it2->second);
				_stage_2[dir1].erase(it1);
				_stage_2[dir2].erase(it2);
			}
		}
		else
		{
			if(_rf(GetID(), f2->dest, true) > perm_num)
			{
				_stage_2[dir2].make_pair(it1->first, it1->second);
				_stage_2[dir1].make_pair(it2->first, it2->second);
				_stage_2[dir1].erase(it1);
				_stage_2[dir2].erase(it2);
			}
			else
			{
			
			}
		}

	}	
}
// Added by HH : scheme for determining Golden_packet as in chipper paper(not exactly)
// Returning the packet id that is to be made golden by just dividing time_elapsed by L instead of 
// keeping track of all the packets in the network. (I don't know if a maximum number of packets is available)
// Instead of iterating over all possible packet ids called "transaction ids" from all nodes, just iterate over 
// packet ids which uniquely identifies each packet

