/*flit.cpp
 *
 *flit struct is a flit, carries all the control signals that a flit needs
 *Add additional signals as necessary. Flits has no concept of length
 *it is a singluar object.
 *
 *When adding objects make sure to set a default value in this constructor
 */

#include "booksim.hpp"
#include "flit.hpp"

ostream& operator<<( ostream& os, const Flit& f )
{
  os << "  Flit ID: " << f.id << " (" << &f << ")" 
     << " Type: " << f.type 
     << " Head: " << f.head << " Tail: " << f.tail << endl;
  os << "  Source : " << f.src << "  Dest : " << f.dest << " intm: "<<f.intm<<endl;
  os << "  Injection time : " << f.time << " Delay: "<<f.delay<<" phase: "<<f.ph<< endl;

  return os;
}

Flit::Flit() 
{  
  type      = ANY_TYPE ;
  vc        = -1 ;
  delay     = 0;
  head      = false ;
  tail      = false ;
  true_tail = false ;
  time      = -1 ;
  sn        = 0 ;
  rob_time  = 0 ;
  id        = -1 ;
  hops      = 0 ;
  watch     = false ;
  record    = false ;
  intm = 0;
  src = -1;
  dest = -1;
  pri = 0;
  intm =-1;
  ph = -1;
  dr = -1;
  minimal = 1;
  ring_par = -1;
  x_then_y = -1;
  data = 0;
}  


