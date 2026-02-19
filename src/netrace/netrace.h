/*
 * Copyright (c) 2010-2011 The University of Texas at Austin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NETRACE_H_
#define NETRACE_H_

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Macro Definitions
//#define DEBUG_ON
#define NT_MAGIC 0x484A5455
#define NT_BMARK_NAME_LENGTH 30
#define NT_DEPENDENCY_ARRAY_SIZE 200
#define nt_checked_malloc(x) _nt_checked_malloc(x,__FILE__,__LINE__)
#define nt_error(x) _nt_error(x,__FILE__,__LINE__)
#define NT_NUM_PACKET_TYPES	31
#define NT_NUM_NODE_TYPES	4
#define NT_NODE_TYPE_L1D	0
#define NT_NODE_TYPE_L1I	1
#define NT_NODE_TYPE_L2		2
#define NT_NODE_TYPE_MC		3
#define NT_READ_AHEAD		1000000

// Type Declaration
typedef unsigned int nt_dependency_t;
typedef struct nt_header nt_header_t;
typedef struct nt_regionhead nt_regionhead_t;
typedef struct nt_packet nt_packet_t;
typedef struct nt_dep_ref_node nt_dep_ref_node_t;
typedef struct nt_packet_list nt_packet_list_t;
typedef struct nt_context nt_context_t;

struct nt_header {
	unsigned int nt_magic;
	float version;
	char benchmark_name[NT_BMARK_NAME_LENGTH];
	unsigned char num_nodes;
	unsigned long long int num_cycles;
	unsigned long long int num_packets;
	unsigned int notes_length;  // Includes null-terminating char
	unsigned int num_regions;
	char* notes;
	nt_regionhead_t* regions;
};

struct nt_regionhead {
	unsigned long long int seek_offset;
	unsigned long long int num_cycles;
	unsigned long long int num_packets;
};

struct nt_packet {
	unsigned long long int cycle;
	unsigned int id;
	unsigned int addr;
	unsigned char type;
	unsigned char src;
	unsigned char dst;
	unsigned char node_types;
	unsigned char num_deps;
	nt_dependency_t* deps;
};

struct nt_dep_ref_node {
	nt_packet_t* node_packet;
	unsigned int packet_id;
	unsigned int ref_count;
	nt_dep_ref_node_t* next_node;
};

struct nt_packet_list {
	nt_packet_t* node_packet;
	nt_packet_list_t* next;
};

struct nt_context {
  char*	input_popencmd;
  FILE*	input_tracefile;
  char*	input_buffer;
  nt_header_t*	input_trheader;
  int	dependencies_off;
  int	self_throttling;
  int	primed_self_throttle;
  int	done_reading;
  unsigned long long int latest_active_packet_cycle;
  nt_dep_ref_node_t** dependency_array;
  unsigned long long int num_active_packets;
  nt_packet_list_t*	cleared_packets_list;
  nt_packet_list_t*	cleared_packets_list_tail;
  int track_cleared_packets_list;
};

// Data Members
extern const char* nt_packet_types[];
extern int nt_packet_sizes[];
extern const char* nt_node_types[];

// Interface Functions
void			nt_open_trfile( nt_context_t*, const char* );
void			nt_disable_dependencies( nt_context_t* );
void			nt_seek_region( nt_context_t*, nt_regionhead_t* );
nt_packet_t*		nt_read_packet( nt_context_t* );
int			nt_dependencies_cleared( nt_context_t*, nt_packet_t* );
void			nt_clear_dependencies_free_packet( nt_context_t*, nt_packet_t* );
void			nt_close_trfile( nt_context_t* );
void			nt_init_cleared_packets_list( nt_context_t* );
void			nt_init_self_throttling( nt_context_t* );
nt_packet_list_t*	nt_get_cleared_packets_list( nt_context_t* );
void			nt_empty_cleared_packets_list( nt_context_t* );

// Utility Functions
void			nt_print_trheader( nt_context_t* );
void			nt_print_packet( nt_packet_t* );
nt_header_t*		nt_get_trheader( nt_context_t* );
float			nt_get_trversion( nt_context_t* );
int			nt_get_src_type( nt_packet_t* );
int			nt_get_dst_type( nt_packet_t* );
const char* 		nt_node_type_to_string( int );
const char* 		nt_packet_type_to_string( nt_packet_t* );
int			nt_get_packet_size( nt_packet_t* );

// Netrace Internal Helper Functions
int			nt_little_endian( void );
nt_header_t*		nt_read_trheader( nt_context_t* );
void			nt_print_header( nt_context_t*, nt_header_t* );
void			nt_free_trheader( nt_header_t* );
int			nt_get_headersize( nt_context_t* );
nt_packet_t*		nt_packet_malloc( void );
nt_dependency_t*	nt_dependency_malloc( unsigned char );
nt_dep_ref_node_t*	nt_get_dependency_node( nt_context_t*, unsigned int );
nt_dep_ref_node_t*	nt_add_dependency_node( nt_context_t*, unsigned int );
nt_packet_t*		nt_remove_dependency_node( nt_context_t*, unsigned int );
void			nt_delete_all_dependencies( nt_context_t* );
nt_packet_t*		nt_packet_copy( nt_packet_t* );
void			nt_packet_free( nt_packet_t* );
void			nt_read_ahead( nt_context_t*, unsigned long long int );
void			nt_prime_self_throttle( nt_context_t* );
void			nt_add_cleared_packet_to_list( nt_context_t*, nt_packet_t* );
void*			_nt_checked_malloc( size_t, char*, int ); // Use the macro defined above instead of this function
void			_nt_error( const char*, char*, int ); // Use the macro defined above instead of this functio

// Backend functions for creating trace files
void	nt_dump_header( nt_header_t*, FILE* );
void	nt_dump_packet( nt_packet_t*, FILE* );

#endif /*NETRACE_H_*/
