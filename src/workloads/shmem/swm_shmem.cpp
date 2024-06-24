/*
 * swm_shmem.cpp
 *
 * SHMEM implementation
 * for Booksim scalable workload model 
 *
 *  Created on: Dec 24, 2022
 *      Author: hdogan
 */

#include <stdlib.h>
#include "swm_shmem.hpp"

char ** _symmetric_mem = NULL;
size_t _symmetric_mem_size;




void SwmShmem::shmem_init(SwmThread * thread, int me, int np) {

  _thread = thread;
  shmem_internal_my_pe = me;
  shmem_internal_num_pes = np;
  _symmetric_mem_size = thread->getConfig()->GetInt("swm_symmetric_heap_size");

  // Allocate memory for symmetric heap
  if(me == 0) {
    _symmetric_mem = (char**) malloc(np*sizeof(char*));
    for (int i=0; i < np; ++i) {
      _symmetric_mem[i] = (char*) malloc(_symmetric_mem_size); 
      if(_symmetric_mem[i] == NULL) {
        printf("Failed to allocate symmetric heap for PE %d\n", i);
        exit(1);
      }
    }
  }


  if(shmem_internal_collectives_init()) {
    printf("Failed to init collectives\n");
    exit(1);
  }
}


void * SwmShmem::shmem_malloc(size_t sz) {
  void *mem;

  if(_symmetric_mem_size - next_index < sz)
    return NULL;

  mem = &_symmetric_mem[shmem_internal_my_pe][next_index];
  next_index += sz;
  return mem;
}

void SwmShmem::sfree(size_t sz) {
  next_index = next_index - sz;
}


void SwmShmem::shmem_barrier_all() {  
  shmem_quiet(); 
  shmem_internal_barrier_all();
}


void SwmShmem::shmem_int64_p  (int64_t *ptr, int64_t val, int pe) { 
  *(int64_t *)get_remote_addr(ptr,pe)  = val; 
  _thread->PUT(sizeof(int64_t), pe); 
}
void SwmShmem::shmem_double_p (double  *ptr, double val, int pe)  { 
  *(double *)get_remote_addr(ptr,pe) = val; 
  _thread->PUT(sizeof(double), pe); 
}
int64_t SwmShmem::shmem_int64_g  (int64_t *ptr, int pe) { 
  _thread->GET(sizeof(int64_t), pe); 
  return *(int64_t *)get_remote_addr(ptr, pe); 
}
double  SwmShmem::shmem_double_g (double  *ptr, int pe) { 
  _thread->GET(sizeof(double), pe);  
  return *(double *)get_remote_addr(ptr, pe); 
}
int64_t SwmShmem::shmem_int64_get_nbi (int64_t *ptr, int pe) { 
  _thread->GETNB(sizeof(int64_t), pe); 
  return *(int64_t *)get_remote_addr(ptr, pe); 
}
double  SwmShmem::shmem_double_get_nbi (double  *ptr, int pe) { 
  _thread->GETNB(sizeof(double), pe);  
  return *(double *)get_remote_addr(ptr, pe); 
}

void SwmShmem::shmem_getmem(void *target, const void *source, size_t len, int pe) { 
  _thread->GET(len, pe);  
  memcpy(target, get_remote_addr((void *)source, pe), len); 
}
void SwmShmem::shmem_get32(void *target, const void *source, size_t len, int pe)  { 
  _thread->GET(sizeof(int32_t)*len, pe);  
  memcpy(target, get_remote_addr((void *)source, pe), sizeof(int32_t)*len); 
}
void SwmShmem::shmem_get64(void *target, const void *source, size_t len, int pe)  { 
  _thread->GET(sizeof(int64_t)*len, pe);  
  memcpy(target, get_remote_addr((void *)source, pe), sizeof(int64_t)*len); 
}
void SwmShmem::shmem_get128(void *target, const void *source, size_t len, int pe) { 
  _thread->GET(sizeof(__int128)*len, pe); 
  memcpy(target, get_remote_addr((void *)source, pe), sizeof(__int128 )*len); 
}

void SwmShmem::shmem_getmem_nbi(void *target, const void *source, size_t len, int pe) { 
  _thread->GETNB(len, pe);  
  memcpy(target, get_remote_addr((void *)source, pe), len); 
}
void SwmShmem::shmem_get32_nbi(void *target, const void *source, size_t len, int pe)  { 
  _thread->GETNB(sizeof(int32_t)*len, pe);  
  memcpy(target, get_remote_addr((void *)source, pe), sizeof(int32_t)*len); 
}
void SwmShmem::shmem_get64_nbi(void *target, const void *source, size_t len, int pe)  { 
  _thread->GETNB(sizeof(int64_t)*len, pe);  
  memcpy(target, get_remote_addr((void *)source, pe), sizeof(int64_t)*len); 
}
void SwmShmem::shmem_get128_nbi(void *target, const void *source, size_t len, int pe) { 
  _thread->GETNB(sizeof(__int128)*len, pe); 
  memcpy(target, get_remote_addr((void *)source, pe), sizeof(__int128 )*len); 
}

void SwmShmem::shmem_putmem(void *target, const void *source, size_t len, int pe) { 
  _thread->PUT(len, pe);  
  memcpy(get_remote_addr(target, pe), source, len); 
}
void SwmShmem::shmem_put32(void *target, const void *source, size_t len, int pe)  { 
  _thread->PUT(sizeof(int32_t)*len, pe);  
  memcpy(get_remote_addr(target, pe), source, sizeof(int32_t)*len); 
}
void SwmShmem::shmem_put64(void *target, const void *source, size_t len, int pe)  { 
  _thread->PUT(sizeof(int64_t)*len, pe);  
  memcpy(get_remote_addr(target, pe), source, sizeof(int64_t)*len); 
}
void SwmShmem::shmem_put128(void *target, const void *source, size_t len, int pe) { 
  _thread->PUT(sizeof(__int128)*len, pe); 
  memcpy(get_remote_addr(target, pe), source, sizeof(__int128 )*len); 
}


void SwmShmem::shmem_long_sum_to_all(long *target, const long *source, int nreduce, int PE_start, int logPE_stride, int PE_size,
    long *pWrk, long *pSync) { 

  // tree base collective implementation 
  shmem_internal_op_to_all(target, source, nreduce, sizeof(long), PE_start, logPE_stride, PE_size, pWrk, pSync); 

  /*
  * Functional part -- work with real data */
  long sum = 0;
  if(shmem_internal_my_pe == 0) {
    for(int j=0; j<nreduce; j++) {
      for(int i=0; i<shmem_internal_num_pes; ++i) {
         long * ptr = ((long *) get_remote_addr((void *)&source[j], i));
         sum += *ptr; 
      }
      for(int i=0; i<shmem_internal_num_pes; ++i) {
         long * ptr = ((long *)get_remote_addr(&target[j], i)); 
         *ptr = sum;
      }
    }
  }
}
void SwmShmem::shmem_long_min_to_all(long *target, const long *source, int nreduce, int PE_start, int logPE_stride, int PE_size,
    long *pWrk, long *pSync) {

  // tree base collective implementation 
  shmem_internal_op_to_all(target, source, nreduce, sizeof(long), PE_start, logPE_stride, PE_size, pWrk, pSync); 

  /* Functional part -- work with real data */
  if(shmem_internal_my_pe == 0) {
    long min = *((long *) get_remote_addr((void *)source, 0));
    for(int i=0; i<shmem_internal_num_pes; ++i) {
      long value = *((long *) get_remote_addr((void *)source, i));
      if(value < min)
        min = value; 
    }
    for(int i=0; i<shmem_internal_num_pes; ++i)
      *((long *)get_remote_addr(target, i)) = min;
  }
}
void SwmShmem::shmem_long_max_to_all(long *target, const long *source, int nreduce, int PE_start, int logPE_stride, int PE_size,
    long *pWrk, long *pSync) {

  // tree base collective implementation 
  shmem_internal_op_to_all(target, source, nreduce, sizeof(long), PE_start, logPE_stride, PE_size, pWrk, pSync); 

  /* Functional part -- work with real data */
  if(shmem_internal_my_pe == 0) {
    long max = *((long *) get_remote_addr((void *)source, 0));
    for(int i=0; i<shmem_internal_num_pes; ++i) {
      long * ptr = ((long *) get_remote_addr((void *)source, i));
      long value = *ptr;
      if(value > max)
        max = value; 
    }
    for(int i=0; i<shmem_internal_num_pes; ++i) {
      long * ptr = ((long *) get_remote_addr(target, i));
      *ptr = max;
    }
  }
}

void SwmShmem::shmem_double_sum_to_all(double *target, const double *source, int nreduce, int PE_start, int logPE_stride, int PE_size,
    double *pWrk, long *pSync) { 

  // tree base collective implementation 
  shmem_internal_op_to_all(target, source, nreduce, sizeof(double), PE_start, logPE_stride, PE_size, pWrk, pSync); 

  /* Functional part -- work with real data */
  double sum = 0;
  if(shmem_internal_my_pe == 0) {
    for(int i=0; i<shmem_internal_num_pes; ++i)
      sum += *((double *) get_remote_addr((void *)source, i));   
    for(int i=0; i<shmem_internal_num_pes; ++i)
      *((double *)get_remote_addr(target, i)) = sum;
  }
}
void SwmShmem::shmem_double_min_to_all(double *target, const double *source, int nreduce, int PE_start, int logPE_stride, int PE_size,
    double *pWrk, long *pSync) {

  // tree base collective implementation 
  shmem_internal_op_to_all(target, source, nreduce, sizeof(double), PE_start, logPE_stride, PE_size, pWrk, pSync); 

  /* Functional part -- work with real data */
  if(shmem_internal_my_pe == 0) {
    double min = *((double *) get_remote_addr((void *)source, 0));
    for(int i=0; i<shmem_internal_num_pes; ++i) {
      double value = *((double *) get_remote_addr((void *)source, i));
      if(value < min)
        min = value; 
    }
    for(int i=0; i<shmem_internal_num_pes; ++i)
      *((double *)get_remote_addr(target, i)) = min;
  }

}
void SwmShmem::shmem_double_max_to_all(double *target, const double *source, int nreduce, int PE_start, int logPE_stride, int PE_size,
    double *pWrk, long *pSync) {

  // tree base collective implementation 
  shmem_internal_op_to_all(target, source, nreduce, sizeof(double), PE_start, logPE_stride, PE_size, pWrk, pSync); 

  /* Functional part -- work with real data */
  if(shmem_internal_my_pe == 0) {
    double max = *((double *) get_remote_addr((void *)source, 0));
    for(int i=0; i<shmem_internal_num_pes; ++i) {
      double value = *((double *) get_remote_addr((void *)source, i));
      if(value > max)
        max = value; 
    }
    for(int i=0; i<shmem_internal_num_pes; ++i)
      *((double *)get_remote_addr(target, i)) = max;
  }
}

void SwmShmem::shmem_atomic_add(int64_t *target, int64_t value, int pe) {
  int64_t * ptr = (int64_t *) get_remote_addr(target, pe);  
  *ptr += value; // No need to use atomic add because booksim is sequential
  _thread->GET(sizeof(int64_t), pe);
}

int64_t SwmShmem::shmem_atomic_fetch_inc(int64_t * target, int pe) {
  int64_t * ptr = (int64_t *) get_remote_addr(target, pe);  
  int64_t oldval = *ptr;
  *ptr = *ptr + 1;
  _thread->GET(sizeof(int64_t), pe);
  return oldval;
}

int64_t SwmShmem::shmem_atomic_fetch_add(int64_t * target, int64_t value, int pe) {
  int64_t * ptr = (int64_t *) get_remote_addr(target, pe);  
  int64_t oldval = *ptr;
  *ptr += value;
  _thread->GET(sizeof(int64_t), pe);
  return oldval;
}

int64_t SwmShmem::shmem_atomic_compare_swap(int64_t *target, int cond, int value, int pe) {
  int64_t * ptr = (int64_t*)get_remote_addr(target, pe);
  int64_t ret =  __sync_val_compare_and_swap(ptr, cond, value);
  _thread->GET(sizeof(int64_t), pe);
  return ret; 
}

void SwmShmem::shmem_atomic_add_nbi(int64_t *target, int64_t value, int pe) {
  int64_t * ptr = (int64_t *) get_remote_addr(target, pe);  
  *ptr += value; // No need to use atomic add because booksim is sequential
  _thread->GETNB(sizeof(int64_t), pe);
}

int64_t SwmShmem::shmem_atomic_fetch_inc_nbi(int64_t * target, int pe) {
  int64_t * ptr = (int64_t *) get_remote_addr(target, pe);  
  int64_t oldval = *ptr;
  *ptr = *ptr + 1;
  _thread->GETNB(sizeof(int64_t), pe);
  return oldval;
}

int64_t SwmShmem::shmem_atomic_fetch_add_nbi(int64_t * target, int64_t value, int pe) {
  int64_t * ptr = (int64_t *) get_remote_addr(target, pe);  
  int64_t oldval = *ptr;
  *ptr += value;
  _thread->GETNB(sizeof(int64_t), pe);
  return oldval;
}









