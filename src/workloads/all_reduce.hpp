/*
 * all_reduce.cpp
 *
 *  Created on: Feb 7, 2024
 *      Author: Timothy Chong
 */

#ifndef SWM_ALL_REDUCE_H
#define SWM_ALL_REDUCE_H

#include <stdlib.h>
#include <list>
#include "swm.hpp"
#include "swm_shmem.hpp"
#include "piecewise_linear_function.hpp"


//
// SWM behavioral routine - a tree barrier (with static topology)
//
SWM_CLASS(SwmAllReduce)
{

  SwmShmem so;

  public:
    SWM_INIT(SwmAllReduce)

    // top-level workload behavior
    //
    // Usage: <work-base> <work-range>
    //
    void behavior(int argc, char * argv[])
    {


        so.shmem_init(this, _me, _np);

        // 1 flit = 32B
        // 1 cycle = 2.5ns
        // 60M float parameters
        // Starting at 500K bucket onwards, it's around 250ms for total time
        // Total backward propagation time: ~250ms
        // 1 - 854
        // forward - 129 (15%)
        // backward 337 (39%)
        // comm 377  (44%)
        // opt 17 (2%)
        // overlap - 391 (46%)

        //double compute_backprop_time = 1e8;
        //double forward_pass_time = compute_backprop_time / (39) * 15;
        //double forward_pass_time = 100;

        // Amount of time to finish computing each bucket
        // total height 742 (100%)
        // 0.75: 8
        // 1.5: 9
        // 2.25: 11
        // 3: 80
        // 3.75: 208
        // 4.5: 341
        // 5.25: 467
        // 6: 742

        PiecewiseLinearFunction plf;
        // 1 Parameter = 32 bit = 4byte = 1/8 flit

        // ( parameters / 8, us * 1000 / 2.5) // These numbers are estimated from distributed pytorch paper
        // ( # of flits , cycles )

        //plf.addPoint(0, 0);
        //plf.addPoint(5000, 10000);
        //plf.addPoint(10000, 20000);
        plf.addPoint(2.7187e7 / 8, 8.056   * 1000 / 2.5);
        plf.addPoint(5.755e7  / 8, 172.391 * 1000 / 2.5);
        plf.addPoint(6.01e7   / 8, 240     * 1000 / 2.5);


        cout << "Starting app" << endl;

        int chunk_size = 781250; // 25MB
        //int num_of_parameters_byte = 6e7 * 4; // 60M parameters, 4byte each
        int num_of_parameters_byte = 781250 * 320; // 60M parameters, 4byte each
        int num_of_parameters_flit = num_of_parameters_byte / 32; // Num of flits
                                 //
        //int num_of_parameters_flit = 1000; // Num of flits
        //int chunk_size = 1000; // 25MB

        //for(int i = 0; i < 1; i++){
            //// Forward pass
            //cout << fixed;
            //cout << _me << "Time: " << getTime() << " Starting forward till " <<  getTime() + forward_pass_time << endl;
            //work((int) forward_pass_time);
            //
        chunk_size = getConfig()->GetInt("packet_size");
        num_of_parameters_flit = chunk_size;

        unsigned int start_time = 0;
        gLastClearStatTime = start_time;

        // Backward pass
        int j = 0;
        for (; j < num_of_parameters_flit; j += chunk_size) {
            int finish_time = plf.getValue(j);
            //if (getTime() < (unsigned int) finish_time) {
                //cout << _me << "Time: " << getTime() << " Forwarding time to " << finish_time << endl;
                //set_time(finish_time);
            //} else {
                // Calculate the
                cout << _me << "Time: " << getTime() << " Starting backward Bucket work" << j << endl;

                const int PE_start = 0;
                const int logPE_stride = 1;
                cout << _me << " Time: " << getTime() << " Put backward Bucket work starting communication" << j << endl;
                so.shmem_internal_op_to_all(NULL, NULL, _np, chunk_size, PE_start, logPE_stride, _np, NULL, NULL);
                cout << _me << " Time: " << getTime() << " Finish backward Bucket work starting communication" << j << endl;
            }
        //}
        //if (j < num_of_parameters_flit) {
            //cout << "Time: " << getTime() << " Final batch " << endl;
            //int finish_time = plf.getValue(num_of_parameters_flit);
            //if (getTime() < (unsigned int) finish_time) {
                //cout << "Time: " << getTime() << " Forwarding time to " << finish_time << endl;
                //set_time(finish_time);
            //} else {
                //const int PE_start = 0;
                //const int logPE_stride = 1;
                //so.shmem_internal_op_to_all(NULL, NULL, _np, chunk_size, PE_start, logPE_stride, _np, NULL, NULL);
                //cout << _me << "Time: " << getTime() << " Finish backward Bucket work starting communication" << j << endl;
            //}
        //}
        //}

        DBGPRINT("DONE");
    }
};

#endif
