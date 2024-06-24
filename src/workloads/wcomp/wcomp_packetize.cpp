/*
 * workload component for adding packetization overhead
 *
 * H. Dogan (c) Intel, 2023
 * 
 */

#include "wcomp_packetize.hpp"

/*
 * Packetization -- modifier
 */
int Packetize::_fabric_overhead;
int Packetize::_max_payload;
int Packetize::_min_payload;
float Packetize::_flit_size;

PUBLISH_WORKLOAD_COMPONENT(Packetize, "packetize");

int Packetize::_flits(int bytes)
{
    int num_frames = ceil((float)bytes/(float)_max_payload);
    int size = num_frames * _fabric_overhead + bytes;
    return ceil((float)(size)/(float)_flit_size);
}

Packetize::Packetize(int nodes, const vector<string> &options, Configuration const * const config, WorkloadComponent * upstrm)
   : WComp<Packetize>(upstrm)
{
    if (options.size() != 4) {
        std::cerr << "Usage: packetize(overhead, min_payload, max_payload, flit_size)" << std::endl;
        ::exit(-3);
    }
    _fabric_overhead = std::stoi(options[0]);
    _min_payload = std::stoi(options[1]);
    _max_payload = std::stoi(options[2]);
    _flit_size = std::stof(options[3]);
#if DEBUG > 0
    printf("%d %d %d %f\n", _fabric_overhead, _min_payload, _max_payload, _flit_size);
#endif
}

void Packetize::next(int src)
{
    WorkloadComponent::next(src);
    _upstream->next(src);
}

void Packetize::eject(WorkloadMessagePtr m)
{
    auto msg = dynamic_cast<Message*>(m.get());
    assert(msg);
    _upstream->eject(msg->_contents);
}
