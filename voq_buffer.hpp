#ifndef _VOQ_BUFFER_HPP_
#define _VOQ_BUFFER_HPP_

#include "buffer.hpp"
class VOQ_Buffer : public Buffer{

public:
  VOQ_Buffer(const Configuration& config, int outputs,
	  Module *parent, const string& name );

};
#endif
