/*
 * swm_collectives.cpp
 *
 * collectives.c from SandiaOpenSHMEM
 * ported for Booksim scalable workload model
 *
 *  Created on: Dec 24, 2022
 *      Author: hdogan
 */



/* -*- C -*-
 *
 * Copyright 2011 Sandia Corporation. Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S.  Government
 * retains certain rights in this software.
 *
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
 * This software is available to you under the BSD license.
 *
 * This file is part of the Sandia OpenSHMEM software package. For license
 * information, see the LICENSE file in the top level directory of the
 * ditrtribution.
 *
 */


#define SHMEM_INTERNAL_INCLUDE
#include "swm_shmem.hpp"
#include "wcomp_collxl.hpp"

#if 0
coll_type_t shmem_internal_barrier_type = AUTO;
coll_type_t shmem_internal_bcast_type = AUTO;
coll_type_t shmem_internal_collect_type = AUTO;
coll_type_t shmem_internal_fcollect_type = AUTO;


static int *full_tree_children;
tatic int full_tree_num_children;
static int full_tree_parent;
static long tree_radix = -1;
#endif

std::string coll_type_str[] = { "auto",
  "linear",
  "tree",
  "dissem",
  "all2all",
  "ring",
  "recdbl",
  "hwaccel" };

coll_type_t SwmShmem::shmem_internal_barrier_type = AUTO;
coll_type_t SwmShmem::shmem_internal_reduce_type = AUTO;
coll_type_t SwmShmem::shmem_internal_bcast_type = AUTO;

long *SwmShmem::shmem_internal_sync_all_psync;
long *SwmShmem::shmem_internal_barrier_all_psync;


  int
SwmShmem::shmem_internal_collectives_init(void)
{
  int i, j, k;
  int tmp_radix;
  int my_root = 0;

  tree_radix = _thread->getConfig()->GetInt("k"); //Radix; //shmem_internal_params.COLL_RADIX;

  std::string barrier_type = _thread->getConfig()->GetStr("swm_shmem_barrier");
  std::string reduce_type  = _thread->getConfig()->GetStr("swm_shmem_reduce");
  std::string bcast_type   = _thread->getConfig()->GetStr("swm_shmem_bcast");

  if(!shmem_internal_my_pe) {
    printf("Barrier algorithm : %s\n", barrier_type.c_str());
    printf("Bcast   algorithm : %s\n", bcast_type.c_str());
    printf("Reduce  algorithm : %s\n", reduce_type.c_str());
  }

  // TODO: need to make this variable
  shmem_internal_barrier_type = TREE;
  if (barrier_type == "auto") {
    shmem_internal_barrier_type = AUTO;
  } else if (barrier_type ==  "linear") {
    shmem_internal_barrier_type = LINEAR;
  } else if (barrier_type == "tree") {
    shmem_internal_barrier_type = TREE;
  } else if (barrier_type == "dissem") {
    shmem_internal_barrier_type = DISSEM;
  } else if (barrier_type == "all2all") {
    shmem_internal_barrier_type = ALL2ALL;
  } else if (barrier_type == "hwaccel") {
    shmem_internal_barrier_type = HWACCEL;
  } else {
    printf("Ignoring bad barrier algorithm '%s'\n", barrier_type.c_str());
  }

  // TODO: need to make this variable
  shmem_internal_bcast_type = TREE;
  if (bcast_type == "auto") {
    shmem_internal_bcast_type = AUTO;
  } else if (bcast_type ==  "linear") {
    shmem_internal_bcast_type = LINEAR;
  } else if (bcast_type == "tree") {
    shmem_internal_bcast_type = TREE;
  } else {
    printf("Ignoring bad bcast algorithm '%s'\n", bcast_type.c_str());
  }

  // TODO: need to make this variable
  shmem_internal_reduce_type = TREE;
  if (reduce_type == "auto") {
    shmem_internal_reduce_type = AUTO;
  } else if (reduce_type ==  "linear") {
    shmem_internal_reduce_type = LINEAR;
  } else if (reduce_type == "tree") {
    shmem_internal_reduce_type = TREE;
  } else if (reduce_type == "recdbl") {
    shmem_internal_reduce_type = RECDBL;
  } else if (reduce_type == "ring") {
    shmem_internal_reduce_type = RING;
  } else if (reduce_type == "hwaccel") {
    shmem_internal_reduce_type = HWACCEL;
  } else {
    printf("Ignoring bad reduce algorithm '%s'\n", reduce_type.c_str());
  }


  /* initialize the binomial tree for collective operations over
     entire tree */
  full_tree_num_children = 0;
  for (i = 1 ; i <= shmem_internal_num_pes ; i *= tree_radix) {
    tmp_radix = (shmem_internal_num_pes / i < tree_radix) ?
      (shmem_internal_num_pes / i) + 1 : tree_radix;
    my_root = (shmem_internal_my_pe / (tmp_radix * i)) * (tmp_radix * i);
    if (my_root != shmem_internal_my_pe) break;
    for (j = 1 ; j < tmp_radix ; ++j) {
      if (shmem_internal_my_pe + i * j < shmem_internal_num_pes) {
        full_tree_num_children++;
      }
    }
  }

  full_tree_children = (int *) malloc(sizeof(int) * full_tree_num_children);
  if (NULL == full_tree_children) return -1;

  k = full_tree_num_children - 1;
  for (i = 1 ; i <= shmem_internal_num_pes ; i *= tree_radix) {
    tmp_radix = (shmem_internal_num_pes / i < tree_radix) ?
      (shmem_internal_num_pes / i) + 1 : tree_radix;
    my_root = (shmem_internal_my_pe / (tmp_radix * i)) * (tmp_radix * i);
    if (my_root != shmem_internal_my_pe) break;
    for (j = 1 ; j < tmp_radix ; ++j) {
      if (shmem_internal_my_pe + i * j < shmem_internal_num_pes) {
        full_tree_children[k--] = shmem_internal_my_pe + i * j;
      }
    }
  }
  full_tree_parent = my_root;

  return 0;
}



  int
SwmShmem::shmem_internal_build_kary_tree(int radix, int PE_start, int stride,
    int PE_size, int PE_root, int *parent,
    int *num_children, int *children)
{
  int i;

  /* my_id is the index in a theoretical 0...N-1 array of
     participating tasks. where the 0th entry is the root */
  int my_id = (((shmem_internal_my_pe - PE_start) / stride) + PE_size - PE_root) % PE_size;

  /* We shift PE_root to index 0, resulting in a PE active set layout of (for
     example radix 2): 0 [ 1 2 ] [ 3 4 ] [ 5 6 ] ...  The first group [ 1 2 ]
     are chilren of 0, second group [ 3 4 ] are chilren of 1, and so on */
  *parent = PE_start + (((my_id - 1) / radix + PE_root) % PE_size) * stride;

  *num_children = 0;
  for (i = 1 ; i <= radix ; ++i) {
    int tmp = radix * my_id + i;
    if (tmp < PE_size) {
      const int child_idx = (PE_root + tmp) % PE_size;
      children[(*num_children)++] = PE_start + child_idx * stride;
    }
  }

  return 0;
}



/*****************************************
 *
 * BARRIER/SYNC Implementations
 *
 *****************************************/

  void
SwmShmem::shmem_internal_sync(int PE_start, int PE_stride, int PE_size, long *pSync)
{
  if (PE_size == 1) return;

  switch (shmem_internal_barrier_type) {
    /*case AUTO:
      if (PE_size < shmem_internal_params.COLL_CROSSOVER) {
      shmem_internal_sync_linear(PE_start, PE_stride, PE_size, pSync);
      } else {
      shmem_internal_sync_tree(PE_start, PE_stride, PE_size, pSync);
      }
      break;
      */
    case LINEAR:
      if(pSync == shmem_internal_sync_all_psync)
        shmem_internal_sync_all_linear(PE_start, PE_size);
      else
        shmem_internal_sync_linear(PE_start, PE_stride, PE_size, pSync);
      break;
    case TREE:
      if(pSync == shmem_internal_sync_all_psync)
         shmem_internal_sync_all_tree(PE_start, PE_size);
      else
         shmem_internal_sync_tree(PE_start, PE_stride, PE_size, pSync);
      break;
    case DISSEM:
      if(pSync == shmem_internal_sync_all_psync)
        shmem_internal_sync_all_dissem(PE_start, PE_size);
      else
        shmem_internal_sync_dissem(PE_start, PE_stride, PE_size, pSync);
      break;
    case ALL2ALL:
      shmem_internal_sync_all2all(PE_start, PE_stride, PE_size, pSync);
      break;
    case HWACCEL:
      shmem_internal_sync_xl(PE_start, PE_stride, PE_size, pSync);
      break;
    default:
      printf("Illegal barrier/sync type (%d)\n",
          shmem_internal_barrier_type);
      exit(1);
  }

  shmem_quiet();
  /* Ensure remote updates are visible in memory */
  //shmem_internal_membar_acq_rel();
  //shmem_transport_syncmem();
}


  void
SwmShmem::shmem_internal_sync_all(void)
{
  shmem_quiet(); // this is to replace shmem_internal_quit();
  shmem_internal_sync(0, 1, shmem_internal_num_pes, shmem_internal_sync_all_psync);
}

  void
SwmShmem::shmem_internal_barrier_all(void)
{
  shmem_quiet(); // this is to replace shmem_internal_quit();
  shmem_internal_sync(0, 1, shmem_internal_num_pes, shmem_internal_barrier_all_psync);
}


void SwmShmem::shmem_internal_sync_all_linear(int root, int PE_size) {

  int shmem_local_pes = _thread->getConfig()->GetInt("swm_ppn");
  int my_local_root = (shmem_internal_my_pe/shmem_local_pes) * shmem_local_pes;


  if(shmem_internal_my_pe != my_local_root) {
    _thread->SEND(BEnterSz, my_local_root);
    _thread->RECV(my_local_root);
  } else {
    if(shmem_internal_my_pe == root)
    // Wait for messages from local PEs
    for(int i=1; i<shmem_local_pes; ++i) {
      int pe = my_local_root + i;
      _thread->RECV(pe);
    }

    if(shmem_internal_my_pe == root) {
      // Wait for message from all the other nodes
      for(int i=0; i<PE_size; i += shmem_local_pes ) {
        if(i != shmem_internal_my_pe)
          _thread->RECV(i);
      }
      // Notify all the children
      for(int i=0; i<PE_size; i += shmem_local_pes ) {
        if(i != shmem_internal_my_pe)
          _thread->SEND(BEnterSz, i);
      }
    } else {
      // Send message to the root node
      _thread->SEND(BEnterSz,root);

      // Wait reply from the root node
      _thread->RECV(root);
    }

    // Notify local PEs
    for(int i=1; i<shmem_local_pes; ++i) {
      int pe = my_local_root + i;
      _thread->SEND(BEnterSz, pe);
    }
  }
}

  void
SwmShmem::shmem_internal_sync_linear(int PE_start, int PE_stride, int PE_size, long *pSync)
{

  if (PE_start == shmem_internal_my_pe) {
    int pe, i;

    /* wait for N - 1 callins up the tree */
    for(int i=1; i<shmem_internal_num_pes; ++i)
      _thread->RECV(i);


    /* Clear pSync */
    /* Send acks down psync tree */
    for (pe = PE_start + PE_stride, i = 1 ;
        i < PE_size ;
        i++, pe += PE_stride) {
      _thread->SEND(BEnterSz, i);
    }

  } else {
    /* send message to root */
    _thread->SEND(BEnterSz, PE_start);

    /* wait for ack down psync tree */
    /* Clear pSync */
    _thread->RECV(PE_start);
  }

}

// build tree for tree barrier algorithm
int * SwmShmem::build_tree(int radix, int nnodes, int shmem_local_pes, int *parent, int *num_children)
{
  int tmp_radix, my_root = 0;
  *num_children = 0;
  int my_node = shmem_internal_my_pe / shmem_local_pes;
  for (int i = 1 ; i <= nnodes ; i *= radix) {
    tmp_radix = (nnodes / i < radix) ?
      (nnodes / i) + 1 : radix;
    my_root = (my_node / (tmp_radix * i)) * (tmp_radix * i);
    if (my_root != my_node) break;
    for (int j = 1 ; j < tmp_radix ; ++j) {
      if (my_node + i * j < nnodes) {
        (*num_children)++;
      }
    }
  }

  int * children = (int*) malloc(sizeof(int) * (*num_children));

  int k = *num_children - 1;
  for (int i = 1 ; i <= nnodes ; i *= radix) {
    tmp_radix = (nnodes / i < radix) ?
      (nnodes / i) + 1 : radix;
    my_root = (my_node / (tmp_radix * i)) * (tmp_radix * i);
    if (my_root != my_node) break;
    for (int j = 1 ; j < tmp_radix ; ++j) {
      if (my_node + i * j < nnodes) {
        children[k--] = (my_node + i * j) * shmem_local_pes;
      }
    }
  }
  *parent = my_root * shmem_local_pes;
  return children;
}


  void
SwmShmem::shmem_internal_sync_all_tree(int PE_start, int PE_size)
{
  int parent, num_children, *children;


  int shmem_local_pes = _thread->getConfig()->GetInt("swm_ppn");
  int my_local_root = (shmem_internal_my_pe/shmem_local_pes) * shmem_local_pes;


  if(my_local_root != shmem_internal_my_pe) {
    _thread->SEND(BEnterSz, my_local_root);
    _thread->RECV(my_local_root);
  } else  {
    // Wait for local PEs
    for(int i=1; i<shmem_local_pes; ++i) {
      int pe = my_local_root + i;
      _thread->RECV(pe);
    }

    children = build_tree(tree_radix, PE_size/shmem_local_pes, shmem_local_pes, &parent, &num_children);

    if (num_children != 0) {
      /* Not a pure leaf node */
      int i;

      /* wait for num_children callins up the tree */
      for(int i=0; i<num_children; ++i) {
        _thread->RECV(children[i]);
      }

      if (parent == shmem_internal_my_pe) {
        /* The root of the tree */

        /* Send acks down to children */
        for (i = 0 ; i < num_children ; ++i) {
          _thread->SEND(BEnterSz, children[i]);
        }

      } else {
        /* Middle of the tree */

        /* send ack to parent */
        _thread->SEND(BEnterSz, parent);

        /* wait for ack from parent */
        _thread->RECV(parent);

        /* Send acks down to children */
        for (i = 0 ; i < num_children ; ++i) {
          _thread->SEND(BEnterSz,children[i]);
        }
      }
    } else {
      /* Leaf node */

      /* send message up psync tree */
      _thread->SEND(BEnterSz,parent);

      /* wait for ack down psync tree */
      _thread->RECV(parent);
    }

    // Notify local PEs
    for(int i=1; i<shmem_local_pes; ++i) {
      int pe = my_local_root + i;
      _thread->SEND(BEnterSz, pe);
    }
  }

  //free(children);
}

  void
SwmShmem::shmem_internal_sync_tree(int PE_start, int PE_stride, int PE_size, long *pSync)
{
  int parent, num_children, *children;

  if (PE_size == shmem_internal_num_pes) {
    /* we're the full tree, use the binomial tree */
    parent = full_tree_parent;
    num_children = full_tree_num_children;
    children = full_tree_children;
  } else {
    children = (int *)alloca(sizeof(int) * tree_radix);
    shmem_internal_build_kary_tree(tree_radix, PE_start, PE_stride, PE_size,
        0, &parent, &num_children, children);
  }

  if (num_children != 0) {
    /* Not a pure leaf node */
    int i;

    /* wait for num_children callins up the tree */
    for(int i=0; i<num_children; ++i) {
      _thread->RECV(children[i]);
    }

    if (parent == shmem_internal_my_pe) {
      /* The root of the tree */

      /* Send acks down to children */
      for (i = 0 ; i < num_children ; ++i) {
        _thread->SEND(BEnterSz, children[i]);
      }

    } else {
      /* Middle of the tree */

      /* send ack to parent */
      _thread->SEND(BEnterSz, parent);

      /* wait for ack from parent */
      _thread->RECV(parent);

      /* Send acks down to children */
      for (i = 0 ; i < num_children ; ++i) {
        _thread->SEND(BEnterSz,children[i]);
      }
    }
  } else {
    /* Leaf node */

    /* send message up psync tree */
    _thread->SEND(BEnterSz,parent);

    /* wait for ack down psync tree */
    _thread->RECV(parent);
  }
}

  void
SwmShmem::shmem_internal_sync_all_dissem(int PE_start, int PE_size)
{
  int distance, to;

  int shmem_local_pes = _thread->getConfig()->GetInt("swm_ppn");
  int my_local_root = (shmem_internal_my_pe/shmem_local_pes) * shmem_local_pes;

  // Syncronize local PEs
  if(my_local_root != shmem_internal_my_pe) {
    _thread->SEND(BEnterSz, my_local_root);
    _thread->RECV(my_local_root);
  } else {
    int nodes = PE_size / shmem_local_pes;
    for (int i=1; i<shmem_local_pes; ++i) {
      int pe = my_local_root + i;
      _thread->RECV(pe);
    }


    int my_node = shmem_internal_my_pe / shmem_local_pes;
    int coll_rank = (my_node - PE_start);

    /* need log2(num_procs) int slots.  max_num_procs is
       2^(sizeof(int)*8-1)-1, so make the math a bit easier and assume
       2^(sizeof(int) * 8), which means log2(num_procs) is always less
       than sizeof(int) * 8. */
    /* Note: pSync can be treated as a byte array rather than an int array to
     * get better cache locality.  We chose int here for portability, since SUM
     * on INT is required by the SHMEM atomics API. */
    for (distance = 1 ; distance < nodes ; distance <<= 1) {
      to = ((coll_rank + distance) % nodes);
      to = PE_start + to;

      int from = coll_rank-distance;
      if(from < 0)
        from = nodes - distance + coll_rank;

      _thread->SEND(BEnterSz, to * shmem_local_pes);

      _thread->RECV(from * shmem_local_pes);
    }

    // Notify local PEs
    for(int i=1; i<shmem_local_pes; ++i) {
      int pe = my_local_root + i;
      _thread->SEND(BEnterSz,pe);
    }
  }
  /* Ensure local pSync decrements are done before a subsequent barrier */
  //shmem_internal_quiet(SHMEM_CTX_DEFAULT);
}


  void
SwmShmem::shmem_internal_sync_dissem(int PE_start, int PE_stride, int PE_size, long *pSync)
{
  int distance, to;
  int coll_rank = (shmem_internal_my_pe - PE_start) / PE_stride;

  /* need log2(num_procs) int slots.  max_num_procs is
     2^(sizeof(int)*8-1)-1, so make the math a bit easier and assume
     2^(sizeof(int) * 8), which means log2(num_procs) is always less
     than sizeof(int) * 8. */
  /* Note: pSync can be treated as a byte array rather than an int array to
   * get better cache locality.  We chose int here for portability, since SUM
   * on INT is required by the SHMEM atomics API. */

  for (distance = 1 ; distance < PE_size ; distance <<= 1) {
    to = ((coll_rank + distance) % PE_size);
    to = PE_start + (to * PE_stride);

    int from = coll_rank-distance;
    if(from < 0)
      from = PE_size - distance + coll_rank;

    _thread->SEND(BEnterSz, to);

    _thread->RECV(from);
  }
  /* Ensure local pSync decrements are done before a subsequent barrier */
  //shmem_internal_quiet(SHMEM_CTX_DEFAULT);
}


  void
SwmShmem::shmem_internal_sync_all2all(int PE_start, int PE_stride, int PE_size, long *pSync)
{

  int shmem_local_pes = _thread->getConfig()->GetInt("swm_ppn");
  int my_local_root = (shmem_internal_my_pe/shmem_local_pes) * shmem_local_pes;


  // Local sync first using local root
  if(my_local_root != shmem_internal_my_pe) {
    _thread->SEND(BEnterSz, my_local_root);
    _thread->RECV(my_local_root);
  } else {
    // Wait for messages from local PEs
    for(int i=1; i<shmem_local_pes; ++i) {
      int pe = my_local_root + i;
      _thread->RECV(pe);
    }

    // Node roots send all-to-all messages
    int nodes = shmem_internal_num_pes / shmem_local_pes;
    for(int i=0; i<nodes; ++i) {
      int pe = i * shmem_local_pes;
      if(pe != shmem_internal_my_pe)
        _thread->SEND(BEnterSz, pe);
    }

    for(int i=0; i<nodes; ++i) {
      int pe = i * shmem_local_pes;
      if(pe != shmem_internal_my_pe) {
        _thread->RECV(pe);
      }
    }

    // Notify local PEs
    for(int i=1; i<shmem_local_pes; ++i) {
      int pe = my_local_root + i;
      _thread->SEND(BEnterSz, pe);
    }
    //_thread->QUIET();
  }


  /*
     int i=0;
     for(i = PE_start; i < PE_size; i = i + PE_stride) {
     if(i != shmem_internal_my_pe) {
     _thread->SEND(BEnterSz, i);
     }
     }

     for(i = PE_start; i < PE_size; i = i + PE_stride) {
     if(i != shmem_internal_my_pe)
     _thread->RECV(i);
     }
     */

}

// hardware accelerated barrier sync
  void
SwmShmem::shmem_internal_sync_xl(int PE_start, int PE_stride, int PE_size, long *pSync)
{
  // send a request to accelerator and block on the response
  _thread->ACCELREQ(new CollectiveAccel::Request(_thread, _thread->get_id(), _thread->get_num_pes()));
  _thread->QUIET();
}


/*****************************************
 *
 * BROADCAST
 *
 *****************************************/

  void
SwmShmem::shmem_internal_bcast(void *target, const void *source, size_t len,
    int PE_root, int PE_start, int PE_stride, int PE_size,
    long *pSync, int complete)
{
  switch (shmem_internal_bcast_type) {
    /*case AUTO:
      if (PE_size < shmem_internal_params.COLL_CROSSOVER) {
      shmem_internal_bcast_linear(target, source, len, PE_root, PE_start,
      PE_stride, PE_size, pSync, complete);
      } else {
      shmem_internal_bcast_tree(target, source, len, PE_root, PE_start,
      PE_stride, PE_size, pSync, complete);
      }
      break;
      */
    case LINEAR:
      shmem_internal_bcast_linear(target, source, len, PE_root, PE_start,
          PE_stride, PE_size, pSync, complete);
      break;
    case TREE:
      shmem_internal_bcast_tree(target, source, len, PE_root, PE_start,
          PE_stride, PE_size, pSync, complete);
      break;
    default:
      printf("Illegal broadcast type (%d)\n",
          shmem_internal_bcast_type);
      exit(1);
  }
}


  void
SwmShmem::shmem_internal_bcast_tree(void *target, const void *source, size_t len,
    int PE_root, int PE_start, int PE_stride, int PE_size,
    long *pSync, int complete)
{
  long one = 1; // zero = 0, one = 1;
  int parent, num_children, *children;

  if (PE_size == 1 || len == 0) return;

  if (PE_size == shmem_internal_num_pes && 0 == PE_root) {
    /* we're the full tree, use the binomial tree */
    parent = full_tree_parent;
    num_children = full_tree_num_children;
    children = full_tree_children;
  } else {
    children = (int *) alloca(sizeof(int) * tree_radix);
    shmem_internal_build_kary_tree(tree_radix, PE_start, PE_stride, PE_size,
        PE_root, &parent, &num_children, children);
  }


  if (0 != num_children) {
    int i;

    if (parent != shmem_internal_my_pe) {
      /* wait for data arrival message if not the root */
      _thread->RECV(parent);
    }

    /* send data to all leaves */
    for (i = 0 ; i < num_children ; ++i) {
      _thread->PUT(len, children[i]);
    }
    //_thread->QUIET(); // Do we need a fence here?

    /* send completion ack to all peers */
    for (i = 0 ; i < num_children ; ++i) {
      _thread->SEND(sizeof(one), children[i]);
    }
    //_thread->QUIET();

  } else {
    /* wait for data arrival message */
    _thread->RECV(parent);
  }

}


  void
SwmShmem::shmem_internal_bcast_linear(void *target, const void *source, size_t len,
    int PE_root, int PE_start, int PE_stride, int PE_size,
    long *pSync, int complete)
{
  long one = 1;
  int real_root = PE_start + PE_root * PE_stride;

  if (PE_size == 1 || len == 0) return;

  if (real_root == shmem_internal_my_pe) {
    int i, pe;

    /* send data to all peers */
    for (pe = PE_start,i=0; i < PE_size; pe += PE_stride, i++) {
      if (pe == shmem_internal_my_pe) continue;
      _thread->PUT(len, pe);
    }
    //_thread->QUIET();

    /* send completion ack to all peers */
    for (pe = PE_start,i=0; i < PE_size; pe += PE_stride, i++) {
      if (pe == shmem_internal_my_pe) continue;
      _thread->SEND(sizeof(one), pe);
    }

  } else {
    /* wait for data arrival message */
    _thread->RECV(real_root);
  }
}


/*****************************************
 *
 * REDUCTION
 *
 *****************************************/


#define CACHELINE 64

void SwmShmem::shmem_internal_reduce_local(int count, int type_size)
{

  //int reduction_overhead = 5; // 5 cycles reduction overhead per cacheline

  //int stride = CACHELINE/type_size;
  //if (stride == 0) stride = 1; // if type size is larger than cacheline
  //for(int i=0; i<count; i+=stride) {
     //cout << "i: " << i << " stride " << stride << " count: " << count << endl;
    //_thread->WORK(reduction_overhead);
  //}
}

  void
SwmShmem::shmem_internal_op_to_all(void *target, const void *source, size_t count,
    size_t type_size, int PE_start, int PE_stride,
    int PE_size, void *pWrk, long *pSync)
{
  assert(type_size > 0);

  switch (shmem_internal_reduce_type) {
    case LINEAR:
         shmem_internal_op_to_all_linear(target, source, count, type_size,
               PE_start, PE_stride, PE_size, pWrk, pSync);
         break;

    case RING:
        shmem_internal_op_to_all_ring(target, source, count, type_size,
               PE_start, PE_stride, PE_size, pWrk, pSync);
        break;

    case TREE:
        shmem_internal_op_to_all_tree(target, source, count, type_size,
               PE_start, PE_stride, PE_size, pWrk, pSync);
        break;

    case RECDBL:
        shmem_internal_op_to_all_recdbl_sw(target, source, count, type_size,
               PE_start, PE_stride, PE_size, pWrk, pSync);
        break;

    case HWACCEL:
        shmem_internal_op_to_all_xl(target, source, count, type_size,
               PE_start, PE_stride, PE_size, pWrk, pSync);
        break;


    default:
      printf("Illegal reduction type (%d)\n", shmem_internal_reduce_type);
      exit(1);

  }
}

  void
SwmShmem::shmem_internal_op_to_all_tree(void *target, const void *source, size_t count, size_t type_size,
    int PE_start, int PE_stride, int PE_size,
    void *pWrk, long *pSync)
{
  long one = 1;
  int parent, num_children, *children;

  if (PE_size == 1) {
    if (target != source) {
      memcpy(target, source, type_size*count);
    }
    return;
  }

  if (count == 0) return;

  if (PE_size == shmem_internal_num_pes) {
    /* we're the full tree, use the binomial tree */
    parent = full_tree_parent;
    num_children = full_tree_num_children;
    children = full_tree_children;
  } else {
    children = (int *) alloca(sizeof(int) * tree_radix);
    shmem_internal_build_kary_tree(tree_radix, PE_start, PE_stride, PE_size,
        0, &parent, &num_children, children);
  }


  if (0 != num_children) {
    int i;

    /* update our target buffer with our contribution.  The put
       will flush any atomic cache value that may currently
       exist. */
    for (i = 0 ; i < num_children ; ++i) {
      _thread->SEND(sizeof(one), children[i]);
    }

    /* Wait for others to acknowledge sending data */
    for (i = 0 ; i < num_children ; ++i) {
      _thread->RECV(children[i]);
    }
  }

  if (parent != shmem_internal_my_pe) {
    /* wait for clear to send */
    _thread->RECV(parent);

    // Send data to parent for calculation
    _thread->PUT(count*type_size, parent);
    _thread->QUIET(); // Do we need this?

    /* send data, ack, and wait for completion */
    _thread->SEND(sizeof(one), parent);
  }

  /* broadcast out */
  shmem_internal_bcast(target, target, count * type_size, 0, PE_start, PE_stride, PE_size, pSync + 2, 0);
}




  void
SwmShmem::shmem_internal_op_to_all_linear(void *target, const void *source, size_t count, size_t type_size,
    int PE_start, int PE_stride, int PE_size,
    void *pWrk, long *pSync)
{

  long one = 1;

  if (count == 0) return;

  if (PE_start == shmem_internal_my_pe) {
    int pe, i;
    /* update our target buffer with our contribution.  The put
       will flush any atomic cache value that may currently
       exist. */
    _thread->PUT(count * type_size, shmem_internal_my_pe);
    //_thread->QUIET();

    /* let everyone know that it's safe to send to us */
    for (pe = PE_start + PE_stride, i = 1 ;
        i < PE_size ;
        i++, pe += PE_stride) {
      _thread->SEND(sizeof(one), pe);
    }

    /* Wait for others to acknowledge sending data */
    for (pe = PE_start + PE_stride, i = 1 ;
        i < PE_size ;
        i++, pe += PE_stride) {
      _thread->RECV(pe);
    }

  } else {
    /* wait for clear to send */
    _thread->RECV(PE_start);

    /* send data, ack, and wait for completion */
    _thread->PUT(count * type_size, PE_start);
    _thread->QUIET(); // Do we need this?

    _thread->SEND(sizeof(one), PE_start);
  }

  /* broadcast out */
  shmem_internal_bcast(target, target, count * type_size, 0, PE_start, PE_stride, PE_size, pSync + 2, 0);
}


#define chunk_count(id_, count_, npes_) \
  (count_)/(npes_) + ((id_) < (count_) % (_npes))

  void
SwmShmem::shmem_internal_op_to_all_ring(void *target, const void *source, size_t count, size_t type_size,
    int PE_start, int PE_stride, int PE_size,
    void *pWrk, long *pSync)
{
  int group_rank = (shmem_internal_my_pe - PE_start) / PE_stride;
  long one = 1;


  int peer_to_send = PE_start + ((group_rank + 1) % PE_size) * PE_stride;
  int peer_to_recv = PE_start + ((group_rank - 1) < 0 ? PE_size-1 : group_rank - 1) * PE_stride;
  int free_source = 0;

  /* One slot for reduce-scatter and another for the allgather */
  //shmem_internal_assert(SHMEM_REDUCE_SYNC_SIZE >= 2 + SHMEM_BARRIER_SYNC_SIZE);

  if (count == 0) return;

  if (PE_size == 1) {
    if (target != source)
      memcpy(target, source, count*type_size);
    return;
  }

  /* In-place reduction: copy source data to a temporary buffer so we can use
   * the symmetric buffer to accumulate reduced data. */
  /*if (target == source) {
    void *tmp = malloc(count * type_size);

    if (NULL == tmp)
      RAISE_ERROR_MSG("Unable to allocate %zub temporary buffer\n", count*type_size);

    memcpy(tmp, target, count*type_size);
    free_source = 1;
    source = tmp;

    shmem_internal_sync(PE_start, PE_stride, PE_size, pSync + 2);
  }*/

  /* Perform reduce-scatter:
   *
   * The source buffer is divided into PE_size chunks.  PEs send data to the
   * right around the ring, starting with the chunk index equal to the PE id
   * and decreasing.  For example, with 4 PEs, PE 0 sends chunks 0, 3, 2 and
   * PE 1 sends chunks 1, 0, 3.  At the end, each PE has the reduced chunk
   * corresponding to its PE id + 1.
   */
  int max_chunk = 256;
  for (int i = 0; i < PE_size - 1; i++) {
    size_t chunk_in  = (group_rank - i - 1 + PE_size) % PE_size;
    size_t chunk_out = (group_rank - i + PE_size) % PE_size;

    //[> Evenly distribute extra elements across first count % PE_size chunks <]
    size_t chunk_in_extra  = chunk_in  < count % PE_size;
    size_t chunk_out_extra = chunk_out < count % PE_size;
    size_t chunk_in_count  = count/PE_size + chunk_in_extra;
    size_t chunk_out_count = count/PE_size + chunk_out_extra;

    //[> Account for extra elements in the displacement <]
    //size_t chunk_out_disp  = chunk_out_extra ?
      //chunk_out * chunk_out_count * type_size :
      //(chunk_out * chunk_out_count + count % PE_size) * type_size;
    //size_t chunk_in_disp   = chunk_in_extra ?
      //chunk_in * chunk_in_count * type_size :
      //(chunk_in * chunk_in_count + count % PE_size) * type_size;

    //printf("[%d] PUT %d\n",shmem_internal_my_pe, peer_to_send);
    //
    int total_to_send = chunk_out_count * type_size;

    for(int j = 0; j < total_to_send; j+=max_chunk){
        if (total_to_send - j > max_chunk){
            _thread->PUT(max_chunk, peer_to_send);
        } else {
            _thread->PUT(total_to_send - j, peer_to_send);
        }
    }

    //if (shmem_internal_my_pe == 0){
        //cout << shmem_internal_my_pe << ": Finish first part " << endl;
    //}

    /*shmem_internal_put_nbi(SHMEM_CTX_DEFAULT,
        ((uint8_t *) target) + chunk_out_disp,
        i == 0 ?
        ((uint8_t *) source) + chunk_out_disp :
            //
        ((uint8_t *) target) + chunk_out_disp,
        chunk_out_count * type_size, peer);
        */
    //shmem_internal_fence(SHMEM_CTX_DEFAULT);
    //shmem_internal_atomic(SHMEM_CTX_DEFAULT, pSync, &one, sizeof(one),
    //    peer, SHM_INTERNAL_SUM, SHM_INTERNAL_LONG);
    //printf("[%d] SEND %d\n",shmem_internal_my_pe, peer_to_send);
    _thread->SEND(sizeof(one), peer_to_send);
    //_thread->QUIET(); // This may not be needed. Not sure if atomic operation completes when injected into network or arrived at the destination endpoint
    _thread->RECV(peer_to_recv);
    /* Wait for chunk */
    //SHMEM_WAIT_UNTIL(pSync, SHMEM_CMP_GE, i+1);

    // ToDo: add latency for local reduction
    shmem_internal_reduce_local(chunk_in_count, type_size);
    //    ((uint8_t *) source) + chunk_in_disp,
    //    ((uint8_t *) target) + chunk_in_disp);
  }

  /* Reset reduce-scatter pSync */
  //shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync, &zero, sizeof(zero), shmem_internal_my_pe);
  //SHMEM_WAIT_UNTIL(pSync, SHMEM_CMP_EQ, 0);

  /* Perform all-gather:
   *
   * Initially, each PE has the reduced chunk for PE id + 1.  Forward chunks
   * around the ring until all PEs have all chunks.
   */
  for (int i = 0; i < PE_size - 1; i++) {
    size_t chunk_out = (group_rank + 1 - i + PE_size) % PE_size;
    size_t chunk_out_extra = chunk_out < count % PE_size;
    size_t chunk_out_count = count/PE_size + chunk_out_extra;
    //size_t chunk_out_disp  = chunk_out_extra ?
      //chunk_out * chunk_out_count * type_size :
      //(chunk_out * chunk_out_count + count % PE_size) * type_size;

    //printf("[%d] PUT %d\n",shmem_internal_my_pe, peer_to_send);

    int total_to_send = chunk_out_count * type_size;

    for(int i = 0; i < total_to_send; i+=max_chunk){
        if (total_to_send - i > max_chunk){
            _thread->PUT(max_chunk, peer_to_send);
        } else {
            _thread->PUT(total_to_send - i, peer_to_send);
        }
    }

    /*shmem_internal_put_nbi(SHMEM_CTX_DEFAULT,
        ((uint8_t *) target) + chunk_out_disp,
        ((uint8_t *) target) + chunk_out_disp,
        chunk_out_count * type_size, peer);
    shmem_internal_fence(SHMEM_CTX_DEFAULT);
    shmem_internal_atomic(SHMEM_CTX_DEFAULT, pSync+1, &one, sizeof(one),
        peer, SHM_INTERNAL_SUM, SHM_INTERNAL_LONG);
        */
    /* Wait for chunk */
   // SHMEM_WAIT_UNTIL(pSync+1, SHMEM_CMP_GE, i+1);
   //
   // printf("[%d] SEND %d\n",shmem_internal_my_pe, peer_to_send);
    _thread->SEND(sizeof(one), peer_to_send);
    //_thread->QUIET(); // This may not be needed. Not sure if atomic operation completes when injected into network or arrived at the destination endpoint
    _thread->RECV(peer_to_recv);
  }

  /* reset pSync */
  //shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync+1, &zero, sizeof(zero), shmem_internal_my_pe);
  //SHMEM_WAIT_UNTIL(pSync+1, SHMEM_CMP_EQ, 0);

  if (free_source)
    free((void *)source);
}


// hardware accelerated barrier sync
  void
SwmShmem::shmem_internal_op_to_all_xl(void *target, const void *source, size_t count, size_t type_size,
    int PE_start, int PE_stride, int PE_size,
    void *pWrk, long *pSync)
{
  // send a request to accelerator and block on the response
  _thread->ACCELREQ(new CollectiveAccel::Request(_thread, _thread->get_id(), _thread->get_num_pes(), CollectiveAccel::CX_ALLREDUCE, count, type_size));
  _thread->QUIET();
}

#define  SHMEM_REDUCE_SYNC_SIZE 35

  void
SwmShmem::shmem_internal_op_to_all_recdbl_sw(void *target, const void *source, size_t count, size_t type_size,
    int PE_start, int PE_stride, int PE_size,
    void *pWrk, long *pSync)
{
  int my_id = ((shmem_internal_my_pe - PE_start) / PE_stride);
  int log2_proc = 1, pow2_proc = 2;
  int i = PE_size >> 1;
  size_t wrk_size = type_size*count;
  void * const current_target = malloc(wrk_size);
  //long completion = 0;
  //long * pSync_extra_peer = pSync + SHMEM_REDUCE_SYNC_SIZE - 2;

  if (PE_size == 1) {
    if (target != source) {
      memcpy(target, source, type_size*count);
    }
    free(current_target);
    return;
  }

  if (count == 0) {
    free(current_target);
    return;
  }

  while (i != 1) {
    i >>= 1;
    pow2_proc <<= 1;
    log2_proc++;
  }

  /* Currently SHMEM_REDUCE_SYNC_SIZE assumes space for 2^32 PEs; this
     parameter may be changed if need-be */
  //shmem_internal_assert(log2_proc <= (SHMEM_REDUCE_SYNC_SIZE - 2));

  /*if (current_target)
    memcpy(current_target, (void *) source, wrk_size);
  else
    RAISE_ERROR_MSG("Failed to allocate current_target (count=%zu, type_size=%zu, size=%zuB)\n",
        count, type_size, wrk_size);
        */

  /* Algorithm: reduce N number of PE's into a power of two recursive
   * doubling algorithm have extra_peers do the operation with one of the
   * power of two PE's so the information is in the power of two algorithm,
   * at the end, update extra_peers with answer found by power of two team
   *
   * -target is used as "temp" buffer -- current_target tracks latest result
   * give partner current_result,
   */

  /* extra peer exchange: grab information from extra_peer so its part of
   * pairwise exchange */
  if (my_id >= pow2_proc) {
    int peer = (my_id - pow2_proc) * PE_stride + PE_start;

    /* Wait for target ready, required when source and target overlap */
    _thread->RECV(peer);

    _thread->PUT(wrk_size, peer);
    /*shmem_internal_put_nb(SHMEM_CTX_DEFAULT, target, current_target, wrk_size, peer,
        &completion);
    shmem_internal_put_wait(SHMEM_CTX_DEFAULT, &completion);
    shmem_internal_fence(SHMEM_CTX_DEFAULT);
    */

    //shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync_extra_peer, &ps_data_ready, sizeof(long), peer);
    //SHMEM_WAIT_UNTIL(pSync_extra_peer, SHMEM_CMP_EQ, ps_data_ready);
    _thread->SEND(sizeof(long), peer);
    _thread->RECV(peer);

  } else {
    if (my_id < PE_size - pow2_proc) {
      int peer = (my_id + pow2_proc) * PE_stride + PE_start;
      //shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync_extra_peer, &ps_target_ready, sizeof(long), peer);
      _thread->SEND(sizeof(long),peer);

      _thread->RECV(peer);
      //SHMEM_WAIT_UNTIL(pSync_extra_peer, SHMEM_CMP_EQ, ps_data_ready);
      shmem_internal_reduce_local(count, type_size);
    }

    /* Pairwise exchange: (only for PE's that are within the power of 2
     * set) with every iteration, the information from each previous
     * exchange is passed forward in the new interation */

    for (i = 0; i < log2_proc; i++) {
      //long *step_psync = &pSync[i];
      int peer = (my_id ^ (1 << i)) * PE_stride + PE_start;

      if (shmem_internal_my_pe < peer) {
        _thread->SEND(sizeof(long), peer);
        _thread->RECV(peer);
        //shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, step_psync, &ps_target_ready,
        //    sizeof(long), peer);
        //SHMEM_WAIT_UNTIL(step_psync, SHMEM_CMP_EQ, ps_data_ready);

        _thread->PUT(wrk_size,peer);
        _thread->SEND(sizeof(long),peer);
        /*shmem_internal_put_nb(SHMEM_CTX_DEFAULT, target, current_target,
            wrk_size, peer, &completion);
        shmem_internal_put_wait(SHMEM_CTX_DEFAULT, &completion);
        shmem_internal_fence(SHMEM_CTX_DEFAULT);
        shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, step_psync, &ps_data_ready,
            sizeof(long), peer);
        */
      }
      else {
        //SHMEM_WAIT_UNTIL(step_psync, SHMEM_CMP_EQ, ps_target_ready);
        _thread->RECV(peer);

        _thread->PUT(wrk_size, peer);
        _thread->SEND(sizeof(long), peer);
        /*shmem_internal_put_nb(SHMEM_CTX_DEFAULT, target, current_target,
            wrk_size, peer, &completion);
        shmem_internal_put_wait(SHMEM_CTX_DEFAULT, &completion);
        shmem_internal_fence(SHMEM_CTX_DEFAULT);
        shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, step_psync, &ps_data_ready,
            sizeof(long), peer);
        */

        _thread->RECV(peer);
        //SHMEM_WAIT_UNTIL(step_psync, SHMEM_CMP_EQ, ps_data_ready);
      }
      
      // ToDo: add latency for local reduction
      shmem_internal_reduce_local(count, type_size);
    }

    /* update extra peer with the final result from the pairwise exchange */
    if (my_id < PE_size - pow2_proc) {
      int peer = (my_id + pow2_proc) * PE_stride + PE_start;

      _thread->PUT(wrk_size, peer);
      /*shmem_internal_put_nb(SHMEM_CTX_DEFAULT, target, current_target, wrk_size,
          peer, &completion);
      shmem_internal_put_wait(SHMEM_CTX_DEFAULT, &completion);
      shmem_internal_fence(SHMEM_CTX_DEFAULT);
      */
      _thread->SEND(sizeof(long), peer);
      //shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync_extra_peer, &ps_data_ready,
      //    sizeof(long), peer);
    }

    //memcpy(target, current_target, wrk_size);
  }

  free(current_target);

  //for (i = 0; i < SHMEM_REDUCE_SYNC_SIZE; i++)
  //  pSync[i] = SHMEM_SYNC_VALUE;
}


#if 0
/*****************************************
 *
 * COLLECT (variable size)
 *
 *****************************************/
  void
shmem_internal_collect_linear(void *target, const void *source, size_t len,
    int PE_start, int PE_stride, int PE_size, long *pSync)
{
  size_t my_offset;
  long tmp[2];
  int peer, start_pe, i;

  /* Need 2 for lengths and barrier for completion */
  shmem_internal_assert(SHMEM_COLLECT_SYNC_SIZE >= 2 + SHMEM_BARRIER_SYNC_SIZE);

  DEBUG_MSG("target=%p, source=%p, len=%zd, PE_Start=%d, PE_stride=%d, PE_size=%d, pSync=%p\n",
      target, source, len, PE_start, PE_stride, PE_size, (void*) pSync);

  if (PE_size == 1) {
    if (target != source) memcpy(target, source, len);
    return;
  }

  /* Linear prefix sum -- propagate update lengths and calculate offset */
  if (PE_start == shmem_internal_my_pe) {
    my_offset = 0;
    tmp[0] = (long) len; /* FIXME: Potential truncation of size_t into long */
    tmp[1] = 1; /* FIXME: Packing flag with data relies on byte ordering */
    shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync, tmp, 2 * sizeof(long), PE_start + PE_stride);
  }
  else {
    /* wait for send data */
    SHMEM_WAIT_UNTIL(&pSync[1], SHMEM_CMP_EQ, 1);
    my_offset = pSync[0];

    /* Not the last guy, so send offset to next PE */
    if (shmem_internal_my_pe < PE_start + PE_stride * (PE_size - 1)) {
      tmp[0] = (long) (my_offset + len);
      tmp[1] = 1;
      shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync, tmp, 2 * sizeof(long),
          shmem_internal_my_pe + PE_stride);
    }
  }

  /* Send data round-robin, ending with my PE */
  start_pe = shmem_internal_circular_iter_next(shmem_internal_my_pe,
      PE_start, PE_stride,
      PE_size);
  peer = start_pe;
  do {
    if (len > 0) {
      shmem_internal_put_nbi(SHMEM_CTX_DEFAULT, ((uint8_t *) target) + my_offset, source,
          len, peer);
    }
    peer = shmem_internal_circular_iter_next(peer, PE_start, PE_stride,
        PE_size);
  } while (peer != start_pe);

  shmem_internal_barrier(PE_start, PE_stride, PE_size, &pSync[2]);

  pSync[0] = SHMEM_SYNC_VALUE;
  pSync[1] = SHMEM_SYNC_VALUE;

  for (i = 0; i < SHMEM_BARRIER_SYNC_SIZE; i++)
    pSync[2+i] = SHMEM_SYNC_VALUE;
}


/*****************************************
 *
 * COLLECT (same size)
 *
 *****************************************/
  void
shmem_internal_fcollect_linear(void *target, const void *source, size_t len,
    int PE_start, int PE_stride, int PE_size, long *pSync)
{
  long tmp = 1;
  long completion = 0;

  /* need 1 slot, plus bcast */
  shmem_internal_assert(SHMEM_COLLECT_SYNC_SIZE >= 1 + SHMEM_BCAST_SYNC_SIZE);

  if (PE_start == shmem_internal_my_pe) {
    /* Copy data into the target */
    if (source != target) memcpy(target, source, len);

    /* send completion update */
    shmem_internal_atomic(SHMEM_CTX_DEFAULT, pSync, &tmp, sizeof(long),
        PE_start, SHM_INTERNAL_SUM, SHM_INTERNAL_LONG);

    /* wait for N updates */
    SHMEM_WAIT_UNTIL(pSync, SHMEM_CMP_EQ, PE_size);

    /* Clear pSync */
    tmp = 0;
    shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync, &tmp, sizeof(tmp), PE_start);
    SHMEM_WAIT_UNTIL(pSync, SHMEM_CMP_EQ, 0);
  } else {
    /* Push data into the target */
    size_t offset = ((shmem_internal_my_pe - PE_start) / PE_stride) * len;
    shmem_internal_put_nb(SHMEM_CTX_DEFAULT, (char*) target + offset, source, len, PE_start,
        &completion);
    shmem_internal_put_wait(SHMEM_CTX_DEFAULT, &completion);

    /* ensure ordering */
    shmem_internal_fence(SHMEM_CTX_DEFAULT);

    /* send completion update */
    shmem_internal_atomic(SHMEM_CTX_DEFAULT, pSync, &tmp, sizeof(long),
        PE_start, SHM_INTERNAL_SUM, SHM_INTERNAL_LONG);
  }

  shmem_internal_bcast(target, target, len * PE_size, 0, PE_start, PE_stride,
      PE_size, pSync + 1, 0);
}


/* Ring algorithm, in which every process sends only to its next
 * highest neighbor, each time sending the data it received in the
 * previous iteration.  This algorithm works regardless of process
 * count and is efficient at larger message sizes.
 *
 *   (p - 1) alpha + ((p - 1)/p)n beta
 */
  void
shmem_internal_fcollect_ring(void *target, const void *source, size_t len,
    int PE_start, int PE_stride, int PE_size, long *pSync)
{
  int i;
  /* my_id is the index in a theoretical 0...N-1 array of
     participating tasks */
  int my_id = ((shmem_internal_my_pe - PE_start) / PE_stride);
  int next_proc = PE_start + ((my_id + 1) % PE_size) * PE_stride;
  long completion = 0;
  long zero = 0, one = 1;

  /* need 1 slot */
  shmem_internal_assert(SHMEM_COLLECT_SYNC_SIZE >= 1);

  if (len == 0) return;

  /* copy my portion to the right place */
  memcpy((char*) target + (my_id * len), source, len);

  /* send n - 1 messages to the next highest proc.  Each message
     contains what we received the previous step (including our own
     data for step 1). */
  for (i = 1 ; i < PE_size ; ++i) {
    size_t iter_offset = ((my_id + 1 - i + PE_size) % PE_size) * len;

    /* send data to me + 1 */
    shmem_internal_put_nb(SHMEM_CTX_DEFAULT, (char*) target + iter_offset, (char*) target + iter_offset,
        len, next_proc, &completion);
    shmem_internal_put_wait(SHMEM_CTX_DEFAULT, &completion);
    shmem_internal_fence(SHMEM_CTX_DEFAULT);

    /* send completion for this round to next proc.  Note that we
       only ever sent to next_proc and there's a shmem_fence
       between successive calls to the put above.  So a rolling
       counter is safe here. */
    shmem_internal_atomic(SHMEM_CTX_DEFAULT, pSync, &one, sizeof(long),
        next_proc, SHM_INTERNAL_SUM, SHM_INTERNAL_LONG);

    /* wait for completion for this round */
    SHMEM_WAIT_UNTIL(pSync, SHMEM_CMP_GE, i);
  }

  /* zero out psync */
  shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, pSync, &zero, sizeof(long), shmem_internal_my_pe);
  SHMEM_WAIT_UNTIL(pSync, SHMEM_CMP_EQ, 0);
}


/* recursive doubling algorithm.  Pairs of doubling distance send
 * doubling amounts of data at each step.  This implementation only
 * supports power of two processes and is less efficient than the ring
 * algorithm at large messages.
 *
 *   log(p) alpha + (p-1)/p n beta
 */
  void
shmem_internal_fcollect_recdbl(void *target, const void *source, size_t len,
    int PE_start, int PE_stride, int PE_size, long *pSync)
{
  int my_id = ((shmem_internal_my_pe - PE_start) / PE_stride);
  int i;
  long completion = 0;
  size_t curr_offset;
  int *pSync_ints = (int*) pSync;
  int one = 1, neg_one = -1;
  int distance;

  /* need log2(num_procs) int slots.  max_num_procs is
     2^(sizeof(int)*8-1)-1, so make the math a bit easier and assume
     2^(sizeof(int) * 8), which means log2(num_procs) is always less
     than sizeof(int) * 8. */
  /* Note: pSync can be treated as a byte array rather than an int array to
   * get better cache locality.  We chose int here for portability, since SUM
   * on INT is required by the SHMEM atomics API. */
  shmem_internal_assert(SHMEM_COLLECT_SYNC_SIZE >= (sizeof(int) * 8) / (sizeof(long) / sizeof(int)));
  shmem_internal_assert(0 == (PE_size & (PE_size - 1)));

  if (len == 0) return;

  /* copy my portion to the right place */
  curr_offset = my_id * len;
  memcpy((char*) target + curr_offset, source, len);

  for (i = 0, distance = 0x1 ; distance < PE_size ; i++, distance <<= 1) {
    int peer = my_id ^ distance;
    int real_peer = PE_start + (peer * PE_stride);

    /* send data to peer */
    shmem_internal_put_nb(SHMEM_CTX_DEFAULT, (char*) target + curr_offset, (char*) target + curr_offset,
        distance * len, real_peer, &completion);
    shmem_internal_put_wait(SHMEM_CTX_DEFAULT, &completion);
    shmem_internal_fence(SHMEM_CTX_DEFAULT);

    /* mark completion for this round */
    shmem_internal_atomic(SHMEM_CTX_DEFAULT, &pSync_ints[i], &one, sizeof(int),
        real_peer, SHM_INTERNAL_SUM, SHM_INTERNAL_INT);

    SHMEM_WAIT_UNTIL(&pSync_ints[i], SHMEM_CMP_NE, 0);

    /* this slot is no longer used, so subtract off results now */
    shmem_internal_atomic(SHMEM_CTX_DEFAULT, &pSync_ints[i], &neg_one, sizeof(int),
        shmem_internal_my_pe, SHM_INTERNAL_SUM, SHM_INTERNAL_INT);

    if (my_id > peer) {
      curr_offset -= (distance * len);
    }
  }

  shmem_internal_quiet(SHMEM_CTX_DEFAULT);
}


  void
shmem_internal_alltoall(void *dest, const void *source, size_t len,
    int PE_start, int PE_stride, int PE_size, long *pSync)
{
  const int my_as_rank = (shmem_internal_my_pe - PE_start) / PE_stride;
  const void *dest_ptr = (uint8_t *) dest + my_as_rank * len;
  int peer, start_pe, i;

  shmem_internal_assert(SHMEM_ALLTOALL_SYNC_SIZE >= SHMEM_BARRIER_SYNC_SIZE);

  if (0 == len)
    return;

  /* Send data round-robin, ending with my PE */
  start_pe = shmem_internal_circular_iter_next(shmem_internal_my_pe,
      PE_start, PE_stride,
      PE_size);
  peer = start_pe;
  do {
    int peer_as_rank = (peer - PE_start) / PE_stride; /* Peer's index in active set */

    shmem_internal_put_nbi(SHMEM_CTX_DEFAULT, (void *) dest_ptr, (uint8_t *) source + peer_as_rank * len,
        len, peer);
    peer = shmem_internal_circular_iter_next(peer, PE_start, PE_stride,
        PE_size);
  } while (peer != start_pe);

  shmem_internal_barrier(PE_start, PE_stride, PE_size, pSync);

  for (i = 0; i < SHMEM_BARRIER_SYNC_SIZE; i++)
    pSync[i] = SHMEM_SYNC_VALUE;
}


  void
shmem_internal_alltoalls(void *dest, const void *source, ptrdiff_t dst,
    ptrdiff_t sst, size_t elem_size, size_t nelems,
    int PE_start, int PE_stride, int PE_size, long *pSync)
{
  const int my_as_rank = (shmem_internal_my_pe - PE_start) / PE_stride;
  const void *dest_base = (uint8_t *) dest + my_as_rank * nelems * dst * elem_size;
  int peer, start_pe, i;

  shmem_internal_assert(SHMEM_ALLTOALLS_SYNC_SIZE >= SHMEM_BARRIER_SYNC_SIZE);

  if (0 == nelems)
    return;

  /* Implementation note: Neither OFI nor Portals presently has support for
   * noncontiguous data at the target of a one-sided operation.  I'm not sure
   * of the best communication schedule for the resulting doubly-nested
   * all-to-all.  It may be preferable in some scenarios to exchange the
   * loops below to spread out the communication and decrease the exposure to
   * incast.
   */

  /* Send data round-robin, ending with my PE */
  start_pe = shmem_internal_circular_iter_next(shmem_internal_my_pe,
      PE_start, PE_stride,
      PE_size);
  peer = start_pe;
  do {
    size_t i;
    int peer_as_rank    = (peer - PE_start) / PE_stride; /* Peer's index in active set */
    uint8_t *dest_ptr   = (uint8_t *) dest_base;
    uint8_t *source_ptr = (uint8_t *) source + peer_as_rank * nelems * sst * elem_size;

    for (i = nelems ; i > 0; i--) {
      shmem_internal_put_scalar(SHMEM_CTX_DEFAULT, (void *) dest_ptr, (uint8_t *) source_ptr,
          elem_size, peer);

      source_ptr += sst * elem_size;
      dest_ptr   += dst * elem_size;
    }
    peer = shmem_internal_circular_iter_next(peer, PE_start, PE_stride,
        PE_size);
  } while (peer != start_pe);

  shmem_internal_barrier(PE_start, PE_stride, PE_size, pSync);

  for (i = 0; i < SHMEM_BARRIER_SYNC_SIZE; i++)
    pSync[i] = SHMEM_SYNC_VALUE;
}
#endif
