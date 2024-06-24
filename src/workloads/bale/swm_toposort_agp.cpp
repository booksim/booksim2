/*
 * swm_toposort_agp.cpp
 *
 * toposort agp implementation from Bale benchmark suite
 * for Booksim scalable workload model
 *
 *  Created on: Dec 24, 2022
 *      Author: hdogan
*/



/******************************************************************
//
//
//  Copyright(C) 2020, Institute for Defense Analyses
//  4850 Mark Center Drive, Alexandria, VA; 703-845-2500
//
//
//  All rights reserved.
//
//   This file is a part of Bale.  For license information see the
//   LICENSE file in the top level directory of the distribution.
//
//
 *****************************************************************/
/*! \file toposort_agp.upc
 * \brief The intuitive implementation of toposort that does uses
 * atomics and generic global references
 */

#include "swm_toposort.hpp"

/*!
 * \brief This routine implements the AGP variant of toposort
 * \param *rperm returns the row permutation that is found
 * \param *cperm returns the column permutation that is found
 * \param *mat the input sparse matrix NB. it must be a permuted upper triangular matrix
 * \param *tmat the transpose of mat
 * \return average run time
 */
double SwmToposort::toposort_matrix_agp(SHARED int64_t *rperm, SHARED int64_t *cperm, sparsemat_t *mat, sparsemat_t *tmat) {
   //T0_printf("Running Toposort with UPC ...");
   int64_t nr = mat->numrows;

   assert(mat->numrows == mat->numcols);
   //int64_t col2;

   //lgp.lgp_barrier();


   SHARED int64_t * queue  = (int64_t *) lgp.lgp_all_alloc(nr+THREADS, sizeof(int64_t));
   int64_t * lqueue  = lgp_local_part(int64_t, queue);
   //lgp.getShmem()->getSimThread()->YIELD();
   SHARED int64_t * rowsum = (int64_t *) lgp.lgp_all_alloc(nr+THREADS, sizeof(int64_t));
   int64_t *lrowsum = lgp_local_part(int64_t, rowsum);
   //lgp.getShmem()->getSimThread()->YIELD();
   SHARED int64_t * rowcnt = (int64_t *) lgp.lgp_all_alloc(nr+THREADS, sizeof(int64_t));
   int64_t *lrowcnt = lgp_local_part(int64_t, rowcnt);
   //lgp.getShmem()->getSimThread()->YIELD();

   SHARED int64_t * S_end = (int64_t *) lgp.lgp_all_alloc(THREADS, sizeof(int64_t));
   int64_t *lS_end = lgp_local_part(int64_t, S_end);
   //lgp.getShmem()->getSimThread()->YIELD();
   int64_t start, end;

   SHARED int64_t * pivots = (int64_t *) lgp.lgp_all_alloc(THREADS, sizeof(int64_t));
   int64_t *lpivots = lgp_local_part(int64_t, pivots);
   //lgp.getShmem()->getSimThread()->YIELD();
   lpivots[0] = 0L;
   int64_t i, j;

   printf("[%d] allocations are done\n", _me);

   /* initialize rowsum, rowcnt, and queue (queue holds degree one rows) */
   start = end = 0;
   for(i = 0; i < mat->lnumrows; i++){
      lrowsum[i] = 0L;
      lrowcnt[i] = mat->loffset[i+1] - mat->loffset[i];
      if(lrowcnt[i] == 1)
         lqueue[end++] = i;
      for(j = mat->loffset[i]; j < mat->loffset[i+1]; j++)
         lrowsum[i] += mat->lnonzero[j];
      lgp.getShmem()->getSimThread()->YIELD();
      //printf("[%d] %d %ld\n", _me, i, get_time());
   }

   //printf("[%d] I am here \n", _me);

   //S_end[MYTHREAD] = end;
   lgp.lgp_barrier();
   lS_end[0] = end;

   if(!MYTHREAD) printf("initialization is done\n");


   // we a pick a row with a single nonzero = col.
   // setting rperm[row] = nr-1-pos and cprem[col] = nr-1-pos
   // moves that nonzero to the diagonal.
   // Now, cross out that row and col by decrementing
   //  the rowcnt for any row that contains that column
   // repeat


   // Do it in levels:
   // Per level -- everyone picks their degree one rows
   //              and claims their spots in the permutations,
   //              then they all go back and update the
   //              appropriate rowcnts for that level
   //
   // cool trick: rowsum[i] is the sum of all the column indices in row i,
   //             so rowsum[i] is the column indice we want when rowcnt = 1;

   //double t1 = wall_seconds();


   //printf("[%d] begin lgp_reduce_add_l, %ld %ld\n", MYTHREAD, end-start, lgp.getShmem()->getNextIndex());
   //lgp.lgp_barrier();
   int work_to_do = lgp.lgp_reduce_add_l(end - start);
   printf("[%d] return %d\n", MYTHREAD, work_to_do);
   //T0_printf("done\n"); fflush(0);
   //lgp.lgp_barrier();

   //printf("[%d] end lgp_reduce_add_l, %ld\n", MYTHREAD, end);

   int64_t pos, l_row, S_col, S_row, S_indx, colcount, level = 0;
   //int64_t old_row_sum, old_row_cnt, l_pos;
   int64_t old_row_cnt, l_pos;

   if(!MYTHREAD) printf("work loop\n");

   while(work_to_do) {
      level++;
      printf("start %ld end %ld\n", start, end);
      while( start < end ){
         l_row = lqueue[start++];
         S_col = lrowsum[l_row];  // see cool trick

         // claim our spot on the diag
         pos = lgp.lgp_fetch_and_inc(pivots, 0);
         printf("[%d] pos %ld\n", MYTHREAD, pos);
         lgp.lgp_put_int64(rperm, l_row*THREADS + MYTHREAD, nr - 1 - pos);
         lgp.lgp_put_int64(cperm, S_col, nr - 1 - pos);

         // use the global version of tmat to look at this column (tmat's row)
         // tmat->offset[S_col] is the offset local to S_col%THREADS
         S_indx = lgp.lgp_get_int64(tmat->offset, S_col) * THREADS + S_col % THREADS;
         colcount = lgp.lgp_get_int64(tmat->offset, S_col+THREADS) - lgp.lgp_get_int64(tmat->offset,S_col);
         for(j=0; j < colcount; j++) {
            S_row = lgp.lgp_get_int64(tmat->nonzero, S_indx + j*THREADS );

            assert((S_row) < mat->numrows);
            old_row_cnt = lgp.lgp_fetch_and_add(rowcnt, S_row, -1L);
            // old_row_sum = lgp.lgp_fetch_and_add(rowsum, S_row, (-1L)*S_col);
            if( old_row_cnt == 2L ) {
               l_pos = lgp.lgp_fetch_and_inc(S_end, S_row % THREADS);
               lgp.lgp_put_int64(queue, l_pos*THREADS + S_row%THREADS , S_row / THREADS);fflush(0);
            }
         }
      }
      lgp.lgp_barrier();
      assert( lS_end[0] >= end );
      end = lS_end[0];
      printf("[%d] level %ld\n", _me, level);
      work_to_do = lgp.lgp_reduce_add_l(end - start);
   }

   level = lgp.lgp_reduce_max_l(level);

   //minavgmaxD_t stat[1];
   //t1 = wall_seconds() - t1;
   //lgp.lgp_min_avg_max_d( stat, t1, THREADS );


   if(lgp.lgp_get_int64(pivots,0) != nr){
      printf("ERROR! toposort_matrix_upc_orig: found %" PRId64 " pivots but expected %" PRId64 "!\n", lgp.lgp_get_int64(pivots, 0), nr);
      exit(1);
   }
   T0_fprintf(stderr, "num levels = %ld ", level+1);
   lgp.lgp_all_free(queue);
   lgp.lgp_all_free(rowsum);
   lgp.lgp_all_free(rowcnt);
   lgp.lgp_all_free(S_end);
   lgp.lgp_all_free(pivots);
   return(0.0);
   //return(stat->avg);
}
