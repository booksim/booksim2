/*
 * Random number workload traffic generator, using Booksim's built-in injection process and traffic patterns
 *
 * C. Beckmann (c) Intel, 2023
 */
#include "wkld_comp.hpp"
#include "injection.hpp"
#include "traffic.hpp"

class RandomWorkloadGenerator : public WComp<RandomWorkloadGenerator>,
                                private GeneratorWorkloadMessage::Factory
{
    double const _wr_fraction;               // copy of key Booksim knobs
    bool   const _use_rdwr;

    string             _injection_process;
    InjectionProcess * _inject;              // Booksim injection process
    TrafficPattern *   _pattern;             // Booksim traffic pattern

  public:
    // Options are:
    //   injection process (same options as booksim knob injection_process)
    // Also uses booksim knobs:
    //   traffic, injection_rate, injection_rate_uses_flits (must be 0!), use_read_write, write_fraction,
    //   packet_size, read_request_size, read_reply_size, write_request_size, write_reply_size
    // Assumes these are all scalars (for now), i.e. there is only one traffic class.
    // Also, there is currently no support for packet_size_rate.
    RandomWorkloadGenerator(int nodes, const vector<string> &options, Configuration const * config, WorkloadComponent *upstrm);
    void Init(int pes, Configuration const *config);
    bool test(int src) { return _inject->test(src); }
  private:
    WorkloadMessagePtr _get_new(int src);
};
