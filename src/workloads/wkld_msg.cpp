/*
 * workload message
 *
 * C. Beckmann (c) Intel, 2023
 * H. Dogan
 */
#include <iostream>
#include "config_utils.hpp"
#include "wkld_msg.hpp"

int gInjectorTrafficClass = 0;

vector<int> GeneratorWorkloadMessage::any_overhead;
vector<int> GeneratorWorkloadMessage::read_request_overhead;
vector<int> GeneratorWorkloadMessage::write_request_overhead;
vector<int> GeneratorWorkloadMessage::read_reply_overhead;
vector<int> GeneratorWorkloadMessage::write_reply_overhead;

// initialize int array from config knobs (just like in TrafficManager constructor)
inline void _init_array(vector<int>& arr, Configuration const *config, const char *name, int tcls)
{
    // not initialized? try parsing knob string as a list
    if (arr.empty()) {
        arr = config->GetIntArray(name);
        // knob string is a single value? copy it to all array elements
        if (arr.empty()) arr.resize(tcls+1, config->GetInt(name));
    }
    // array still not big enough?  Copy last element to the end
    if ((int)arr.size() <= tcls) arr.resize(tcls+1, arr.back());
}

// get default message size adders from Booksim knobs
GeneratorWorkloadMessage::Factory::Factory(Configuration const * config, int tcls)
  : traffic_class(tcls)
{
    _init_array(any_overhead,           config, "packet_size",        tcls);
    _init_array(read_request_overhead,  config, "read_request_size",  tcls);
    _init_array(write_request_overhead, config, "write_request_size", tcls);
    _init_array(read_reply_overhead,    config, "read_reply_size",    tcls);
    _init_array(write_reply_overhead,   config, "write_reply_size",   tcls);
}

std::ostream & WorkloadMessage::Print(std::ostream & os, bool deep) const
{
    static const char *type2str[] = {"MSG", "GET", "PUT", "nbGET", "SEND", "RECV", "(dummy)"};
    os << Source() << "->" << Dest() << " "
       << type2str[(int)Type()] << (IsReply() ? " (reply)" : "")
       << " [" << Size() << "]";
    WorkloadMessagePtr c;
    if (deep && (c = Contents()).get()) {
        os << " {";
        c->Print(os, deep);
        os << '}';
    }
    return os;
}
