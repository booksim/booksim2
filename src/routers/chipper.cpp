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

void Chipper::ReadInputs()
{
	cout << "Nothing to display" << endl;		//	Ameya: Just for sake of avoiding pure virual func
}

void Chipper::WriteOutputs()
{
	cout << "Nothing to display" << endl;		//	Ameya: Just for sake of avoiding pure virual func
}

void Chipper::_InternalStep()
{
	cout << "Nothing to display" << endl;		//	Ameya: Just for sake of avoiding pure virual func
}