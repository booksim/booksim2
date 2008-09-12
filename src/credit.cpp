/*credit.cpp
 *
 *A class for credits
 */

#include "booksim.hpp"
#include "credit.hpp"

Credit::Credit( int max_vcs )
{
  vc = new int [max_vcs];
  vc_cnt = 0;

  tail = false;
  id   = -1;
  dest_router = -1;
}

Credit::~Credit( )
{
  delete [] vc;
}
