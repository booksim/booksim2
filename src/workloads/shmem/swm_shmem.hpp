/*
 * swm_shmem.cpp
 *
 * SHMEM implementation
 * for Booksim scalable workload model 
 *
 *  Created on: Dec 24, 2022
 *      Author: hdogan
 */



#ifndef shmem_INCLUDED
#define shmem_INCLUDED  /*!< std trick */



#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <sys/time.h>
#include "swm_globals.hpp"
#include "swm.hpp"



enum coll_type_t {
  AUTO = 0,
  LINEAR,
  TREE,
  DISSEM,
  ALL2ALL,
  RING,
  RECDBL,
  HWACCEL
};
typedef enum coll_type_t coll_type_t;


class SwmShmem
{

  SwmThread * _thread;

  // malloc variable 
  int64_t next_index = 0;


  // tree barrier constants
  const int Radix = 8;      // tree barrier radix
  const int BEnterSz = 8;   // payload size of barrier entry message (bytes)
  const int BNotifySz = 8;  // payload size of barrier notify message

  // variables for collectives
  int *full_tree_children;
  int full_tree_num_children;
  int full_tree_parent;
  long tree_radix = -1;

  static coll_type_t shmem_internal_barrier_type;
  static coll_type_t shmem_internal_bcast_type;
  static coll_type_t shmem_internal_reduce_type;
  static long *shmem_internal_sync_all_psync;
  static long *shmem_internal_barrier_all_psync;


  char * get_remote_addr(void * addr, int pe) {

    char * pe_base_addr = _symmetric_mem[pe]; 
    char * my_base_addr = _symmetric_mem[shmem_internal_my_pe]; 

    size_t offset = (char*)addr - my_base_addr;
    char * remote_addr = pe_base_addr + offset; 

    return remote_addr;
  }

  public:

  int shmem_internal_my_pe;
  int shmem_internal_num_pes;

  void shmem_init(SwmThread * thread, int me, int np); 
  int shmem_n_pes() { return shmem_internal_num_pes; } 
  int shmem_my_pe() { return shmem_internal_my_pe; } 

  void *shmem_malloc(size_t sz);
  void sfree(size_t sz); 

  void shmem_quiet() { _thread->QUIET(); }
  void shmem_barrier_all(); 


  void shmem_int64_p  (int64_t *ptr, int64_t val, int pe);
  void shmem_double_p  (double *ptr, double val, int pe);

  int64_t shmem_int64_g  (int64_t *ptr, int pe);
  double shmem_double_g  (double *ptr, int pe);

  int64_t shmem_int64_get_nbi  (int64_t *ptr, int pe);
  double shmem_double_get_nbi  (double *ptr, int pe);

  void shmem_getmem(void *target, const void *source, size_t len, int pe); 
  void shmem_get32(void *target, const void *source, size_t len, int pe); 
  void shmem_get64(void *target, const void *source, size_t len, int pe); 
  void shmem_get128(void *target, const void *source, size_t len, int pe); 

  void shmem_getmem_nbi(void *target, const void *source, size_t len, int pe);
  void shmem_get32_nbi(void *target, const void *source, size_t len, int pe); 
  void shmem_get64_nbi(void *target, const void *source, size_t len, int pe); 
  void shmem_get128_nbi(void *target, const void *source, size_t len, int pe); 

  void shmem_putmem(void *target, const void *source, size_t len, int pe);
  void shmem_put32(void *target, const void *source, size_t len, int pe);
  void shmem_put64(void *target, const void *source, size_t len, int pe);
  void shmem_put128(void *target, const void *source, size_t len, int pe);

  void shmem_long_sum_to_all(long *target, const long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync);
  void shmem_long_min_to_all(long *target, const long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync);
  void shmem_long_max_to_all(long *target, const long *source, int nreduce, int PE_start, int logPE_stride, int PE_size, long *pWrk, long *pSync);

  void shmem_double_sum_to_all(double *target, const double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, double *pWrk, long *pSync);
  void shmem_double_min_to_all(double *target, const double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, double *pWrk, long *pSync);
  void shmem_double_max_to_all(double *target, const double *source, int nreduce, int PE_start, int logPE_stride, int PE_size, double *pWrk, long *pSync);

  void shmem_atomic_add(int64_t *target, int64_t value, int pe); 
  int64_t shmem_atomic_fetch_inc(int64_t * target, int pe); 
  int64_t shmem_atomic_fetch_add(int64_t * target, int64_t value, int pe); 
  int64_t shmem_atomic_compare_swap(int64_t *target, int cond, int value, int pe); 

  void shmem_atomic_add_nbi(int64_t *target, int64_t value, int pe); 
  int64_t shmem_atomic_fetch_inc_nbi(int64_t * target, int pe); 
  int64_t shmem_atomic_fetch_add_nbi(int64_t * target, int64_t value, int pe); 



  /* ------ SHMEM internals ------ */ 
  inline int shmem_internal_circular_iter_next(int curr, int PE_start, int PE_stride, int PE_size);
  int shmem_internal_build_kary_tree(int radix, int PE_start, int stride, int PE_size, int PE_root, int *parent, int *num_children, int *children);
  int shmem_internal_collectives_init(void);

  int * build_tree(int radix, int nnodes, int shmem_local_pes, int *parent, int *num_children);

  
  // Broadcasts
  void shmem_internal_bcast(void *target, const void *source, size_t len, int PE_root, int PE_start, int PE_stride, int PE_size, long *pSync, int complete);
  void shmem_internal_bcast_tree(void *target, const void *source, size_t len, int PE_root, int PE_start, int PE_stride, int PE_size, long *pSync, int complete);
  void shmem_internal_bcast_linear(void *target, const void *source, size_t len, int PE_root, int PE_start, int PE_stride, int PE_size, long *pSync, int complete);

  // Reductions
  void shmem_internal_op_to_all(void *target, const void *source, size_t count, size_t type_size, int PE_start, int PE_stride, int PE_size, void *pWrk, long *pSync);
  void shmem_internal_op_to_all_tree(void *target, const void *source, size_t count, size_t type_size, int PE_start, int PE_stride, int PE_size, void *pWrk, long *pSync); 
  void shmem_internal_op_to_all_linear(void *target, const void *source, size_t count, size_t type_size, int PE_start, int PE_stride, int PE_size, void *pWrk, long *pSync); 
  void shmem_internal_op_to_all_ring(void *target, const void *source, size_t count, size_t type_size, int PE_start, int PE_stride, int PE_size, void *pWrk, long *pSync);
  void shmem_internal_op_to_all_recdbl_sw(void *target, const void *source, size_t count, size_t type_size, int PE_start, int PE_stride, int PE_size, void *pWrk, long *pSync);

  void shmem_internal_reduce_local(int count, int type_size);
  
  
  // Barriers 
  void shmem_internal_sync(int PE_start, int PE_stride, int PE_size, long *pSync);
  void shmem_internal_sync_linear(int PE_start, int PE_stride, int PE_size, long *pSync);
  void shmem_internal_sync_tree(int PE_start, int PE_stride, int PE_size, long *pSync);
  void shmem_internal_sync_dissem(int PE_start, int PE_stride, int PE_size, long *pSync);
  void shmem_internal_sync_all2all(int PE_start, int PE_stride, int PE_size, long *pSync);
  void shmem_internal_sync_all_linear(int root, int PE_size); 
  void shmem_internal_sync_all_dissem(int root, int PE_size); 
  void shmem_internal_sync_all_tree(int root, int PE_size); 
  void shmem_internal_sync_all(void);
  void shmem_internal_barrier_all(void);


  // hardware accelerated collectives
  void shmem_internal_bcast_xl(void *target, const void *source, size_t len, int PE_root, int PE_start, int PE_stride, int PE_size, long *pSync, int complete);
  void shmem_internal_op_to_all_xl(void *target, const void *source, size_t count, size_t type_size, int PE_start, int PE_stride, int PE_size, void *pWrk, long *pSync);
  void shmem_internal_sync_xl(int PE_start, int PE_stride, int PE_size, long *pSync);


  // Simulator related functions
  SwmThread * getSimThread() { return _thread; }
  int64_t getTime() { return _thread->getTime(); }

  
  
  // Tree Barrier Implementation
  //
  // children thread ids at this level of the tree
  std::list<int> children(int level)
  {
    int skip = powi(Radix, level);
    std::list<int> res;
    for (int n = 1, next = shmem_internal_my_pe + skip; n < Radix && next < shmem_internal_num_pes; n++, next += skip)
      res.push_back(next);
    return res;
  }

  // parent thread id at this level of the tree
  int parent(int level)
  {
    int denom = powi(Radix, level + 1);
    return denom * (shmem_internal_my_pe / denom);
  }

  // recursive statically scheduled tree barrier
  void barrier(int level = 0)
  {
    int p = parent(level);
    if (p == shmem_internal_my_pe) {   // PARENT AT THIS LEVEL:
      auto cs = children(level);
      for (int c : cs)                  // wait for children
        _thread->RECV(c);
      if (shmem_internal_num_pes > powi(Radix, level + 1)) // maybe sync at next level
        barrier(level + 1);
      for (int c : cs)                  // notify children
        _thread->SEND(BNotifySz, c);
    }
    else {   // CHILD AT THIS LEVEL:
      _thread->SEND(BEnterSz, p);                // notify parent
      _thread->RECV(p);                          // wait for parent
    }
  }

};


#endif
