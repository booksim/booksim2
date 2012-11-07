// $Id$

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this 
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

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

/*anynet
 *
 *The 
 *
 */

#include "anynet.hpp"
#include <fstream>
#include <sstream>
#include <limits>

//this is a hack, I can't easily get the routing talbe out of the network
map<int, int>* global_routing_table;

AnyNet::AnyNet( const Configuration &config, const string & name )
  :  Network( config, name ){

  router_list.resize(2);
  _ComputeSize( config );
  _Alloc( );
  _BuildNet( config );
}

AnyNet::~AnyNet(){
  for(int i = 0; i < 2; ++i) {
    for(map<int, map<int, int>* >::iterator iter = router_list[i].begin();
	iter != router_list[i].end();
	++iter) {
      delete iter->second;
    }
  }
}

void AnyNet::_ComputeSize( const Configuration &config ){
  file_name = config.GetStr("network_file");
  if(file_name==""){
    cout<<"No network file name provided"<<endl;
    exit(-1);
  }
  //parse the network description file
  readFile();

  _channels =0;
  cout<<"========================Network File Parsed=================\n";
  cout<<"******************node listing**********************\n";
  map<int,  int >::const_iterator iter;
  for(iter = node_list.begin(); iter!=node_list.end(); iter++){
    cout<<"Node "<<iter->first;
    cout<<"\t Router "<<iter->second<<endl;
  }

  map<int,   map<int, int >*>::const_iterator iter3;
  cout<<"\n****************router to node listing*************\n";
  for(iter3 = router_list[0].begin(); iter3!=router_list[0].end(); iter3++){
    cout<<"Router "<<iter3->first<<endl;
    map<int, int >::const_iterator iter2;
    for(iter2 = iter3->second->begin(); iter2!=iter3->second->end(); iter2++){
      cout<<"\t Node "<<iter2->first<<endl;
    }
  }

  cout<<"\n*****************router to router listing************\n";
  for(iter3 = router_list[1].begin(); iter3!=router_list[1].end(); iter3++){
    cout<<"Router "<<iter3->first<<endl;
    map<int, int >::const_iterator iter2;
    if(iter3->second->size() == 0){
      cout<<"Caution Router "<<iter3->first
	  <<" is not connected to any other Router\n"<<endl;
    }
    for(iter2 = iter3->second->begin(); iter2!=iter3->second->end(); iter2++){
      cout<<"\t Router "<<iter2->first<<endl;
      _channels++;
    }
  }

  _size = router_list[1].size();
  _nodes = node_list.size();

}



void AnyNet::_BuildNet( const Configuration &config ){
  

  //I need to keep track the output ports for each router during build
  int * outport = (int*)malloc(sizeof(int)*_size);
  for(int i = 0; i<_size; i++){outport[i] = 0;}

  cout<<"==========================Node to Router =====================\n";
  //adding the injection/ejection chanenls first
  map<int,   map<int, int >*>::const_iterator niter;
  for(niter = router_list[0].begin(); niter!=router_list[0].end(); niter++){
    map<int,   map<int, int >*>::const_iterator riter = router_list[1].find(niter->first);
    //calculate radix
    int radix = niter->second->size()+riter->second->size();
    int node = niter->first;
    cout<<"router "<<node<<" radix "<<radix<<endl;
    //decalre the routers 
    ostringstream router_name;
    router_name << "router";
    router_name << "_" <<  node ;
    _routers[node] = Router::NewRouter( config, this, router_name.str( ), 
    					node, radix, radix );
    _timed_modules.push_back(_routers[node]);
    //add injeciton ejection channels
    map<int, int >::const_iterator nniter;
    for(nniter = niter->second->begin();nniter!=niter->second->end(); nniter++){
      int link = nniter->first;
      //add the outport port assined to the map
      (*(niter->second))[link] = outport[node];
      outport[node]++;
      cout<<"\t connected to node "<<link<<" at outport "<<nniter->second<<endl;
      _routers[node]->AddInputChannel( _inject[link], _inject_cred[link] );
      _routers[node]->AddOutputChannel( _eject[link], _eject_cred[link] );
    }

  }

  cout<<"==========================Router to Router =====================\n";
  //add inter router channels
  //since there is no way to systematically number the channels we just start from 0
  //the map, is a mapping of output->input
  int channel_count = 0; 
  for(niter = router_list[0].begin(); niter!=router_list[0].end(); niter++){
    map<int,   map<int, int >*>::const_iterator riter = router_list[1].find(niter->first);
    int node = niter->first;
    map<int, int >::const_iterator rriter;
    cout<<"router "<<node<<endl;
    for(rriter = riter->second->begin();rriter!=riter->second->end(); rriter++){
      int other_node = rriter->first;
      int link = channel_count;
      //add the outport port assined to the map
      (*(riter->second))[other_node] = outport[node];
      outport[node]++;
      cout<<"\t connected to router "<<other_node<<" using link "<<link<<" at outport "<<rriter->second<<endl;
      _routers[node]->AddOutputChannel( _chan[link], _chan_cred[link] );
      _routers[other_node]->AddInputChannel( _chan[link], _chan_cred[link]);
      channel_count++;
    }
  }

  buildRoutingTable();

}


void AnyNet::RegisterRoutingFunctions() {
  gRoutingFunctionMap["min_anynet"] = &min_anynet;
}

void min_anynet( const Router *r, const Flit *f, int in_channel, 
		 OutputSet *outputs, bool inject ){
  int out_port=-1;
  if(!inject){
    assert(global_routing_table[r->GetID()].count(f->dest)!=0);
    out_port=global_routing_table[r->GetID()].find(f->dest)->second;
  }
 

  int vcBegin = 0, vcEnd = gNumVCs-1;
  if ( f->type == Flit::READ_REQUEST ) {
    vcBegin = gReadReqBeginVC;
    vcEnd   = gReadReqEndVC;
  } else if ( f->type == Flit::WRITE_REQUEST ) {
    vcBegin = gWriteReqBeginVC;
    vcEnd   = gWriteReqEndVC;
  } else if ( f->type ==  Flit::READ_REPLY ) {
    vcBegin = gReadReplyBeginVC;
    vcEnd   = gReadReplyEndVC;
  } else if ( f->type ==  Flit::WRITE_REPLY ) {
    vcBegin = gWriteReplyBeginVC;
    vcEnd   = gWriteReplyEndVC;
  }

  outputs->Clear( );

  outputs->AddRange( out_port , vcBegin, vcEnd );
}

void AnyNet::buildRoutingTable(){
  cout<<"==========================Router to Router =====================\n";  
  routing_table.resize(_size);

  for(int i = 0; i<_size; i++){
    route(i);
  }
  global_routing_table = &routing_table[0];
}


//11/7/2012
//basically djistra's, tested on a large dragonfly anynet configuration
void AnyNet::route(int r_start){
  int* dist = new int[_size];
  int* prev = new int[_size];
  set<int> rlist;
  for(int i = 0; i<_size; i++){
    dist[i] =  numeric_limits<int>::max();
    prev[i] = -1;
    rlist.insert(i);
  }
  dist[r_start] = 0;
  while(!rlist.empty()){
    //find min 
    int min_dist = numeric_limits<int>::max();
    int min_cand = -1;
    for(set<int>::iterator i = rlist.begin();
	i!=rlist.end();
	i++){
      if(dist[*i]<min_dist){
	min_dist = dist[*i];
	min_cand = *i;
      }
    }
    rlist.erase(min_cand);

    //neighbor
    for(map<int,int>::iterator i = router_list[1][min_cand]->begin(); 
	i!=router_list[1][min_cand]->end(); 
	i++){
      int new_dist = dist[min_cand] + 1;//distance is hops not cycles
      if(new_dist < dist[i->first]){
	dist[i->first] = new_dist;
	prev[i->first] = min_cand;
      }
    }
  }
  
  //post process from the prev list
  for(int i = 0; i<_size; i++){
    if(prev[i] ==-1){ //self
      assert(i == r_start);
      for(map<int, int>::iterator iter = router_list[0][i]->begin();
	  iter!=router_list[0][i]->end();
	  iter++){
	routing_table[r_start][iter->first]=iter->second;
	cout<<"node "<<iter->first<<" port "<< iter->second<<endl;
      }
    } else {
      int distance=1;
      int neighbor=i;
      while(prev[neighbor]!=r_start){
	neighbor= prev[neighbor];
	distance++;
      }
      assert( router_list[1][r_start]->count(neighbor)!=0);
      int port = router_list[1][r_start]->find(neighbor)->second;
      for(map<int, int>::iterator iter = router_list[0][i]->begin();
	  iter!=router_list[0][i]->end();
	  iter++){
	routing_table[r_start][iter->first]=port;
	cout<<"node "<<iter->first<<" port "<< port<<" dist "<<distance<<endl;
      }
    }
  }
}


void AnyNet::readFile(){

  ifstream network_list;
  string line;
  enum ParseState{HEAD_TYPE=0,
		  HEAD_ID,
		  BODY_TYPE, 
		  BODY_ID,
		  LINK_WEIGHT};
  enum ParseType{NODE=0,
		 ROUTER,
		 UNKNOWN};

  network_list.open(file_name.c_str());
  if(!network_list.is_open()){
    cout<<"can't open network file "<<file_name<<endl;
    exit(-1);
  }
  
  //loop through the entire file
  while(!network_list.eof()){
    getline(network_list,line);
    if(line==""){
      continue;
    }

    ParseState state=HEAD_TYPE;
    //position to parse out white sspace
    int pos = 0;
    int next_pos=-1;
    string temp;
    //the first node and its type
    int head_id = -1;
    ParseType head_type = UNKNOWN;
    //stuff that head are linked to
    ParseType body_type = UNKNOWN;
    int body_id = -1;
    int link_weight = 1;

    do{

      //skip empty spaces
      next_pos = line.find(" ",pos);
      temp = line.substr(pos,next_pos-pos);
      pos = next_pos+1;
      if(temp=="" || temp==" "){
	continue;
      }

      switch(state){
      case HEAD_TYPE:
	if(temp=="router"){
	  head_type = ROUTER;
	} else if (temp == "node"){
	  head_type = NODE;
	} else {
	  cout<<"Unknow head of line type "<<temp<<"\n";
	  assert(false);
	}
	state=HEAD_ID;
	break;
      case HEAD_ID:
	//need better error check
	head_id = atoi(temp.c_str());

	//intialize router structures
	if(router_list[NODE].count(head_id) == 0){
	  router_list[NODE][head_id] = new map<int, int>;
	}
	if(router_list[ROUTER].count(head_id) == 0){
	  router_list[ROUTER][head_id] = new map<int, int>;
	}  

	state=BODY_TYPE;
	break;
      case LINK_WEIGHT:
      case BODY_TYPE:
	if(temp=="router"){
	  body_type = ROUTER;
	} else if (temp == "node"){
	  body_type = NODE;
	} else {
	  cout<<"Unknow body type "<<temp<<"\n";
	  assert(false);
	}
	state=BODY_ID;
	break;
      case BODY_ID:
	body_id = atoi(temp.c_str());	
	//intialize router structures if necessary
	if(body_type==ROUTER){
	  if(router_list[NODE].count(body_id) ==0){
	    router_list[NODE][body_id] = new map<int, int>;
	  }
	  if(router_list[ROUTER].count(body_id) == 0){
	    router_list[ROUTER][body_id] = new map<int, int>;
	  }
	}

	if(head_type==NODE && body_type==NODE){ 

	  cout<<"Cannot connect node to node "<<temp<<"\n";
	  assert(false);

	} else if(head_type==NODE && body_type==ROUTER){

	  if(node_list.count(head_id)!=0 &&
	     node_list[head_id]!=body_id){
	    cout<<"Node "<<body_id<<" trying to connect to multiple router "
		<<body_id<<" and "<<node_list[head_id]<<endl;
	    assert(false);
	  }
	  node_list[head_id]=body_id;
	  router_list[NODE][body_id]->insert(pair<int, int>(head_id,-1));

	} else if(head_type==ROUTER && body_type==NODE){
	  //insert and check node
	  if(node_list.count(body_id) != 0 &&
	     node_list[body_id]!=head_id){
	    cout<<"Node "<<body_id<<" trying to connect to multiple router "
		<<body_id<<" and "<<node_list[head_id]<<endl;
	    assert(false);
	  }
	  node_list[body_id] = head_id;
	  router_list[NODE][head_id]->insert(pair<int, int>(body_id,-1));

	} else if(head_type==ROUTER && body_type==ROUTER){
	  router_list[ROUTER][head_id]->insert(pair<int, int>(body_id,-1));
	  router_list[ROUTER][body_id]->insert(pair<int, int>(head_id,-1));
	}
	state=LINK_WEIGHT;
	break ;
      default:
	cout<<"Unknow parse state\n";
	assert(false);
	break;
      }

    } while(pos!=0);
    if(state!=LINK_WEIGHT &&
       state!=BODY_TYPE){
      cout<<"Warn incomplete parse of the line: "<<line<<endl;
      exit(-1);
    }

  }



 

      /*
    //loop through each element in a line
    do{
      next_pos = line.find(" ",pos);
      string temp = line.substr(pos,next_pos-pos);
      pos = next_pos+1;
      //skip empty spaces
      if(temp=="" || temp==" "){
	continue;
      }

      //////////////////////////////////
      //real parsing begins
      if(name){
	int id = atoi(temp.c_str());
	//if this is a head 
	if(!head){ //indicates the beginin of the line
	  head = true;
	  head_type = body_type;
	  head_position = id;
	} else { 
	  //if this is a body parse, approriately
	  if(body_type==-1){
	    cout<<"illegal body type\n";
	    exit(-1);
	  }
	  //use map depending on head type
	  switch(head_type){
	  case 0: //node
	    if(body_type==0){
	      cout<<"can not connect node to node\n";
	      exit(-1);
	    }
	    //insert into the node list
	    if(node_list.find(head_position) != node_list.end()){
	      if(node_list.find(head_position)->second!=id){
		cout<<"node "<<head_position<<" is conncted to multiple routers! "
		    <<id<<" and "<<node_list.find(head_position)->second<<endl;
		exit(-1);
	      }
	    } else {
	      node_list[head_position]=id;
	    }
	    //reverse insert
	    if(router_list[0].find(id) == router_list[0].end()){
	      (router_list[0])[id] = new map<int, int>;
	    } 
	    (*((router_list[0]).find(id)->second))[head_position]=-1;
	    //initialize the other router array as well
	    if(router_list[1].find(id) == router_list[1].end()){
	      (router_list[1])[id] = new map<int, int>;
	    } 
	    break;
	  case 1: //router
	    //insert into router list
	    if(router_list[body_type].find(head_position) == router_list[body_type].end()){
	      (router_list[body_type])[head_position] = new map<int, int>;
	    } 
	    (*((router_list[body_type]).find(head_position)->second))[id]=-1;

	    //reverse insert
	    if(body_type == 0){
	      if(node_list.find(id) == node_list.end()){
		node_list[id] = head_position;
	      }  else {
		if(node_list.find(id)->second!=head_position){
		  cout<<"node "<<id<<" is conncted to multiple routers! "
		      <<head_position<<" and "<<node_list.find(id)->second<<endl;
		  exit(-1);
		}
	      }
	      //initialize the other array as well
	      if(router_list[1].find(head_position) == router_list[1].end()){
		(router_list[1])[head_position] = new map<int, int>;
	      } 
	    } else {
	      if(router_list[1].find(id) == router_list[1].end()){
		(router_list[1])[id] = new map<int, int>;
	      } 
	      (*((router_list[1]).find(id)->second))[head_position]=-1;
	      //initialize the other array as well
	      if(router_list[0].find(head_position) == router_list[0].end()){
		(router_list[0])[head_position] = new map<int, int>;
	      } 
	      if(router_list[0].find(id) == router_list[0].end()){
		(router_list[0])[id] = new map<int, int>;
	      } 
	    }

	    break;
	  default:
	    cout<<"illegal head type\n";
	    exit(-1);
	  }
	}
	name = false;
	body_type = -1;
      } else {	//setting type name
	name= true;
	if(temp=="router"){
	  body_type = 1;
	} else if (temp == "node"){
	  body_type = 0;
	}
      }
    } while(pos!=0);

  }

      */ 

  //map verification, make sure the information contained in bother maps
  //are the same
  assert(router_list[0].size() == router_list[1].size());

}

