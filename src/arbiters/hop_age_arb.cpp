#include "hop_age_arb.hpp"
#include <iostream>

HopAgeArbiter::HopAgeArbiter(Module *parent, const string &name, int size)
  : Arbiter(parent, name, size), _pointer(0)
{
  assert(size > 0);
}

int HopAgeArbiter::Arbitrate(int *id, int *pri)
{
  _selected = -1;
  for(int input = 0; input < _size; ++input) {
    if(!_request[input].valid) continue;
    assert(_request[input].itime >= 0);
    if(_selected == -1 ||
       Supersedes(input, _request[input].pri, _request[input].itime,
                  _selected, _request[_selected].pri, _request[_selected].itime,
                  _pointer, _size)) {
      _selected = input;
    }
  }
  return Arbiter::Arbitrate(id, pri);
}

void HopAgeArbiter::UpdateState()
{
  if(_selected >= 0) _pointer = (_selected + 1) % _size;
}

void HopAgeArbiter::PrintState() const
{
  std::cout << "Hop/age arbiter pointer = " << _pointer << std::endl;
}
