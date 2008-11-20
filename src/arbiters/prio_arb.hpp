// $Id$
#ifndef _PRIO_ARB_HPP_
#define _PRIO_ARB_HPP_

#include <list>

#include "module.hpp"
#include "config_utils.hpp"

class PriorityArbiter : public Module {
  int _rr_ptr;

protected:
  const int _inputs;

  struct sRequest {
    int in;
    int label;
    int pri;
  };

  list<sRequest> _requests;

  int _match;

public:
  PriorityArbiter( const Configuration &config,
		   Module *parent, const string& name,
		   int inputs );
  ~PriorityArbiter( );

  void Clear( );

  void AddRequest( int in, int label = 0, int pri = 0 );
  void RemoveRequest( int in, int label = 0 );

  int Match( ) const;

  void Arbitrate( );
  void Update( );
};

#endif 
