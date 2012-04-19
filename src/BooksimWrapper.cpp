
#include "BooksimWrapper.hpp"
#include <sstream>
//////////////////////////////////////////////////////////////////////////////
//include new network here//

#include "kncube.hpp"
#include "fly.hpp"
#include "cmesh.hpp"
#include "flatfly_onchip.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
#include "anynet.hpp"
#include "dragonfly.hpp"
#include "credit.hpp"

#define bout cout<<"Booksim:"

//a booksim activity is already in the event queue
bool booksim_active = false;

//if inflight>0, booksim_activity needs to be true
int booksim_inflight = 0;

//indicate message is available from a coreinterface
vector<int> has_message;

//pointer to the sstrafficmanager, needed by rest of booksim
extern TrafficManager * trafficManager;

int clock_ratio = 1;
//Perform almost every function of the original main.cpp, initialization of booksim
BooksimInterface::BooksimInterface(string name, SystemConfig* sysCon, Fwk::Log* log, int id):
  Interface(name,sysCon,log,id){

  booksimconfig = new BookSimConfig();
  booksimconfig->ParseFile(sysCon->booksimConfig);


  InitializeRoutingMap(*booksimconfig );


  string topo;

  topo = booksimconfig->GetStr( "topology");
  clock_ratio = booksimconfig->GetInt( "clock_ratio");

  int networks  = booksimconfig->GetInt("subnets");
  /*To include a new network, must register the network here
   *add an else if statement with the name of the network
   */
  net.resize(networks);
  for (int i = 0; i < networks; ++i) {
    ostringstream name;
    name << "network_" << i;
    if ( topo == "torus" ) {
      KNCube::RegisterRoutingFunctions() ;
      net[i] = new KNCube( *booksimconfig, name.str(), false );
    } else if ( topo == "mesh" ) {
      KNCube::RegisterRoutingFunctions() ;
      net[i] = new KNCube( *booksimconfig, name.str(), true );
    } else if ( topo == "cmesh" ) {
      CMesh::RegisterRoutingFunctions() ;
      net[i] = new CMesh( *booksimconfig, name.str() );
    }else if ( topo == "fly" ) {
      KNFly::RegisterRoutingFunctions() ;
      net[i] = new KNFly( *booksimconfig, name.str() );
    } else if ( topo == "qtree" ) {
      QTree::RegisterRoutingFunctions() ;
      net[i] = new QTree( *booksimconfig, name.str() );
    } else if ( topo == "tree4" ) {
      Tree4::RegisterRoutingFunctions() ;
      net[i] = new Tree4( *booksimconfig, name.str() );
    } else if ( topo == "fattree" ) {
      FatTree::RegisterRoutingFunctions() ;
      net[i] = new FatTree( *booksimconfig, name.str() );
    } else if ( topo == "flatfly" ) {
      FlatFlyOnChip::RegisterRoutingFunctions() ;
      net[i] = new FlatFlyOnChip( *booksimconfig, name.str() );
    } else if ( topo == "anynet"){
      AnyNet::RegisterRoutingFunctions() ;
      net[i] = new AnyNet(*booksimconfig, name.str());
    } else if ( topo == "dragonflynew"){
      DragonFlyNew::RegisterRoutingFunctions() ;
      net[i] = new DragonFlyNew(*booksimconfig, name.str());
    }else {
      cerr << "Unknown topology " << topo << endl;
      exit(-1);
    }
  }

  manager = new SSTrafficManager(*booksimconfig, net);
  trafficManager = manager;
}

BooksimInterface::~BooksimInterface(){
  delete net[0];
  delete manager;
}

//connect core interface to the trafficmanager, called by the main booksim interface
void BooksimInterface::setParent(BooksimWrapper* a){
  parent = a;
  for(int i = 0; i<_sysCon->nCores; i++){
    manager->registerNode(a->getCore(i),i);
  }
}

/*I dont' know if this is corrent, but returning time(0) is definitely wrong*/
Time BooksimInterface::nextEventTime(){
  if( booksim_active){
    return Time::Future();
  } else {
    return Time::Future();
  }
}

//Activited when called by the event queue,
void BooksimInterface::route(){
  //time value is pased to trafficmanger to synchronize network time with systemsim
  if(parent->now().value()%clock_ratio==0){
    manager->_Step(parent->now().value());
  }

  //add the network to the nnext cycle
  if(booksim_inflight!=0||Credit::OutStanding()!=0){
    mainbooksima->nextTimeIs((parent->now()+1));
    mainbooksima->statusIs(Activity::nextTimeScheduled);
  } else {
    booksim_active= false;
    mainbooksima->nextTimeIs(Time::Future());
    mainbooksima->statusIs(Activity::free);
  }
}





/*I could make a activity reactor for each core interface, or I can just pass the mainbooksim activity reactor*/
BooksimCoreInterface::BooksimCoreInterface(string name, SystemConfig* sysCon, Fwk::Log* log, int id, Activity::Ptr a,NetworkSim * p):
  CoreInterface(name,sysCon,log,id){
  mainbooksima = a;
  parent = p;
}

//injection
void BooksimCoreInterface::messageIs(SS_Network::Message *msg){
  
 

  msg->Id = generateId();
  msg->SourceTime = parent->now();

  //queue the message to send
  _inBuffer.push_back(msg);
  has_message[this->id()]++;
  booksim_inflight++;
  
  if(!booksim_active){
    booksim_active=true;
    mainbooksima->statusIs(Activity::executing);
  }
  
}

//ejection
void BooksimCoreInterface::messageIs(SS_Network::Message *msg, const Link *source){
  booksim_inflight--;
  CoreInterface::messageIs(msg,source);
}

SS_Network::Message* BooksimCoreInterface::message(int vc) const{
  if(vc==-1){
    return _inBuffer.front().ptr();
  } else {
    cout<<"Booksim core interface does not differentiate VCs yet\n";
    assert(false);
    return CoreInterface::message(vc);
  }
}
void BooksimCoreInterface::pop(int vc){
  if(vc==-1){
    _inBuffer.pop_front();
    has_message[this->id()]--;
  } else {
    CoreInterface::pop(vc);
  }
}









BooksimWrapper::BooksimWrapper(SystemConfig* sysCon, Fwk::Log* log)
  : NetworkSim(sysCon,log){
  bout<<"simple\n";

}
BooksimWrapper::BooksimWrapper(Activity::Manager* manager, int basePriority, SystemConfig* s, Fwk::Log* l)
  : NetworkSim ( manager, basePriority, s,l){
  bout<<"complex\n";
  mainbooksim = new BooksimInterface("main booksim",s,l/*don't have a log*/,0/*id is 0*/);

  mainbooksima= _actMgr->activityNew(mainbooksim->name()+"Activity", 0);
  _ifaceActivities[mainbooksim] = mainbooksima;
  mainbooksim->setActivity(mainbooksima);
  mainbooksimr= new InterfaceActivityReactor(_actMgr.ptr(), mainbooksima.ptr(), mainbooksim, this);
}

BooksimWrapper::~BooksimWrapper(){

  
}

void  BooksimWrapper::init(NetworkType type){
  bout<<"init\n";
  has_message.resize(_sysCon->nCores);
  bout<<"creating "<<_sysCon->nCores<<" core interfaces\n";
  for(int i=0; i<_sysCon->nCores; i++){

    stringstream number;
    //can only simulate 99999 nodes
    number<<setfill('0')<<setw(5)<<i;
    CoreInterface::Ptr iface = new BooksimCoreInterface(string("CIF")+number.str(), _sysCon, _log, i, mainbooksima, this);
    this->coreInterfaceIs(iface.ptr(), i);
    
    has_message[i] = 0;
    
  }
  mainbooksim->setParent(this);
}

void BooksimWrapper::coreInterfaceIs(CoreInterface* iface, int idx){
  if(_coreInterfaces[idx]!=NULL){
    throw Fwk::ConstraintException("Interface for index %d already exists in NetworkSim::coreInterfaceIs", idx);
  }
  _coreInterfaces[idx]=iface;

  //create activities/reactors not sure if any of these is actually used
  CoreInterfaceReactor::Ptr lr = new CoreInterfaceReactor(iface, this);
  iface->msgInjectionNotifieeIs(lr.ptr());
  Activity::Ptr act = _actMgr->activityNew(iface->name()+"Activity", _ifacePriority+_basePriority);
  InterfaceActivityReactor::Ptr lar= new InterfaceActivityReactor(_actMgr.ptr(), act.ptr(), iface, this);
  _coreIfaceReactors[iface] = lr;
  _ifaceActivities[iface] = act;
  _ifaceActivityReactors[iface] = lar;

  act->statusIs(Activity::free);
}


CoreInterface* BooksimWrapper::coreInterface(int idx){
  return _coreInterfaces[idx].ptr();
}


void BooksimWrapper::printStats(ostream& out) const{
}
void BooksimWrapper::clearStats(){

}

void BooksimWrapper::printConfig(ostream& out) const{
}

void BooksimWrapper::reset(){

}

void BooksimWrapper::print(ostream& out) const{
  out<<"BooksimWrapper: not much to report\n";
}


void BooksimWrapper::printPartialStats(int i){
  trafficManager->printPartialStats(now().value(),i);
}

void BooksimWrapper::report(Fwk::Log* log){

  trafficManager->DisplayStats();

}
