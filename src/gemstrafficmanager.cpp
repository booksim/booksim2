// $Id: trafficmanager.cpp 2516 2010-09-01 23:09:06Z qtedq $

/*
  Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this list
  of conditions and the following disclaimer.
  Redistributions in binary form must reproduce the above copyright notice, this 
  list of conditions and the following disclaimer in the documentation and/or 
  other materials provided with the distribution.
  Neither the name of the Stanford University nor the names of its contributors 
  may be used to endorse or promote products derived from this software without 
  specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "booksim.hpp"
#include <sstream>
#include <math.h>
#include <fstream>
#include "gemstrafficmanager.hpp"
#include "random_utils.hpp"
#include "Message.h"
#include "NetworkMessage.h"
#include "Network.h"

bool InjectConsumer::trigger_wakeup = true;
GEMSTrafficManager::GEMSTrafficManager(  const Configuration &config, const vector<BSNetwork *> & net , int vcc)
  : TrafficManager(config, net)
{
  vc_classes = vcc;
  vc_ptrs = new int[ _sources ];
  memset(vc_ptrs,0, _sources *sizeof(int));
  flit_size = config.GetInt("channel_width");
  _network_time = 0;
}

void GEMSTrafficManager::DisplayStats(){
  int  total_phases  = 0;
  double cur_latency = _latency_stats[0]->Average( );
  int dmin;
  double min, avg;
  dmin = _ComputeStats( _accepted_flits, &avg, &min );
  double cur_accepted = avg;
  _time =  g_eventQueue_ptr->getTime();
  
  cout << "I think time is\t"<<_network_time<<endl;
  cout << "Real ruby time is\t"<< _time<<endl; 
  cout << "Minimum latency = " << _latency_stats[0]->Min( ) << endl;
  cout << "Average latency = " << cur_latency << endl;
  cout << "Maximum latency = " << _latency_stats[0]->Max( ) << endl;
  cout << "Average fragmentation = " << _frag_stats[0]->Average( ) << endl;
  cout << "Accepted packets = " << min << " at node " << dmin << " (avg nonzero = " << avg << ")" << endl;
  cout << "Packet count = "<< _latency_stats[0]->NumSamples( )<<endl;
  cout << "Hop average = "<<_hop_stats->Average( )<<endl;
  if(_stats_out) {

    *_stats_out << "%==============================================\n";
    *_stats_out <<"time = "<<_time<<";"<<endl;
    *_stats_out << "lat(" << total_phases + 1 << ") = " << cur_latency << ";" << endl
		<< "lat_hist(" << total_phases + 1 << ",:) = "
		<< *_latency_stats[0] << ";" << endl
		<< "frag_hist(" << total_phases + 1 << ",:) = "
		<< *_frag_stats[0] << ";" << endl
		<< "pair_sent(" << total_phases + 1 << ",:) = [ ";
    for(int i = 0; i < _sources; ++i) {
      for(int j = 0; j < _dests; ++j) {
	*_stats_out << _pair_latency[i*_dests+j]->NumSamples( ) << " ";
      }
    }
    *_stats_out << "];" << endl
		<< "pair_lat(" << total_phases + 1 << ",:) = [ ";
    for(int i = 0; i < _sources; ++i) {
      for(int j = 0; j < _dests; ++j) {
	*_stats_out << _pair_latency[i*_dests+j]->Average( ) << " ";
      }
    }
    *_stats_out << "];" << endl
		<< "pair_lat(" << total_phases + 1 << ",:) = [ ";
    for(int i = 0; i < _sources; ++i) {
      for(int j = 0; j < _dests; ++j) {
	*_stats_out << _pair_tlat[i*_dests+j]->Average( ) << " ";
      }
    }
    *_stats_out << "];" << endl
		<< "sent(" << total_phases + 1 << ",:) = [ ";
    for ( int d = 0; d < _dests; ++d ) {
      *_stats_out << _sent_flits[d]->Average( ) << " ";
    }
    *_stats_out << "];" << endl
		<< "accepted(" << total_phases + 1 << ",:) = [ ";
    for ( int d = 0; d < _dests; ++d ) {
      *_stats_out << _accepted_flits[d]->Average( ) << " ";
    }
    *_stats_out << "];" << endl;
  }
}

GEMSTrafficManager::~GEMSTrafficManager( )
{

}


Flit *GEMSTrafficManager::_NewFlit( )
{
  if(_flit_pool.empty()){
    Flit* ftemp = new Flit[  _sources ];
    for(int i = 0; i<  _sources ; i++){
      _flit_pool.push_back(&ftemp[i]);
    }
  }
  //the constructor should initialize everything
  Flit * f = _flit_pool.back();
  _flit_pool.pop_back();
  f->id    = _cur_id;
  _total_in_flight_flits[_cur_id] = f;
  f->watch = gWatchOut && (_flits_to_watch.count(_cur_id) > 0);
  ++_cur_id;
  return f;
}

void GEMSTrafficManager::_RetireFlit( Flit *f, int dest )
{

  //send to the output message buffer
  if(f){
    if(f->tail){
      //cout<<"retiring message at "<<dest<<"\t";
      //cout<<"global time :"<<g_eventQueue_ptr->getTime()<<" message time: "<<f->msg.ref()->getLastEnqueueTime()<<endl;
      //cout<<"queue available "<< (*output_buffer)[dest][f->gems_net]->areNSlotsAvailable(1)<<endl;
      (*output_buffer)[dest][f->gems_net]->enqueue(f->msg);
    }
  }
  TrafficManager::_RetireFlit(f, dest);
}

int GEMSTrafficManager::_IssuePacket( int source, int cl )
{
  //no need
}

void GEMSTrafficManager::_GeneratePacket( int source, int stype, 
					  int vcc, int time )
{



  MsgPtr msg_ptr = (*input_buffer)[source][vcc]->peekMsgPtr();
  MsgPtr unmodified_msg_ptr = *(msg_ptr.ref()); 

  NetworkMessage* net_msg_ptr = dynamic_cast<NetworkMessage*>(msg_ptr.ref());
  NetDest msg_destinations = net_msg_ptr->getInternalDestination();
  Vector<NodeID> a = msg_destinations.getAllDest();
  //cout<<"Node "<<source<<" count "<<msg_destinations.count()<<endl;
  //cout<<a<<endl;
  //bytes
  int size = (int) ceil((double) MessageSizeType_to_int(net_msg_ptr->getMessageSize())*8/flit_size ); 

  for(int dest_index = 0; dest_index<a.size(); dest_index++){
    Flit::FlitType packet_type = Flit::ANY_TYPE;
 
    int ttime = time;
    int packet_destination = a[dest_index];
    bool record = false;
    
    if ((packet_destination <0) || (packet_destination >= _dests)) {
      cerr << "Incorrect packet destination " << packet_destination
	   << " for stype " << packet_type
	   << "!" << endl;
      Error( "" );
    }

    if ( ( _sim_state == running ) ||
	 ( ( _sim_state == draining ) && ( time < _drain_time ) ) ) {
      record = true;
    }

    _sub_network = 0;
  
    bool watch  = gWatchOut && (_packets_to_watch.count(_cur_pid) > 0);
  
    if ( watch ) { 
      *gWatchOut << GetSimTime() << " | "
		 << "node" << source << " | "
		 << "Enqueuing packet " << _cur_pid
		 << " at time " << time
		 << "." << endl;
    }
  
    for ( int i = 0; i < size; ++i ) {
      Flit * f = _NewFlit( );
      f->pid = _cur_pid;
      f->watch |= watch;
      f->subnetwork = _sub_network;
      f->src    = source;
      f->time   = time;
      f->ttime  = ttime;
      f->record = record;
      f->gems_net = vcc;
  
      //      msg_ptr = *(unmodified_msg_ptr.ref()); 


      f->msg = *(msg_ptr.ref());
      //cout<<"msg age "<<f->msg.ref()->getLastEnqueueTime()<<endl;//" add "<<(void*)f->msg<<endl;

      if(record) {
	_measured_in_flight_flits[f->id] = f;
      }
    
      if(gTrace){
	cout<<"New Flit "<<f->src<<endl;
      }
      //f->type = packet_type;
      f->type = (Flit::FlitType)f->gems_net;
      if ( i == 0 ) { // Head flit
	f->head = true;
	//packets are only generated to nodes smaller or equal to limit
	f->dest = packet_destination;
	_total_in_flight_packets.insert(pair<int, Flit *>(f->pid, f));
	if(record) {
	  _measured_in_flight_packets.insert(pair<int, Flit *>(f->pid, f));
	}
      } else {
	f->head = false;
	f->dest = -1;
      }
      switch( _pri_type ) {
      case class_based:
	f->pri = 0;
	break;
      case age_based:
	f->pri = _replies_inherit_priority ? -ttime : -time;
	break;
      case sequence_based:
	f->pri = -_packets_sent[source];
	break;
      default:
	f->pri = 0;
      }

      if ( i == ( size - 1 ) ) { // Tail flit
	f->tail = true;
      } else {
	f->tail = false;
      }
    
      f->vc  = -1;

      if ( f->watch ) { 
	*gWatchOut << GetSimTime() << " | "
		   << "node" << source << " | "
		   << "Enqueuing flit " << f->id
		   << " (packet " << f->pid
		   << ") at time " << time
		   << "." << endl;
      }

      _partial_packets[source][0][_sub_network].push_back( f );
    }
    ++_cur_pid;
  }
}





void GEMSTrafficManager::GemsInject(){

  // Receive credits and inject new traffic
  for ( int input = 0; input < _limit; ++input ) {
    for (int i = 0; i < _duplicate_networks; ++i) {
      Credit * cred = _net[i]->ReadCredit( input );
      if ( cred ) {
        _buf_states[input][i]->ProcessCredit( cred );
        delete cred;
      }
    }
    
    for ( int c = 0; c < _classes; ++c ) {
      // Potentially generate packets for any (input,class)
      // that is currently empty
      if ( (_duplicate_networks > 1) || _partial_packets[input][c][0].empty() ) {

	for(int v = 0; v<vc_classes; v++){
	  //the first virtual network we gonna use
	  int vshift = (vc_ptrs[input]+1+v)%vc_classes;
	  
	  if ((*input_buffer)[input][vshift]->isReady()) { //generate a packet
	    _GeneratePacket( input, 1, vshift, _time );
	    (*input_buffer)[input][vshift]->pop();
	  }
	}
      }
    }

    int c=0;
    bool write_flit = false;
    Flit * f;
    if ( !_partial_packets[input][c][0].empty( ) ) {
      f = _partial_packets[input][c][0].front( );
      if ( f->head && f->vc == -1) {
	//vc class should already be assigned depending on messagebuffer
	f->vc =  _buf_states[input][0]->FindAvailable((int)f->gems_net) ;
      }
      
      if ( ( f->vc != -1 ) &&
	   ( !_buf_states[input][0]->IsFullFor( f->vc ) ) ) {

	_partial_packets[input][c][0].pop_front( );
	_buf_states[input][0]->SendingFlit( f );
	write_flit = true;

	if(_pri_type == network_age_based) {
	  f->pri = -_time;
	}

	if(f->watch) {
	  *gWatchOut << GetSimTime() << " | "
		     << "node" << input << " | "
		     << "Injecting flit " << f->id
		     << " at time " << _time
		     << " with priority " << f->pri
		     << "." << endl;
	}
	  
	// Pass VC "back"
	if ( !_partial_packets[input][c][0].empty( ) && !f->tail ) {
	  Flit * nf = _partial_packets[input][c][0].front( );
	  nf->vc = f->vc;
	}

	++_injected_flow[input];
      }
    }
    _net[0]->WriteFlit( write_flit ? f : 0, input );
    if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
      _sent_flits[input]->AddSample(write_flit);
    if (write_flit && f->tail) // If a tail flit, reduce the number of packets of this class.
      _class_array[0][c]--;
  }
  
}

void GEMSTrafficManager::_Step( )
{

  _time=g_eventQueue_ptr->getTime();

  GemsInject();
  
  //advance networks
  for (int i = 0; i < _duplicate_networks; ++i) {
    _net[i]->ReadInputs( );
    _partial_internal_cycles[i] += _internal_speedup;
    while( _partial_internal_cycles[i] >= 1.0 ) {
      _net[i]->InternalStep( );
      _partial_internal_cycles[i] -= 1.0;
    }
  }

  for (int a = 0; a < _duplicate_networks; ++a) {
    _net[a]->WriteOutputs( );
  }
  


  for (int i = 0; i < _duplicate_networks; ++i) {
    // Eject traffic and send credits
    for ( int output = 0; output < _limit; ++output ) {
      Flit * f = _net[i]->ReadFlit( output );

      if ( f ) {
	++_ejected_flow[output];
	f->atime = _time;
        if ( f->watch ) {
	  *gWatchOut << GetSimTime() << " | "
		     << "node" << output << " | "
		     << "Ejecting flit " << f->id
		     << " (packet " << f->pid << ")"
		     << " from VC " << f->vc
		     << "." << endl;
	  *gWatchOut << GetSimTime() << " | "
		     << "node" << output << " | "
		     << "Injecting credit for VC " << f->vc << "." << endl;
        }
      
        Credit * cred = new Credit( 1 );
        cred->vc[0] = f->vc;
        cred->vc_cnt = 1;
	cred->dest_router = f->from_router;
        _net[i]->WriteCredit( cred, output );
        _RetireFlit( f, output );
      
        if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	  _accepted_flits[output]->AddSample( 1 );
      } else {
        _net[i]->WriteCredit( 0, output );
        if( ( _sim_state == warming_up ) || ( _sim_state == running ) )
	  _accepted_flits[output]->AddSample( 0 );
      }
    }

    for(int j = 0; j < _routers; ++j) {
      _received_flow[i*_routers+j] += _router_map[i][j]->GetReceivedFlits();
      _sent_flow[i*_routers+j] += _router_map[i][j]->GetSentFlits();
      _router_map[i][j]->ResetFlitStats();
    }
  }
  ++_network_time;
  if(_network_time%100000==0){
    cout<<"heart beat "<<_network_time<<" "<<endl;
  }
  if(gTrace){
    cout<<"TIME "<<_time<<endl;
  }

}


void GEMSTrafficManager::RegisterMessageBuffers(  Vector<Vector<MessageBuffer*> >* in,   Vector<Vector<MessageBuffer*> >* out){
  input_buffer = in;
  output_buffer = out;
}
