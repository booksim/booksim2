//	Ameya: Remove redundancies
#include <string>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <cassert>

#include "chipper.hpp"
#include "stats.hpp"
#include "globals.hpp"

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

	_input_buffer.resize(_inputs); 
	_output_buffer.resize(_outputs);

	_stage_1.resize(_inputs);
	_stage_2.resize(_inputs);
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
		Flit *f;

	  for ( int input = 0; input < _inputs; ++input ) { 
	   // f = _input_buffer[input].pop;

	    if ( f->dest ==  GetID() ) {
	  	  ;
	    }
	    else if {

	    }
	    else
	  }
	}

	void Chipper::_Buffer_to_stage1()
{
    map<int,  Flit *> intermediate_1; 
    int input = 0;
    for ( int input = 0; input < _inputs; ++input ){
        intermediate = _input_buffer[input];
        for( map<int, Flit*>::iterator it = intermediate_1.begin() ; it = intermediate_1.end() ; ++it ){      //Nandan: Adding the delay associated with the pipe
             if(pair_time == it->first){
             it->first = it->first + delay_pipe_1;
             }
        }
        stage_1[input] = _input_buffer[input];
    }
}

void Chipper::_stage1_to_stage2()
{ 
     msp<int, Flit *> intermediate_2;
     int input = 0;
     for ( int input = 0; input < _inputs; ++input ){      // Nandan: Adding the delay associated with the pipe
         intermediate = _stage_1[input];
         for( map<int, Flit*>::iterator it = intermediate_2.begin() ; it = intermediate_2.end() ; ++it ){
              if(pair_time == it->first){
             it->first = it->first + delay_pipe_2;
             }
         }
         stage_2[input] = _stage_1[input];
    }
}
// Added by HH : scheme for determining Golden_packet as in chipper paper(not exactly)
// Returning the packet id that is to be made golden by just dividing time_elapsed by L instead of 
// keeping track of all the packets in the network. (I don't know if a maximum number of packets is available)
// Instead of iterating over all possible packet ids called "transaction ids" from all nodes, just iterate over 
// packet ids which uniquely identifies each packet
int Chipper::Golden_Packet(int L) // Parameter L is obtained from before from another function
{
	int time_elapsed = GetSimTime();
	int epoch_no = time_elapsed/L; // L is the golden epoch, needs to be defined as the upper 
	//bound of the maximum time taken by a golden packet to reach its destination
	return epoch_no; 
}
