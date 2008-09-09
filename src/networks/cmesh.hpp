////////////////////////////////////////////////////////////////////////
//
// CMesh: Mesh topology with concentration and express links along the
//         edge of the network
//
////////////////////////////////////////////////////////////////////////
//
// RCS Information:
//  $Author: jbalfour $
//  $Date: 2007/06/26 22:49:23 $
//  $Id: cmesh.hpp,v 1.2 2007/06/26 22:49:23 jbalfour Exp $
// 
////////////////////////////////////////////////////////////////////////
#ifndef _CMESH_HPP_
#define _CMESH_HPP_

#include "network.hpp"
#include "routefunc.hpp"

class CMesh : public Network {
public:
  CMesh( const Configuration &config );
  int GetN() const;
  int GetK() const;
  void SetChannelCookie( int cookie );

  static int NodeToRouter( int address ) ;
  static int NodeToPort( int address ) ;

  static void RegisterRoutingFunctions() ;

private:

  static int _cX ;
  static int _cY ;

  static int _memo_NodeShiftX ;
  static int _memo_NodeShiftY ;
  static int _memo_PortShiftY ;

  void _ComputeSize( const Configuration &config );
  void _BuildNet( const Configuration& config );

  int _k ;
  int _n ;
  int _c ;
  bool _express_channels;
};

//
// Routing Functions
//
void xy_yx_cmesh( const Router *r, const Flit *f, int in_channel, 
		  OutputSet *outputs, bool inject ) ;

void xy_yx_no_express_cmesh( const Router *r, const Flit *f, int in_channel, 
			     OutputSet *outputs, bool inject ) ;

void dor_cmesh( const Router *r, const Flit *f, int in_channel, 
		OutputSet *outputs, bool inject ) ;

void dor_no_express_cmesh( const Router *r, const Flit *f, int in_channel, 
			   OutputSet *outputs, bool inject ) ;

#endif
