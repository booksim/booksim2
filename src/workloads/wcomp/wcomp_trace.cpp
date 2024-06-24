/*
 * workload component implementation
 *
 * C. Beckmann (c) Intel, 2023
 * H. Dogan
 */
#include "globals.hpp"
#include "wkld_comp.hpp"
#include <ios>
#include <ostream>
#include <fstream>
#include <sstream>

//////////////////
// Trace message traffic activity
class WorkloadTrafficTrace : public WComp<WorkloadTrafficTrace>
{
    static int _nextfile;

    bool _trace_test, _trace_get, _trace_next, _trace_eject, _trace_msg, _trace_time, _trace_deep;
    std::ostream * _ostrm;

    std::ostream * _get_trace_file() { // create a unique output file for each instance
        std::ostringstream fn;
        fn << "trace" << _nextfile++ << ".txt";
        return new std::ofstream(fn.str().c_str());
    }
    std::string _get_time_str() { // maybe return a prefix "t=<time> " to print
        std::ostringstream o;
        if (_trace_time) o << "t=" << GetSimTime() << ' ';
        return o.str();
    }
  public:
    WorkloadTrafficTrace(int nodes, const vector<string> &options, Configuration const * const config, WorkloadComponent *upstrm)
      : WComp<WorkloadTrafficTrace>(upstrm),
        _trace_test(false), _trace_get(false), _trace_next(false), _trace_eject(false),
        _trace_msg(false), _trace_time(false), _trace_deep(false), _ostrm(&std::cout)
    {
        assert(_upstream);
        for (auto & o : options) {
            if      (o == "test"   ) _trace_test  = true;
            else if (o == "get"    ) _trace_get   = true;
            else if (o == "next"   ) _trace_next  = true;
            else if (o == "eject"  ) _trace_eject = true;
            else if (o == "message") _trace_msg   = true;
            else if (o == "time"   ) _trace_time  = true;
            else if (o == "deep"   ) _trace_deep  = true;
            else if (o == "file"   ) _ostrm = _get_trace_file();
            else {
                std::cerr << "bad trace option: " << o << std::endl;
                ::exit(-2);
            }
        }
    }

    void Init(int pes, Configuration const * const config)
    {   _upstream->Init(pes, config);   }

    bool test(int src) {
        if (_trace_test) *_ostrm << _get_time_str() << "test(" << src << ")";
        auto r = _upstream->test(src);
        if (_trace_test) *_ostrm << " returning " << r << std::endl;
        return r;
    }

    WorkloadMessagePtr get(int src) {
        if (_trace_get) *_ostrm << _get_time_str() << "get(" << src << ")";
        auto r = _upstream->get(src);
        if (_trace_get) {
            if (_trace_msg) {
                *_ostrm << " returning: ";
                r->Print(*_ostrm, _trace_deep);
            }
            *_ostrm << std::endl;
        }
        return r;
    }

    void next(int src) {
        if (_trace_next) *_ostrm << _get_time_str() << "next(" << src << ")" << std::endl;
        _upstream->next(src);
    }

    void eject(WorkloadMessagePtr m) {
        if (_trace_eject) { // print first, since upstream deletes!
            *_ostrm << _get_time_str() << "eject(";
            if (_trace_msg) m->Print(*_ostrm, _trace_deep);
            *_ostrm << ")" << std::endl;
        }
        _upstream->eject(m);
    }
};

PUBLISH_WORKLOAD_COMPONENT(WorkloadTrafficTrace, "trace");
int WorkloadTrafficTrace::_nextfile = 0;
