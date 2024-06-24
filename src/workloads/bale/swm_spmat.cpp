/*
 * swm_spmat.cpp
 *
 * SPMAT library implementation from Bale benchmark suite
 * for Booksim scalable workload model
 *
 *  Created on: Dec 24, 2022
 *      Author: hdogan
*/


/*****************************************************************
//
//
//  Copyright(C) 2020, Institute for Defense Analyses
//  4850 Mark Center Drive, Alexandria, VA; 703-845-2500
//
//  All rights reserved.
//
//  This file is a part of Bale.  For license information see the
//  LICENSE file in the top level directory of the distribution.
//
 *****************************************************************/
/*! \file spmat_agp.upc
 * \brief Sparse matrix support functions implemented with global addresses and atomics
 */
#include <sys/stat.h>   // for mkdir()
#include <fcntl.h>
#include "swm_spmat.hpp"
#include "swm_std_options.hpp"
//#include "spmat.h"
//
//
//
//


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
SHARED int64_t * SwmSpmat::rand_permp_agp(int64_t N, int seed) {
  int64_t * ltarget;
  int64_t r, i;
  int64_t pos, numdarts;

  _lgp->lgp_rand_seed(seed);

  T0_printf("Entering rand_permp_atomic...\n");fflush(0);

  SHARED int64_t * perm = (int64_t*)_lgp->lgp_all_alloc(N, sizeof(int64_t));
  if( perm == NULL ) return(NULL);
   lgp_local_part(int64_t, perm);

  int64_t l_N = (N + THREADS - MYTHREAD - 1)/THREADS;
  int64_t M = 2*N;
  int64_t l_M = (M + THREADS - MYTHREAD - 1)/THREADS;

  SHARED int64_t * target = (int64_t*)_lgp->lgp_all_alloc(M, sizeof(int64_t));
  if( target == NULL ) return(NULL);
  ltarget = lgp_local_part(int64_t, target);

  for(i=0; i<l_M; i++)
    ltarget[i] = -1L;
  _lgp->lgp_barrier();

  //T0_printf("Entering rand_permp_atomic...");fflush(0);

  i=0;
  while(i < l_N){                // throw the darts until you get l_N hits
    r = _lgp->lgp_rand_int64(M);
    if( _lgp->lgp_cmp_and_swap(target, r, -1L, (i*THREADS + MYTHREAD)) == (-1L) ){
      i++;
    }
  }
  _lgp->lgp_barrier();

  numdarts = 0;
  for(i = 0; i < l_M; i++)    // count how many darts I got
    numdarts += (ltarget[i] != -1L );


  pos = _lgp->lgp_prior_add_l(numdarts);    // my first index in the perm array is the number

                                      // of elements produce by the smaller threads
  for(i = 0; i < l_M; i++){
    if(ltarget[i] != -1L ) {
       _lgp->lgp_put_int64(perm, pos, ltarget[i]);
       pos++;
    }
  }

  _lgp->lgp_all_free(target);
  _lgp->lgp_barrier();
  T0_printf("done!\n"); fflush(0);
  return(perm);
}

/*! \brief apply row and column permutations to a sparse matrix using straight UPC
 * \param A pointer to the original matrix
 * \param rperm pointer to the global array holding the row permutation
 * \param cperm pointer to the global array holding the column permutation
 * rperm[i] = j means that row i of A goes to row j in matrix Ap
 * cperm[i] = j means that col i of A goes to col j in matrix Ap
 * \return a pointer to the matrix that has been produced or NULL if the model can't be used
 * \ingroup spmatgrp
 */
sparsemat_t * SwmSpmat::permute_matrix_agp_nbi(sparsemat_t *A, SHARED int64_t *rperm, SHARED int64_t *cperm) {
  //T0_printf("Permuting matrix with single puts\n");
  int weighted = (A->value != NULL);
  //int64_t i, j, col, row, pos;
  int64_t i, j, row, pos;
  int64_t * lrperm = lgp_local_part(int64_t, rperm);
  SHARED int64_t * rperminv = (int64_t*)_lgp->lgp_all_alloc(A->numrows, sizeof(int64_t));
  if( rperminv == NULL ) return(NULL);
  int64_t *lrperminv = lgp_local_part(int64_t, rperminv);

  _lgp->lgp_barrier();

  _lgp->getShmem()->getSimThread()->SwmMarker(1111);

  T0_fprintf(stderr, "start rperminv\n");
  //compute rperminv from rperm
  for(i=0; i < A->lnumrows; i++){
    _lgp->getShmem()->getSimThread()->WORK(0);
    _lgp->lgp_put_int64(rperminv, lrperm[i], i*THREADS + MYTHREAD);
  }

  _lgp->getShmem()->getSimThread()->SwmEndMarker(2222);

  T0_fprintf(stderr, "printing stats for rperminv\n");
  _lgp->lgp_barrier();
  T0_fprintf(stderr, "done rperminv\n");

  _lgp->getShmem()->getSimThread()->SwmMarker(3333);

  T0_fprintf(stderr, "start cnt\n");
  int64_t cnt = 0; // off, nxtoff;
  int64_t off[A->lnumrows];
  int64_t nxtoff[A->lnumrows];
  for(i = 0; i < A->lnumrows; i++){
    row = lrperminv[i];
    off[i]    = _lgp->lgp_get_int64_nbi(A->offset, row);
    nxtoff[i] = _lgp->lgp_get_int64_nbi(A->offset, row + THREADS);
    //cnt += nxtoff - off;
  }
  _lgp->lgp_fence();

  for(i = 0; i < A->lnumrows; i++){
     cnt += nxtoff[i] - off[i];
  }

  T0_fprintf(stderr, "printing stats for cnt phase\n");

  _lgp->getShmem()->getSimThread()->SwmEndMarker(4444);

  _lgp->lgp_barrier();
  T0_fprintf(stderr, "done cnt\n");

  sparsemat_t * Ap = init_matrix(A->numrows, A->numcols, cnt, weighted);
  assert(Ap != NULL); //For coverity static analysis

  T0_fprintf(stderr, "done init_matrix\n");

  _lgp->getShmem()->getSimThread()->SwmMarker(5555);
  // fill in permuted rows
  Ap->loffset[0] = pos = 0;
  for(i = 0; i < Ap->lnumrows; i++){
    row = lrperminv[i];
    //off    = _lgp->lgp_get_int64(A->offset, row);
    //nxtoff = _lgp->lgp_get_int64(A->offset, row + THREADS);
    _lgp->lgp_memget_nbi<int64_t>(&Ap->lnonzero[pos], A->nonzero,
               (nxtoff[i]-off[i])*sizeof(int64_t), off[i]*THREADS + row%THREADS);
    if(weighted)
      _lgp->lgp_memget<double>(&Ap->lvalue[pos], A->value,
                 (nxtoff[i]-off[i])*sizeof(double), off[i]*THREADS + row%THREADS);
    pos += nxtoff[i] - off[i];
    //for(j = off; j < nxtoff; j++){
    //Ap->lnonzero[pos++] = lgp_get_int64(A->nonzero, j*THREADS + row%THREADS);
    //}
    Ap->loffset[i+1] = pos;
    //printf("[%d] cnt %d lnonzero[%d]=%d loffset[%d]=%d\n", MYTHREAD, nxtoff[i] - off[i], pos, Ap->lnonzero[pos],i+1,Ap->loffset[i+1]);
  }
  _lgp->lgp_fence();

  _lgp->getShmem()->getSimThread()->SwmEndMarker(6666);

  assert(pos == cnt);

  _lgp->lgp_barrier();

  _lgp->getShmem()->getSimThread()->SwmMarker(7777);

  T0_fprintf(stderr, "done with the main loop\n");

  // finally permute column indices
  for(i = 0; i < Ap->lnumrows; i++){
    for(j = Ap->loffset[i]; j < Ap->loffset[i+1]; j++){
      Ap->lnonzero[j] = _lgp->lgp_get_int64_nbi(cperm, Ap->lnonzero[j]);
    }
  }
  _lgp->lgp_fence();
  _lgp->getShmem()->getSimThread()->SwmEndMarker(8888);
  T0_fprintf(stderr, "done with the last loop\n");
  _lgp->lgp_barrier();

  _lgp->lgp_all_free(rperminv);
  sort_nonzeros(Ap);

  return(Ap);
}

/*! \brief apply row and column permutations to a sparse matrix using straight UPC
 * \param A pointer to the original matrix
 * \param rperm pointer to the global array holding the row permutation
 * \param cperm pointer to the global array holding the column permutation
 * rperm[i] = j means that row i of A goes to row j in matrix Ap
 * cperm[i] = j means that col i of A goes to col j in matrix Ap
 * \return a pointer to the matrix that has been produced or NULL if the model can't be used
 * \ingroup spmatgrp
 */
sparsemat_t * SwmSpmat::permute_matrix_agp(sparsemat_t *A, SHARED int64_t *rperm, SHARED int64_t *cperm) {
  //T0_printf("Permuting matrix with single puts\n");
  int weighted = (A->value != NULL);
  //int64_t i, j, col, row, pos;
  int64_t i, j, row, pos;
  int64_t * lrperm = lgp_local_part(int64_t, rperm);
  SHARED int64_t * rperminv = (int64_t*)_lgp->lgp_all_alloc(A->numrows, sizeof(int64_t));
  if( rperminv == NULL ) return(NULL);
  int64_t *lrperminv = lgp_local_part(int64_t, rperminv);

  _lgp->lgp_barrier();

  _lgp->getShmem()->getSimThread()->SwmMarker(1111);

  T0_fprintf(stderr, "start rperminv\n");
  //compute rperminv from rperm
  for(i=0; i < A->lnumrows; i++){
    _lgp->getShmem()->getSimThread()->WORK(0);
    _lgp->lgp_put_int64(rperminv, lrperm[i], i*THREADS + MYTHREAD);
  }

  _lgp->getShmem()->getSimThread()->SwmEndMarker(2222);

  T0_fprintf(stderr, "printing stats for rperminv\n");
  _lgp->lgp_barrier();
  T0_fprintf(stderr, "done rperminv\n");

  _lgp->getShmem()->getSimThread()->SwmMarker(3333);

  T0_fprintf(stderr, "start cnt\n");
  int64_t cnt = 0, off, nxtoff;
  for(i = 0; i < A->lnumrows; i++){
    row = lrperminv[i];
    off    = _lgp->lgp_get_int64(A->offset, row);
    nxtoff = _lgp->lgp_get_int64(A->offset, row + THREADS);
    cnt += nxtoff - off;
  }
  T0_fprintf(stderr, "printing stats for cnt phase\n");

  _lgp->getShmem()->getSimThread()->SwmEndMarker(4444);

  _lgp->lgp_barrier();
  T0_fprintf(stderr, "done cnt\n");

  sparsemat_t * Ap = init_matrix(A->numrows, A->numcols, cnt, weighted);
  assert(Ap != NULL);

  T0_fprintf(stderr, "done init_matrix\n");

  _lgp->getShmem()->getSimThread()->SwmMarker(5555);
  // fill in permuted rows
  Ap->loffset[0] = pos = 0;
  for(i = 0; i < Ap->lnumrows; i++){
    row = lrperminv[i];
    off    = _lgp->lgp_get_int64(A->offset, row);
    nxtoff = _lgp->lgp_get_int64(A->offset, row + THREADS);
    _lgp->lgp_memget<int64_t>(&Ap->lnonzero[pos], A->nonzero,
               (nxtoff-off)*sizeof(int64_t), off*THREADS + row%THREADS);
    if(weighted)
      _lgp->lgp_memget<double>(&Ap->lvalue[pos], A->value,
                 (nxtoff-off)*sizeof(double), off*THREADS + row%THREADS);
    pos += nxtoff - off;
    //for(j = off; j < nxtoff; j++){
    //Ap->lnonzero[pos++] = lgp_get_int64(A->nonzero, j*THREADS + row%THREADS);
    //}
    Ap->loffset[i+1] = pos;
  }

  _lgp->getShmem()->getSimThread()->SwmEndMarker(6666);

  assert(pos == cnt);

  T0_fprintf(stderr, "printing stats for the main loop\n");
  _lgp->lgp_barrier();

  _lgp->getShmem()->getSimThread()->SwmMarker(7777);

  T0_fprintf(stderr, "done with the main loop\n");

  // finally permute column indices
  for(i = 0; i < Ap->lnumrows; i++){
    for(j = Ap->loffset[i]; j < Ap->loffset[i+1]; j++){
      Ap->lnonzero[j] = _lgp->lgp_get_int64(cperm, Ap->lnonzero[j]);
    }
  }
  T0_fprintf(stderr, "printing stats for the last loop\n");
  _lgp->getShmem()->getSimThread()->SwmEndMarker(8888);
  T0_fprintf(stderr, "done with the last loop\n");
  _lgp->lgp_barrier();

  _lgp->lgp_all_free(rperminv);
  sort_nonzeros(Ap);

  return(Ap);
}


/*! \brief produce the transpose of a sparse matrix using UPC
 * \param A  pointer to the original matrix
 * \return a pointer to the matrix that has been produced or NULL if the model can't be used
 * \ingroup spmatgrp
 */
sparsemat_t * SwmSpmat::transpose_matrix_agp_nbi(sparsemat_t *A) {
  //int64_t lnnz, i, j, col, row, fromth, idx;
  int64_t lnnz, i, j, row;
  int64_t pos;
  sparsemat_t * At;

  //T0_printf("UPC version of matrix transpose...");

  // find the number of nnz.s per thread

  SHARED int64_t * shtmp = (int64_t*)_lgp->lgp_all_alloc( A->numcols + THREADS, sizeof(int64_t));
  if( shtmp == NULL ) return(NULL);
  int64_t * l_shtmp = lgp_local_part(int64_t, shtmp);


  int64_t lnc = (A->numcols + THREADS - MYTHREAD - 1)/THREADS;
  for(i=0; i < lnc; i++) {
    l_shtmp[i] = 0;
  }

  T0_fprintf(stderr, "printing stats for initialization\n");
   //_lgp->getShmem()->getSimThread()->clear_stats();
  _lgp->lgp_barrier();

  T0_fprintf(stderr, "Histogram is started...\n");


  _lgp->getShmem()->getSimThread()->SwmMarker(1111);

  int64_t histo_loop_start = _lgp->getShmem()->getTime();
  for( i=0; i< A->lnnz; i++) {                   // histogram the column counts of A
    assert( A->lnonzero[i] < A->numcols );
    assert( A->lnonzero[i] >= 0 );
    //printf("[%d] %d\n", MYTHREAD, i);
    _lgp->lgp_fetch_and_inc_nbi(shtmp, A->lnonzero[i]);
  }
  _lgp->lgp_fence();
  int64_t hist_loop_stop = _lgp->getShmem()->getTime();

  _lgp->getShmem()->getSimThread()->SwmMarker(2222);

  //lgp.getShmem()->setSimEnabled(false);

  T0_fprintf(stderr, "printing stats for histogram\n");
   //_lgp->getShmem()->getSimThread()->clear_stats();
  _lgp->lgp_barrier();

  T0_fprintf(stderr, "Histogram is done...\n");


  lnnz = 0;
  for( i = 0; i < lnc; i++) {
    lnnz += l_shtmp[i];
    //if(!MYTHREAD) printf("l_shtmp[%d]=%ld\n", i, shtmp[i]);
  }
  int weighted = (A->value != NULL);
  At = init_matrix(A->numcols, A->numrows, lnnz, weighted);
  if(!At){printf("ERROR: transpose_matrix_upc: init_matrix failed!\n");return(NULL);}

  T0_fprintf(stderr, "At initialized...\n");

  //printf("[%d] nnz(%ld) lnnz(%ld)\n", MYTHREAD, A->nnz, lnnz);
  int64_t reduction_start = _lgp->getShmem()->getTime();
  int64_t sum = _lgp->lgp_reduce_add_l(lnnz);      // check the histogram counted everything
  int64_t reduction_stop = _lgp->getShmem()->getTime();
  assert( A->nnz == sum );

  T0_fprintf(stderr, "Reduction is done...\n");

  // compute the local offsets
  At->loffset[0] = 0;
  for(i = 1; i < At->lnumrows+1; i++)
    At->loffset[i] = At->loffset[i-1] + l_shtmp[i-1];

  // get the global indices of the start of each row of At
  for(i = 0; i < At->lnumrows; i++)
    l_shtmp[i] = MYTHREAD + THREADS * (At->loffset[i]);

  T0_fprintf(stderr, "printing stats reduction\n");
   //_lgp->getShmem()->getSimThread()->clear_stats();
  _lgp->lgp_barrier();

  T0_fprintf(stderr, "Redistribute the nonzeros...\n");
  _lgp->getShmem()->getSimThread()->SwmMarker(3333);

  int64_t redistribute_start = _lgp->getShmem()->getTime();
  //redistribute the nonzeros
  for(row=0; row<A->lnumrows; row++) {
    for(j=A->loffset[row]; j<A->loffset[row+1]; j++){
      pos = _lgp->lgp_fetch_and_add(shtmp, A->lnonzero[j], (int64_t) THREADS);
      _lgp->lgp_put_int64(At->nonzero, pos, row*THREADS + MYTHREAD);
      if(weighted) _lgp->lgp_put_double(At->value, pos, A->lvalue[j]);
    }
  }
  _lgp->getShmem()->getSimThread()->SwmMarker(4444);
  int64_t redistribute_stop = _lgp->getShmem()->getTime();

  T0_fprintf(stderr, "printing stats main loop\n");
  //_lgp->getShmem()->getSimThread()->clear_stats();
  _lgp->lgp_barrier();
  if(!MYTHREAD)printf("done\n");
  _lgp->lgp_all_free(shtmp);

  int64_t max_histo_loop_time = _lgp->lgp_reduce_max_l(hist_loop_stop-histo_loop_start);
  int64_t max_reduction_time = _lgp->lgp_reduce_max_l(reduction_stop-reduction_start);
  int64_t max_redistribute_time = _lgp->lgp_reduce_max_l(redistribute_stop-redistribute_start);
  T0_fprintf(stderr, "[%d] histo_time %ld, reduction_time %ld, redistribution_time %ld\n", MYTHREAD,max_histo_loop_time,max_reduction_time,max_redistribute_time);

  return(At);
}


/*! \brief produce the transpose of a sparse matrix using UPC
 * \param A  pointer to the original matrix
 * \return a pointer to the matrix that has been produced or NULL if the model can't be used
 * \ingroup spmatgrp
 */
sparsemat_t * SwmSpmat::transpose_matrix_agp(sparsemat_t *A) {
  int64_t lnnz, i, j, row;
  int64_t pos;
  sparsemat_t * At;

  //T0_printf("UPC version of matrix transpose...");

  // find the number of nnz.s per thread

  SHARED int64_t * shtmp = (int64_t*)_lgp->lgp_all_alloc( A->numcols + THREADS, sizeof(int64_t));
  if( shtmp == NULL ) return(NULL);
  int64_t * l_shtmp = lgp_local_part(int64_t, shtmp);


  int64_t lnc = (A->numcols + THREADS - MYTHREAD - 1)/THREADS;
  for(i=0; i < lnc; i++) {
    l_shtmp[i] = 0;
  }

  T0_fprintf(stderr, "printing stats for initialization\n");
   //_lgp->getShmem()->getSimThread()->clear_stats();
  _lgp->lgp_barrier();

  T0_fprintf(stderr, "Histogram is started...\n");


  _lgp->getShmem()->getSimThread()->SwmMarker(1111);

  int64_t histo_loop_start = _lgp->getShmem()->getTime();
  for( i=0; i< A->lnnz; i++) {                   // histogram the column counts of A
    assert( A->lnonzero[i] < A->numcols );
    assert( A->lnonzero[i] >= 0 );
    //printf("[%d] %d\n", MYTHREAD, i);
    _lgp->lgp_fetch_and_inc(shtmp, A->lnonzero[i]);
  }
  int64_t hist_loop_stop = _lgp->getShmem()->getTime();

  _lgp->getShmem()->getSimThread()->SwmMarker(2222);

  //lgp.getShmem()->setSimEnabled(false);

  T0_fprintf(stderr, "printing stats for histogram\n");
   //_lgp->getShmem()->getSimThread()->clear_stats();
  _lgp->lgp_barrier();

  T0_fprintf(stderr, "Histogram is done...\n");


  lnnz = 0;
  for( i = 0; i < lnc; i++) {
    lnnz += l_shtmp[i];
    //if(!MYTHREAD) printf("l_shtmp[%d]=%ld\n", i, shtmp[i]);
  }
  int weighted = (A->value != NULL);
  At = init_matrix(A->numcols, A->numrows, lnnz, weighted);
  if(!At){printf("ERROR: transpose_matrix_upc: init_matrix failed!\n");return(NULL);}

  T0_fprintf(stderr, "At initialized...\n");

  //printf("[%d] nnz(%ld) lnnz(%ld)\n", MYTHREAD, A->nnz, lnnz);
  int64_t reduction_start = _lgp->getShmem()->getTime();
  int64_t sum = _lgp->lgp_reduce_add_l(lnnz);      // check the histogram counted everything
  int64_t reduction_stop = _lgp->getShmem()->getTime();
  assert( A->nnz == sum );

  T0_fprintf(stderr, "Reduction is done...\n");

  // compute the local offsets
  At->loffset[0] = 0;
  for(i = 1; i < At->lnumrows+1; i++)
    At->loffset[i] = At->loffset[i-1] + l_shtmp[i-1];

  // get the global indices of the start of each row of At
  for(i = 0; i < At->lnumrows; i++)
    l_shtmp[i] = MYTHREAD + THREADS * (At->loffset[i]);

  T0_fprintf(stderr, "printing stats reduction\n");
   //_lgp->getShmem()->getSimThread()->clear_stats();
  _lgp->lgp_barrier();

  T0_fprintf(stderr, "Redistribute the nonzeros...\n");
  _lgp->getShmem()->getSimThread()->SwmMarker(3333);

  int64_t redistribute_start = _lgp->getShmem()->getTime();
  //redistribute the nonzeros
  for(row=0; row<A->lnumrows; row++) {
    for(j=A->loffset[row]; j<A->loffset[row+1]; j++){
      pos = _lgp->lgp_fetch_and_add(shtmp, A->lnonzero[j], (int64_t) THREADS);
      _lgp->lgp_put_int64(At->nonzero, pos, row*THREADS + MYTHREAD);
      if(weighted) _lgp->lgp_put_double(At->value, pos, A->lvalue[j]);
    }
  }
  _lgp->getShmem()->getSimThread()->SwmMarker(4444);
  int64_t redistribute_stop = _lgp->getShmem()->getTime();

  T0_fprintf(stderr, "printing stats main loop\n");
  //_lgp->getShmem()->getSimThread()->clear_stats();
  _lgp->lgp_barrier();
  if(!MYTHREAD)printf("done\n");
  _lgp->lgp_all_free(shtmp);

  int64_t max_histo_loop_time = _lgp->lgp_reduce_max_l(hist_loop_stop-histo_loop_start);
  int64_t max_reduction_time = _lgp->lgp_reduce_max_l(reduction_stop-reduction_start);
  int64_t max_redistribute_time = _lgp->lgp_reduce_max_l(redistribute_stop-redistribute_start);
  T0_fprintf(stderr, "[%d] histo_time %ld, reduction_time %ld, redistribution_time %ld\n", MYTHREAD,max_histo_loop_time,max_reduction_time,max_redistribute_time);

  return(At);
}


/*! \brief Reads a distributed sparse matrix in parallel using an arbitrary number of
 * PEs (between 1 and THREADS) to do the actual I/O.
 *
 * \param datadir The directory that holds the dataset.
 * \param nreaders Number of PEs that will actually do the I/O (all other PEs will mostly
 * be idle.
 * \return The sparsemat_t that was read from the matrix dataset.
 */
sparsemat_t * SwmSpmat::read_sparse_matrix_agp(char * datadir, int64_t nreaders){
    return 0;
}
#if 0
int64_t i;

  spmat_dataset_t * spd = open_sparse_matrix_read(datadir, nreaders);

  // read row_info to get the counts of the rows we will read
  read_row_info(spd);

  /* distribute the row counts in CYCLIC fashion */
  SHARED int64_t * rowcnt = _lgp->lgp_all_alloc(spd->numrows, sizeof(int64_t));
  assert(rowcnt != NULL);
  int64_t * lrowcnt = lgp_local_part(int64_t, rowcnt);

  for(i = 0; i < spd->lnumrows; i++){
    _lgp->lgp_put_int64(rowcnt, spd->global_first_row + i, spd->rowcnt[i]);
    //fprintf(stderr,"copying rc %ld to %ld\n", spd->rowcnt[i], spd->global_first_row + i);
  }

  _lgp->lgp_barrier();

  /* calculate nnz that will land on each PE */
  int64_t lnnz = 0;
  int64_t lnr = (spd->numrows + THREADS - MYTHREAD - 1)/THREADS;
  for(i = 0; i < lnr; i++){
    //fprintf(stderr, "PE %d: rc[%ld] = %ld\n", MYTHREAD, i, lrowcnt[i]);
    lnnz += lrowcnt[i];
  }
  //fprintf(stderr,"PE %d gets %ld nonzeros\n", MYTHREAD, lnnz);fflush(stderr);

  /* initialize sparse matrix */
  sparsemat_t * A = init_matrix(spd->numrows, spd->numcols, lnnz, spd->values);
  if(!A){
    T0_fprintf(stderr,"ERROR: read_sparse_matrix_agp: init_matrix failed!\n");return(NULL);
  }

  /* create A->offset array */
  A->loffset[0] = 0;
  for(i = 0; i < lnr; i++){
    A->loffset[i+1] = A->loffset[i] + lrowcnt[i];
  }

  _lgp->lgp_barrier();
  _lgp->lgp_all_free(rowcnt);

  /* read the nonzeros into the matrix data structure */
  int64_t buf_size = 512*512;
  int64_t * buf = calloc(buf_size, sizeof(int64_t));
  double * vbuf = calloc(buf_size, sizeof(double));
  int64_t tot_rows_read = 0;
  while(tot_rows_read < spd->lnumrows){
    /* read a buffer of nonzeros */
    int64_t pos = 0;
    int64_t num_rows_read = read_nonzeros_buffer(spd, buf, vbuf, buf_size);
    //fprintf(stderr, "PE %d: numrowsread = %ld\n", MYTHREAD, num_rows_read);
    assert(num_rows_read > 0);

    /* place the read nonzeros into the sparse matrix data structure */
    for(i = 0; i < num_rows_read; i++){
      int64_t rc = spd->rowcnt[tot_rows_read + i];
      int64_t global_row = spd->global_first_row + tot_rows_read + i;
      int64_t rs = _lgp->lgp_get_int64(A->offset,global_row)*THREADS + global_row%THREADS;
      //for(int j = 0; j < rc; j++)
      //fprintf(stderr,"PE %d: putting %ld for row %ld (at pos %ld)\n", MYTHREAD, buf[pos + j], global_row, rs + j*THREADS);
      _lgp->lgp_memput(A->nonzero, &buf[pos], rc*sizeof(int64_t), rs);
      if(spd->values)
        _lgp->lgp_memput(A->value, &vbuf[pos], rc*sizeof(double), rs);
      pos += rc;
    }
    tot_rows_read += num_rows_read;
  }

  free(buf);
  if(spd->values) free(vbuf);
  clear_spmat_dataset(spd);
  _lgp->lgp_barrier();
  //print_matrix(A);
  return(A);
}
#endif
/* \brief Write a sparsemat_t to disk in parallel.
 * This is different than write_matrix_mm since a) it is done in parallel
 * and b) we are not using matrix market format.
 *
 * Every THREAD just writes a slice of the matrix (in BLOCK fashion).
 * This requires data to be transferred since the matrix is stored
 * in CYCLIC layout in memory.
 *
 * We write the dataset into PEs*2 files plus an ASCII metadata
 * file. Each PE writes a nonzero file which contains all the nonzeros
 * in their slice and a row_offset file (which says the offsets for
 * each row in the nonzero file). If there are values in the matrix,
 * there will be 3*PEs files.
 *
 * The metadata file just contains some high level info about the matrix
 * dataset.
 * \param dirname The directory to write the matrix to.
 * \param A The matrix to write.
 * \return 0 on SUCCESS 1 on error.
 */
int SwmSpmat::write_sparse_matrix_agp(char * dirname, sparsemat_t * A){
    return 0;
}
#if 0

  int64_t i;

  spmat_dataset_t * spd = open_sparse_matrix_write(dirname, A);

  write_row_info(spd, A);

  /* allocate write buffer */
  int64_t buf_size = 512*512;
  int64_t * buf = calloc(buf_size, sizeof(int64_t));
  double * vbuf;
  if(spd->values)
    vbuf = calloc(buf_size, sizeof(double));

  char fname[128];
  sprintf(fname, "%s/nonzero_%d", spd->dirname, MYTHREAD);
  spd->nnzfp = fopen(fname, "wb");
  if(spd->values){
    sprintf(fname, "%s/value_%d", spd->dirname, MYTHREAD);
    spd->valfp = fopen(fname, "wb");
  }

  /* write out the nonzeros in your block */
  int64_t pos = 0;
  for(i = spd->global_first_row; i < spd->global_first_row + spd->lnumrows; i++){
    int64_t row_start = _lgp->lgp_get_int64(A->offset, i);
    int64_t row_cnt = _lgp->lgp_get_int64(A->offset, i + THREADS) - row_start;
    int64_t plc = row_start*THREADS + i%THREADS;
    if(pos + row_cnt >= buf_size){
      fwrite(buf, sizeof(int64_t), pos, spd->nnzfp);
      if(spd->values) fwrite(vbuf, sizeof(double), pos, spd->valfp);
      pos = 0;
    }

    _lgp->lgp_memget<int64_t>(&buf[pos], A->nonzero, row_cnt*sizeof(int64_t), plc);
    if(spd->values) _lgp->lgp_memget<int64_t>(&vbuf[pos], A->value, row_cnt*sizeof(int64_t), plc);
    pos += row_cnt;
  }
  fwrite(buf, sizeof(int64_t), pos, spd->nnzfp);
  if(spd->values) fwrite(vbuf, sizeof(int64_t), pos, spd->valfp);

  _lgp->lgp_barrier();

  fclose(spd->nnzfp);
  free(buf);

  if(spd->values){
    fclose(spd->valfp);
    free(vbuf);
  }

  clear_spmat_dataset(spd);

  return(0);

}
#endif
