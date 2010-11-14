
#include "BooksimWrapper.hpp"
#include <sstream>


#define bout cout<<"Booksim:"

static bool booksim_active = false;
vector<SS_Network::Message*> membrane;
BooksimInterface::BooksimInterface(string name, SystemConfig* sysCon, Fwk::Log* log, int id):
  Interface(name,sysCon,log,id){
  
}

void BooksimInterface::route(){

  for(int i = 0; i<_sysCon->nCores; i++){
    if(membrane[i]!=0){
      int dest_id = membrane[i]->Destination->id();
      membrane[i]->DeliveryTime == parent->now();
      parent->getCore(dest_id)->messageIs(membrane[i],(const Link*)1);
      membrane[i] = 0;
    }
  }
}

BooksimCoreInterface::BooksimCoreInterface(string name, SystemConfig* sysCon, Fwk::Log* log, int id, Activity::Ptr a):
  CoreInterface(name,sysCon,log,id){
  mainbooksima = a;
}
//injection
void BooksimCoreInterface::messageIs(SS_Network::Message *msg){
  
 
  if(_log)_log->debugf("network", 1, "%s sending message of type %s id=0x%x to %s", name().c_str(), msg->Type.c_str(), msg->Id, msg->Destination->name().c_str());

  membrane[this->id()] = msg;

  if(!booksim_active){
    booksim_active;
    mainbooksima->statusIs(Activity::executing);
  }
}

BooksimWrapper::BooksimWrapper(SystemConfig* sysCon, Fwk::Log* log)
  : NetworkSim(sysCon,log){
  bout<<"simple\n";

}
BooksimWrapper::BooksimWrapper(Activity::Manager* manager, int basePriority, SystemConfig* s, Fwk::Log* l)
 : NetworkSim ( manager, basePriority, s,l){
  bout<<"complex\n";
  mainbooksim = new BooksimInterface("main booksim",s,l,0);

  mainbooksima= _actMgr->activityNew(mainbooksim->name()+"Activity", 0);
  mainbooksim->setParent(this);
  mainbooksimr= new InterfaceActivityReactor(_actMgr.ptr(), mainbooksima.ptr(), mainbooksim, this);
}

BooksimWrapper::~BooksimWrapper(){

  
}

void  BooksimWrapper::init(NetworkType type){
  bout<<"init\n";
  membrane.resize(_sysCon->nCores);
  bout<<"creating "<<_sysCon->nCores<<" core interfaces\n";
  for(int i=0; i<_sysCon->nCores; i++){

    stringstream number;
    //can only simulate 99999 nodes
    number<<setfill('0')<<setw(5)<<i;
    CoreInterface::Ptr iface = new BooksimCoreInterface(string("CIF")+number.str(), _sysCon, _log, i, mainbooksima);
    this->coreInterfaceIs(iface.ptr(), i);
    
    membrane[i] = 0;
    
  }

}

void BooksimWrapper::coreInterfaceIs(CoreInterface* iface, int idx){
    if(_coreInterfaces[idx]!=NULL){
        throw Fwk::ConstraintException("Interface for index %d already exists in NetworkSim::coreInterfaceIs", idx);
    }
    _coreInterfaces[idx]=iface;

    //create activities/reactors
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
