/*
 * swm_behavior.cpp
 *
 *  Created on: Jun 30, 2022
 *      Author: cjbeckma
 */

#ifndef SWM_REDUCTION_H
#define SWM_REDUCTION_H

#include <stdlib.h>
#include <list>
#include "swm.hpp"
#include "swm_shmem.hpp"

//
// SWM behavioral routine - a tree barrier (with static topology)
//
SWM_CLASS(SwmReduction)
{

  SwmShmem so;

  public:
    SWM_INIT(SwmReduction)

    // top-level workload behavior
    //
    // Usage: <work-base> <work-range>
    //
    void behavior(int argc, char * argv[])
    {
        assert(argc == 4);
        int work_base = atoi(argv[1]);
        int work_range = atoi(argv[2]);
        int num_elements = atoi(argv[3]);
        so.shmem_init(this, _me, _np);

        long * target = (long*) so.shmem_malloc(sizeof(long) * num_elements);
        long * source = (long*) so.shmem_malloc(sizeof(long) * num_elements);

        //for (int i=0; true; i++) {
        while (true) {

            SwmMarker(1111);

            int w = work_base + int((double(random()) / RAND_MAX) * work_range);
            DBGPRINT("work = " << w);
            work(w);

            SwmMarker(1212);

            DBGPRINT("reduction");
            so.shmem_long_sum_to_all(target, source, num_elements, 0, 1, _np, NULL, NULL);

            SwmMarker(2222);
        }

        DBGPRINT("DONE");
    }
};

#endif
