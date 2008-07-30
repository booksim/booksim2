// ----------------------------------------------------------------------
//
//  CrossBar: Network comprising a single crossbar
//
// ----------------------------------------------------------------------
#ifndef _CROSSBAR_HPP_
#define _CROSSBAR_HPP_

// ----------------------------------------------------------------------
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/05/17 17:14:07 $
//  $Id: crossbar.hpp,v 1.1 2007/05/17 17:14:07 jbalfour Exp $
// ----------------------------------------------------------------------

#include "network.hpp"

class CrossBar : public Network {

public:

  CrossBar( const Configuration& config ) ;
  void RegisterRoutingFunctions() ;

protected:

  void _ComputeSize( const Configuration& config ) ;
  void _BuildNet( const Configuration& config ) ;
  int  _nodes ;

} ;

#endif
