#include <string>
#include <sstream>
#include "BooksimConsumer.hpp"  
#include "BooksimWrapper.hpp"  
#include "routefunc.hpp"

///////////////////////////////////////////////////////////////////////////////
//include new network here//

#include "singlenet.hpp"
#include "kncube.hpp"
#include "fly.hpp"
#include "isolated_mesh.hpp"
#include "cmo.hpp"
#include "cmesh.hpp"
#include "cmeshx2.hpp"
#include "flatfly_onchip.hpp"
#include "qtree.hpp"
#include "tree4.hpp"
#include "fattree.hpp"
#include "mecs.hpp"
#include "anynet.hpp"
#include "dragonfly.hpp"

//vcc is vc classes which doesn't have to equal vcs
BooksimConsumer::BooksimConsumer(int nodes, int vcc){
  cout<<"Creating "<<nodes<<" "<<vcc<<endl;
string topo;
  //most transplanted from booksim_main.C
  booksimconfig = new BookSimConfig();
  booksimconfig->ParseFile("/home/qtedq/booksim/gems_interface/testconfig");

  booksimconfig->Assign("limit",(unsigned int)nodes);
  booksimconfig->Assign("message_classes", (unsigned int)vcc);
  InitializeRoutingMap( );
  
  InitializeTrafficMap( );
  InitializeInjectionMap( );

  booksimconfig->GetStr( "topology", topo );
  /*To include a new network, must register the network here
   *add an else if statement with the name of the network
   */
  ostringstream name;
  name << "network_";
  net.push_back(0);
  if ( topo == "torus" ) {
    KNCube::RegisterRoutingFunctions() ;
    net[0] = new KNCube( *booksimconfig, name.str(), false );
  } else if ( topo == "mesh" ) {
    KNCube::RegisterRoutingFunctions() ;
    net[0] = new KNCube( *booksimconfig, name.str(), true );
  } else if ( topo == "cmesh" ) {
    CMesh::RegisterRoutingFunctions() ;
    net[0] = new CMesh( *booksimconfig, name.str() );
  } else if ( topo == "cmeshx2" ) {
    CMeshX2::RegisterRoutingFunctions() ;
    net[0] = new CMeshX2( *booksimconfig, name.str() );
  } else if ( topo == "fly" ) {
    KNFly::RegisterRoutingFunctions() ;
    net[0] = new KNFly( *booksimconfig, name.str() );
  } else if ( topo == "single" ) {
    SingleNet::RegisterRoutingFunctions() ;
    net[0] = new SingleNet( *booksimconfig, name.str() );
  } else if ( topo == "isolated_mesh" ) {
    IsolatedMesh::RegisterRoutingFunctions() ;
    net[0] = new IsolatedMesh( *booksimconfig, name.str() );
  } else if ( topo == "qtree" ) {
    QTree::RegisterRoutingFunctions() ;
    net[0] = new QTree( *booksimconfig, name.str() );
  } else if ( topo == "tree4" ) {
    Tree4::RegisterRoutingFunctions() ;
    net[0] = new Tree4( *booksimconfig, name.str() );
  } else if ( topo == "fattree" ) {
    FatTree::RegisterRoutingFunctions() ;
    net[0] = new FatTree( *booksimconfig, name.str() );
  } else if ( topo == "flatfly" ) {
    FlatFlyOnChip::RegisterRoutingFunctions() ;
    net[0] = new FlatFlyOnChip( *booksimconfig, name.str() );
  } else if ( topo == "cmo"){
    CMO::RegisterRoutingFunctions() ;
    net[0] = new CMO(*booksimconfig, name.str());
  } else if ( topo == "MECS"){
    MECS::RegisterRoutingFunctions() ;
    net[0] = new MECS(*booksimconfig, name.str());
  } else if ( topo == "anynet"){
    AnyNet::RegisterRoutingFunctions() ;
    net[0] = new AnyNet(*booksimconfig, name.str());
  } else if ( topo == "dragonflynew"){
    DragonFlyNew::RegisterRoutingFunctions() ;
    net[0] = new DragonFlyNew(*booksimconfig, name.str());
  }else {
    cerr << "Unknown topology " << topo << endl;
    exit(-1);
  }
  manager = new GEMSTrafficManager( *booksimconfig, net ,vcc) ;

  //  g_eventQueue_ptr->scheduleEvent(this, 1); // Execute in the next cycle.
}

BooksimConsumer::~BooksimConsumer(){
  delete manager;
  delete net[0];
  delete booksimconfig;

}

void BooksimConsumer::wakeup(){


  
  manager->_Step();
  if(manager->inflight()){
    g_eventQueue_ptr->scheduleEvent(this, 1); // Execute in the next cycle.
  } else {
    InjectConsumer::trigger_wakeup = true;
  }
}

void BooksimConsumer::print(ostream& out) const{
  out<<"BooksimConsumer: in your simulator, consuming your packets\n";
}

void BooksimConsumer::printStats(ostream& out) const{
  //make a call to the network since there is no traffic mananger class?
}

void BooksimConsumer::printConfig(ostream& out) const{
  //make a call to the configuration object to dump the booksim config file

}
void BooksimConsumer::RegisterMessageBuffers(Vector<Vector<MessageBuffer*> >* in,   Vector<Vector<MessageBuffer*> >* out)
{
  
  
  manager->RegisterMessageBuffers(in, out);
}
