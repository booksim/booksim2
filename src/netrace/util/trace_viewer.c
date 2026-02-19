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

#include <stdio.h>
#include <string.h>
#include "../netrace.h"

#define xtod(c) ((c>='0' && c<='9') ? c-'0' : ((c>='A' && c<='F') ? \
                c-'A'+10 : ((c>='a' && c<='f') ? c-'a'+10 : 0)))

void print_usage(void);
int print_packet(nt_packet_t*);
int xstrtoi( char *hex );

int read_flag = 0;
int print_flag = 0;
int verify_flag = 0;
int deps_flag = 0;
int node_id_to_trace = -1;
int node_type_to_trace = -1;
long long int address_to_trace = -1;
unsigned long long int min_cycle_to_trace = 0;
unsigned long long int max_cycle_to_trace = -1;

nt_context_t* ctx;

int main( int argc, char** argv ) {
	int i;
	unsigned long long int num_dependencies = 0;
	unsigned long long int dep_distance = 0;
	unsigned int verify_id = 0;
	unsigned long long int verify_cycle = 0;
	if( argc < 2 ) {
		print_usage();
	}
	for( i = 2; i < argc; i++ ) {
		if( strcmp(argv[i], "-h") == 0 ) {
			print_usage();
		} else if( strcmp(argv[i], "-d") == 0 ) {
			read_flag = 1;
			deps_flag = 1;
		} else if( strcmp(argv[i], "-p") == 0 ) {
			read_flag = 1;
			print_flag = 1;
		} else if( strcmp(argv[i], "-v") == 0 ) {
			read_flag = 1;
			verify_flag = 1;
		} else if( strcmp(argv[i], "-n") == 0 ) {
			if( argc > ++i ) {
				node_id_to_trace = atoi(argv[i]);
				read_flag = 1;
				print_flag = 1;
			} else {
				fprintf( stderr, "ERROR: Must specify node number to -n\n" );
				print_usage();
			}
		} else if( strcmp(argv[i], "-t") == 0 ) {
			if( argc > ++i ) {
				node_type_to_trace = atoi(argv[i]);
				read_flag = 1;
				print_flag = 1;
			} else {
				fprintf( stderr, "ERROR: Must specify node type to -t\n" );
				print_usage();
			}
		} else if( strcmp(argv[i], "-a") == 0 ) {
			if( argc > ++i ) {
				address_to_trace = xstrtoi(argv[i]);
				read_flag = 1;
				print_flag = 1;
			} else {
				fprintf( stderr, "ERROR: Must specify node type to -t\n" );
				print_usage();
			}
		} else if( strcmp(argv[i], "-m") == 0 ) {
			if( argc > ++i ) {
				min_cycle_to_trace = atol(argv[i]);
				read_flag = 1;
				print_flag = 1;
			} else {
				fprintf( stderr, "ERROR: Must specify node type to -t\n" );
				print_usage();
			}
		} else if( strcmp(argv[i], "-M") == 0 ) {
			if( argc > ++i ) {
				max_cycle_to_trace = atol(argv[i]);
				read_flag = 1;
				print_flag = 1;
			} else {
				fprintf( stderr, "ERROR: Must specify node type to -t\n" );
				print_usage();
			}
		}
	}
	ctx = calloc( 1, sizeof(nt_context_t) );
	nt_open_trfile( ctx, argv[1] );
	nt_print_trheader( ctx );
	// @todo on verify flag, check region info by reading all packets
	if( read_flag ) {
		nt_packet_t* packet = nt_read_packet( ctx );
		verify_id = packet->id;
		for( /*packet = nt_read_packet()*/; packet != NULL; packet = nt_read_packet( ctx ) ) {
			if( print_flag ) {
				if( print_packet( packet ) ) {
					nt_print_packet( packet );
				}
			}
			if( verify_flag ) {
				if( packet->id != verify_id++ ) {
					nt_print_packet( packet );
					nt_error( "Packet ID out of sequence" );
				}
				if( packet->cycle < verify_cycle ) {
					nt_print_packet( packet );
					nt_error( "Packet cycle out of sequence" );
				}
			}
			if( deps_flag ) {
				num_dependencies += packet->num_deps;
				for( i = 0; i < packet->num_deps; i++ ) {
					dep_distance += (packet->deps[i] - packet->id);
				}
			}
			nt_clear_dependencies_free_packet( ctx, packet );
		}
	}
	nt_close_trfile( ctx );
	free( ctx );
	if( deps_flag ) {
		float avg_dep_distance = (float)dep_distance / (float)num_dependencies;
		printf( "Number of Dependencies: %llu\n", num_dependencies );
		printf( "Average Dependency ID Distance: %f\n", avg_dep_distance );
	}
	return 0;
}

void print_usage() {
	fprintf( stderr, "\nUtility to read and print the header of specified trace file\n" );
	fprintf( stderr, "Usage:\n" );
	fprintf( stderr, "\t./trace_viewer <trace_file> [options]\n" );
	fprintf( stderr, "options:\n" );
	fprintf( stderr, "\t-a #      Only print packets accessing a specified address\n" );
	fprintf( stderr, "\t-h        Print this help message\n" );
	fprintf( stderr, "\t-n #      Only print packets to or from node specified\n" );
	fprintf( stderr, "\t-p        Print complete trace\n" );
	fprintf( stderr, "\t-t #      Only print packets to or from node type specified:\n" );
	fprintf( stderr, "\t   0      L1 Data Cache\n" );
	fprintf( stderr, "\t   1      L1 Instruction Cache\n" );
	fprintf( stderr, "\t   2      L2 Cache\n" );
	fprintf( stderr, "\t   3      Memory Controller\n" );
	fprintf( stderr, "\t-v        Verify the trace correctness\n" );
	fprintf( stderr, "\n" );
	exit(0);
}

int HextoDec( char *hex ) {
    if( *hex == 0 ) return 0;
    return HextoDec(hex-1) * 16 + xtod(*hex);
}

int xstrtoi( char *hex ) {
    return HextoDec( hex + strlen(hex) - 1 );
}

int print_packet( nt_packet_t* packet ) {
	int to_return = 1;
	if( node_id_to_trace >= 0 ) {
		if( node_type_to_trace >= 0 ) {
			if( ( packet->src == node_id_to_trace && nt_get_src_type(packet) == node_type_to_trace ) ||
				( packet->dst == node_id_to_trace && nt_get_dst_type(packet) == node_type_to_trace ) ) {
//				return 1;
			} else {
				to_return = 0;
			}
		} else {
			if( ( packet->src == node_id_to_trace ) || ( packet->dst == node_id_to_trace ) ) {
//				return 1;
			} else {
				to_return = 0;
			}
		}
	} else {
		if( node_type_to_trace >= 0 ) {
			if( ( nt_get_src_type(packet) == node_type_to_trace ) ||
				( nt_get_dst_type(packet) == node_type_to_trace ) ) {
//				return 1;
			} else {
				to_return = 0;
			}
		}
	}
	if( packet->cycle < min_cycle_to_trace || packet->cycle > max_cycle_to_trace ) {
		to_return = 0;
	}
	if( address_to_trace >= 0 && packet->addr != address_to_trace ) {
		to_return = 0;
	}
	return to_return;
}