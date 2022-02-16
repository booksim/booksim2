/** 
 *  @author github.com/Stfort52
 */
#ifndef _CREDITBOX_HPP_
#define _CREDITBOX_HPP_

#include "box.hpp"
#include "credit.hpp"

class CreditBox : public Box<Credit>
{
public:
  uint32_t OutStanding();
};

inline uint32_t CreditBox::OutStanding()
{
  return all_items.size() - free_items.size();
}
#endif /* _CREDITBOX_HPP_ */