/*
 * swm_inj.cpp
 *
 * Booksim scalable workload model implementation
 *
 *  Created on: Jun 9, 2022
 *      Author: cjbeckma and hdogan
 */
#include <iostream>
#include <fstream>
#include "comp_inj.hpp"
#include "config_utils.hpp"
#include "wcomp_smc.hpp"
#include "msg.hpp"


// cache of parsed component specs
map<string, vector<ComponentInjectionProcess::comp_spec_t> > ComponentInjectionProcess::_comp_spec_cache;

// construct component injection object
ComponentInjectionProcess::ComponentInjectionProcess(
      int nodes, const string& params,
      Configuration const * const config) : InjectionProcess(nodes, vector<double>(nodes, 1.0))
{

   // Run Mode :  true  -> run to completion
   //             false -> run for specified sample period
   gSwmAppRunMode = config->GetInt("swm_app_run_mode");
   active_nodes = config->GetInt("swm_active_nodes");
   if(active_nodes == -1)
      active_nodes = nodes;

   _upstream = 0;
   auto paramv = ParseComponents(params);

   // each item in the vector of strings is a component specifier, e.g. "SWM(randperm)", or "Mppn", etc.
   // Build a chain of these components after parsing into names ("SWM") and options ("randperm" or "")
   for(comp_spec_t &comp : paramv)
   {
      try {
         _upstream = WorkloadComponent::New(comp.first, active_nodes, comp.second, config, _upstream);
      }
      catch (std::out_of_range &e) {
         std::cerr << "Unknown component type: " << comp.first << std::endl;
         ::exit(-1);
      }
   }
   _upstream->Init(active_nodes, config);

   // set the coalescing type, in case SMC is used
   auto c = _upstream->ComponentOfType<SmallMessageCoalescing>();
   _coal_type = c ? c->get_coal_type() : CoalType::NO_COAL;
}

// Parse components from a string, or from the contents of a file.
// Returns a list of component specifier name/options records.
vector<ComponentInjectionProcess::comp_spec_t>
ComponentInjectionProcess::ParseComponents(
   const string &s)
{
   // first check the cache to see if we previously parsed the same string
   auto itr = _comp_spec_cache.find(s);
   if (itr != _comp_spec_cache.end())
      return itr->second;
   // next see if the input string is a config file
   string sf = _ComponentsStringFromFile(s);
   // use either the file contents, or the input string directly. Parse it, cache it, return it.
   auto v = _ParseComponentsFromString(sf.empty() ? s : sf);
   _comp_spec_cache[s] = v;
   return v;
}

// parse components string, e.g. "SWM(randperm),Mppn(4),trace(get,eject)",
// return a vector of (kind, options) pairs.
vector<ComponentInjectionProcess::comp_spec_t>
ComponentInjectionProcess::_ParseComponentsFromString(
   const string &s)
{
   auto cv = str_comma_split(s);
   vector<comp_spec_t> parsed(cv.size());
   int i = 0;
   for(auto &c : cv)
      _ParseOneComp(c, parsed[i++]);
   return parsed;
}

// parse a component specifier, e.g. "SWM(randperm)" into name ("SWM") and options ("randperm")
void ComponentInjectionProcess::_ParseOneComp(
   const string & spec, // input: specifier string
   comp_spec_t &  item) // output: component kind string, options (string vector)
{
   item.second.resize(0);
   auto lp = spec.find_first_of("(");
   if (string::npos == lp) {
      item.first = spec;
      return;
   }
   item.first = spec.substr(0, lp);
   auto rp = spec.find_last_of(")");
   item.second = str_comma_split(spec.substr(lp+1, rp-lp-1));
}

// Parse components string from a file, return component spec string.
// If file cannot be opened, return an empty string.
string ComponentInjectionProcess::_ComponentsStringFromFile(
   const string &fname)
{
   std::ifstream ifs(fname.c_str());
   std::string pstring; // string to parse, extracted from file
   while (ifs.good()) {
      char iline[1024];
      ifs.getline(iline, 1024);
      _RmEolSpacesComments(iline);
      const char* ilin = _RmBolSpaces(iline);
      if (ilin[0] == '\0') continue;
      // add a comma if the user forgot to
      if (!pstring.empty() && pstring[pstring.size()-1] != ',')
         pstring.append(",");
      pstring.append(ilin);
   }
   return pstring;
}

// Remove end-of-line spaces and comments, modify the input string
void ComponentInjectionProcess::_RmEolSpacesComments(char *i)
{
   std::list<char> qstack;
   // if you find a hash mark not inside a quote, place end-of-string marker there
   char* s;
   for (s=i; *s; s++) {
      bool stk_empty = qstack.empty();
      if (*s=='\'') {                    // handle single quotes
         if (!stk_empty && qstack.front()=='\'') qstack.pop_front();
         else qstack.push_front(*s);
      } else if (*s=='"') {              // handle double quotes
         if (!stk_empty && qstack.front()=='"') qstack.pop_front();
         else qstack.push_front(*s);
      } else if (stk_empty && *s=='#') { // handle start of comment
         *s = '\0';
         break;
      }
   }
   // move end marker backward to just after last non-space character
   while (s-- != i && isspace(*s)) *s = '\0';
}
