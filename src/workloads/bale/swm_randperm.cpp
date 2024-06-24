/*
 * swm_randperm.cpp
 *
 * Randperm kernel from Bale benchmark suite
 *
 *  Created on: Dec 14, 2022
 *      Author: hdogan
 */
#include <getopt.h>
#include "swm_randperm.hpp"

#define REGION1_START 1111
#define REGION1_END   2222

#define REGION2_START 3333
#define REGION2_END   4444

typedef struct args_t{
  int64_t num_rows;       /*!< total number of elements in the shared permutation */
  int64_t l_num_rows;     /*!< per thread number of elements in the shared permutation */
  std_args_t std;
}args_t;


static int parse_opt(int key, char * arg, struct argp_state * state){
   args_t * args = (args_t *)state->input;
   switch(key)
   {
      case 'N':
         args->num_rows = atol(arg); break;
      case 'n':
         args->l_num_rows = atol(arg); break;
      case ARGP_KEY_INIT:
         state->child_inputs[0] = &args->std;
         break;
   }
   return(0);
}

static struct argp_option options[] =
  {
    {"perm_size",'N', "NUM", 0, "Total length of permutation", 0},
    {"l_perm_size",'n', "NUM", 0, "Per PE length of permutation", 0},
    {0,0,0,0,0,0}
  };

static struct argp_child children_parsers[] =
  {
    {&std_options_argp, 0, "Standard Options", -2},
    {0,0,0,0}
  };

static int total_put = 0, total_pe = 0;


void SwmRandPerm::behavior(int argc, char * argv[])
{

   /******************************************************************************/
   /*! \brief create a global int64_t array with a uniform random permutation
    * \param N the length of the global array
    * \param seed seed for the random number generator
    * \return the permutation
    *
    * This is a collective call.
    * this implements the random dart algorithm to generate the permutation.
    * Each thread throws its elements of the perm array randomly at large target array.
    * Each element claims a unique entry in the large array using compare_and_swap.
    * This gives a random permutation with spaces in it, then you squeeze out the spaces.
    * \ingroup spmatgrp
    */
   int64_t * ltarget;
   int64_t r, i;
   int64_t pos, numdarts;
   const int64_t seed = 122222;


   // Init objects
   lgp.lgp_init(this, _me, _np);
   spmat.init_spmat(&lgp, _me, _np);
   std.init_std_options(&lgp, &spmat, _me, _np);

   args_t args;  // initialize args struct to all zero
   args.num_rows = 0;
   args.l_num_rows = 1000;
   struct argp argp = {options, parse_opt, 0,
      "Parallel permute sparse matrix.", children_parsers, 0, 0};

   //int ret = std.bale_app_init(argc, argv, &args, sizeof(args_t), &argp, &args.std);
   std.bale_app_init(argc, argv, &args, sizeof(args_t), &argp, &args.std);

   if(args.num_rows == 0)
      args.num_rows = args.l_num_rows * _np;
   else
      args.l_num_rows = (args.num_rows + _np - _me - 1)/_np;

   int64_t N = args.num_rows;

   lgp.lgp_barrier();

   lgp.lgp_rand_seed(seed);

   int64_t * perm = (int64_t*)lgp.lgp_all_alloc(N, sizeof(int64_t));
   if( perm == NULL ) return;

   int64_t l_N = (N + _np - _me - 1)/_np;
   int64_t M = 2*N;
   int64_t l_M = (M + _np - _me - 1)/_np;

   int64_t * target = (int64_t*)lgp.lgp_all_alloc(M, sizeof(int64_t));
   if( target == NULL ) return;
   ltarget = target;

   if(_me == 0)
      printf("Init ltarget\n");

   for(i=0; i<l_M; i++)
      ltarget[i] = -1;

   lgp.lgp_barrier();

   SwmMarker(REGION1_START);

   if(_me == 0)
      printf("Start throwing darts\n");


   i=0, r=0;
   while(i < l_N) {                // throw the darts until you get l_N hits
      r = lgp.lgp_rand_int64(M);   // rand() % M;
      if( lgp.lgp_cmp_and_swap(target, r, -1L, (i*_np + _me)) == (-1L) ){
         i++;
      }
   }

   if(_me == 0)
      printf("Done with throwing darts\n");

   SwmMarker(REGION1_END);

   lgp.lgp_barrier();


   numdarts = 0;
   for(i = 0; i < l_M; i++)    // count how many darts I got
      numdarts += (ltarget[i] != -1L );


   pos = lgp.lgp_prior_add_l(numdarts);    // my first index in the perm array is the number

   SwmMarker(REGION2_START);

   if(_me == 0)
      printf("Done with reduction\n");


   // of elements produce by the smaller threads
   for(i = 0; i < l_M; i++){
      if(ltarget[i] != -1L ) {
         lgp.lgp_put_int64(perm, pos, ltarget[i]);
         pos++;
         total_put++;
      }
   }

   total_pe++;
   if (total_pe == _np)
     printf("\nT=%d Randperm total_put=%d l_M=%ld\n",GetSimTime(),total_put,l_M);

   SwmMarker(REGION2_END);

   //lgp.lgp_barrier();

   if(_me == 0)
      printf("Application is completed\n");

   DBGPRINT("DONE");

}
