/*
 * workload component for a generic random all-to-all traffic
 *
 *  * ported for Booksim scalable workload model
 *
 *
 *      Author: Timothy Chong
 */

#include "swm_histo.hpp"

double SwmHisto::gen_interval(){
    double gen = (*expo)(generator);
    return gen;
}

void SwmHisto::send_packets(int num_of_packets, unsigned int run_till, const bool include_itself_target, bool streaming){
    while(num_of_packets && !gShouldSkipDrain) {
        if (streaming || getTime() > this->last_injection_time + this->send_interval){
            this->send_interval = gen_interval();
            this->last_injection_time = getTime();
            if (include_itself_target){
                put(this->packet_size, (*intdist)(generator));
            } else {
                int target = (*intdist)(generator);
                if (target >= _me) target += 1;
                put(this->packet_size, target);
            }
            num_of_packets --;
        } else {
            set_time(ceil(this->last_injection_time + this->send_interval));
        }

        if (lgp.getTime() >= run_till){
            cout << _me <<": drain now" << lgp.getTime() << endl << flush;
            gSimEnabled = false;
            gShouldSkipDrain = true;
        }
    }
}
void SwmHisto::behavior(int argc, char *argv[]){
    lgp.lgp_init(this, _me, _np);
    spmat.init_spmat(&lgp, _me, _np);
    std.init_std_options(&lgp, &spmat, _me, _np);

    const bool include_itself_target = false;

    this->packet_size = getConfig()->GetInt("packet_size");
    //this->work_time = getConfig()->GetInt("swm_work_time");

    cout << "write packet size" << this->packet_size << endl << flush;

    int header_size = 2;
    // In packet_per cycle
    double per_endpoint_bandwdith = 1.0 / (double)(packet_size + header_size);
    //double variance = 1.0 / (double)(packet_size + header_size) / _np * 0.1;

    generator.seed(123 + _me);
    expo = new std::exponential_distribution<double>(per_endpoint_bandwdith);

    if (include_itself_target)
        intdist = new std::uniform_int_distribution<int>(0, _np - 1);
    else
        intdist = new std::uniform_int_distribution<int>(0, _np - 2);

    this->last_injection_time = 0.0;
    put(this->packet_size, (*intdist)(generator));
    this->send_interval = gen_interval();

    //unsigned int start_time = 900000;
    //unsigned int start_time = 900000;
    //unsigned int start_time = 1000;
    //unsigned int run_for = 200000;

   unsigned int start_time = 0;
    unsigned int run_for = 200000000;

    //unsigned int start_time = 100000;
    //unsigned int run_for = 100000;

    gLastClearStatTime = start_time;
    //send_packets(5000, start_time + run_for, include_itself_target);
    //send_packets(10000, start_time + run_for, include_itself_target);

    bool streaming = true;
    send_packets(10000, start_time + run_for, include_itself_target, streaming);

    cout <<  _me << ": Ending Histo application at " << lgp.getTime() << endl;
}
