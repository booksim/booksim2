/*
 * swm_behavior.cpp
 *
 *  Created on: Jun 14, 2022
 *      Author: cjbeckma
 */
#include "swm.hpp"
#include "random_utils.hpp"
#include "swm_globals.hpp"
#include "swm_shmem.hpp"
#include "swm_libgetput.hpp"
#include "swm_spmat.hpp"
#include "swm_std_options.hpp"

#include <random>
#include <cmath>

//
// a simple SWM behavioral routine example
//
SWM_CLASS(SwmHisto)
{
   SWM_INIT(SwmHisto)

   SwmSpmat spmat;
   SwmLibgetput lgp;
   SwmStdOptions std;

   exponential_distribution<double> * expo;
   uniform_int_distribution<int> * intdist;

   default_random_engine generator;
   int packet_size;
   double last_injection_time;
   double send_interval;

   void behavior(int argc, char * argv[]);

   double gen_interval();
   void send_packets(int num_of_packets, unsigned int run_till, const bool include_itself_target,
           bool streaming);

};
