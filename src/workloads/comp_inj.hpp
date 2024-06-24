/*
 * swm_inj.hpp
 *
 * Scalable workload model traffic generator for Booksim
 *
 *  Created on: Jun 9, 2022
 *      Author: cjbeckma and hdogan
 */

#ifndef SRC_SWM_INJ_HPP_
#define SRC_SWM_INJ_HPP_

#include <iostream>
#include <vector>
#include <map>
#include <boost/coroutine/all.hpp>
#include "injection.hpp"
#include "swm.hpp"
#include "roi.hpp"
#include "wkld_comp.hpp"
#include "msg.hpp"

class ComponentInjectionProcess : public InjectionProcess {
    WorkloadComponent * _upstream;
    CoalType _coal_type;
    int active_nodes;
  public:
    bool test(int src)               { if(src < active_nodes) return _upstream->test(src); else return false; }
    WorkloadMessagePtr get(int src)  { if(src < active_nodes) return _upstream->get(src); else return NULL; }
    void next(int src)               { if(src < active_nodes ) _upstream->next(src); }
    void eject(WorkloadMessagePtr m) { _upstream->eject(m); }

	ComponentInjectionProcess(int nodes, const string& params, Configuration const * const config);

    typedef pair<string, vector<string> > comp_spec_t;          // component specifier: kind, options
    static vector<comp_spec_t> ParseComponents(const string &); // parse components string

    virtual CoalType get_coal_type() { return _coal_type; }

  private:
    static map<string, vector<comp_spec_t> > _comp_spec_cache;

    static void                _ParseOneComp(const string &, comp_spec_t &);
    static vector<comp_spec_t> _ParseComponentsFromString(const string &);
    static string              _ComponentsStringFromFile(const string &);
    static void                _RmEolSpacesComments(char *);
    static const char*         _RmBolSpaces(const char *i) { const char *o; for(o=i; *o && isspace(*o); ++o) {}; return o; }
};


#endif /* SRC_SWM_INJ_HPP_ */
