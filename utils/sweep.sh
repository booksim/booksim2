#!/bin/sh

# $Id$

# Copyright (c) 2007-2011, Trustees of The Leland Stanford Junior University
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# This is a helper script that performs a latency-throughput sweep.
#
# It takes a complete booksim commandline as its parameter.
#
# Example:
#
#  ./sweep.sh ./booksim configfile
#
# The script implicitly enables BookSim's 'print_csv_results' parameter; 
# result data can be gathered from standard output by grepping for lines that 
# start with "results:"; miscellaneous status information for the script 
# itself is printed out in lines that begin with "SWEEP: ".

if [ "${1}" = "" ]
then
    echo "SWEEP: Please specify a simulator executable as the first parameter."
    exit
fi

sim=${1}
shift

if [ "${initial_step}" = "" ]
then
    initial_step=500
fi
if [ "${scale}" = "" ]
then
    scale=10000
fi
if [ "${zero_load_inj}" = "" ]
then
    zero_load_inj=10
fi
if [ "${no_backtrack}" = "" ]
then
    no_backtrack=0
fi
if [ "${no_addint}" = "" ]
then
    no_addint=0
fi

inj_rate="`awk "BEGIN{ print ${zero_load_inj} / ${scale} }"`"

echo "SWEEP: Determining zero-load latency..."
${sim} $* print_csv_results=1 injection_rate=${inj_rate} | tee ${sim}.${HOSTNAME}.${$}.log
lat=`grep "results:" ${sim}.${HOSTNAME}.${$}.log | cut -d , -f 5`
rm ${sim}.${HOSTNAME}.${$}.log
if [ "${lat}" = "" ]
then
    echo "SWEEP: Simulation run failed."
    echo "SWEEP: Aborting."
    exit
fi
echo "SWEEP: Simulation run succeeded."
echo "SWEEP: Zero-load latency is ${lat}."

step=${initial_step}
old_inj=${zero_load_inj}
old_lat=${lat}
ref_delta="unset"
inj=${step}
last_fail=${scale}

step_size="`awk "BEGIN{ print ${step} / ${scale} }"`"
echo "SWEEP: Sweeping with initial step size ${step_size}"

while true
do
    inj_rate="`awk "BEGIN{ print ${inj} / ${scale} }"`"
    if [ ${inj} -ge ${last_fail} ]
    then
	echo "SWEEP: Already failed for injection rate ${inj_rate} before."
	echo "SWEEP: Simulation run skipped."
	lat=""
    else
	echo "SWEEP: Simulating for injection rate ${inj_rate}..."
	${sim} $* print_csv_results=1 injection_rate=${inj_rate} | tee ${sim}.${HOSTNAME}.${$}.log
	lat=`grep "results:" ${sim}.${HOSTNAME}.${$}.log | cut -d , -f 5`
	rm ${sim}.${HOSTNAME}.${$}.log
    fi
    if [ "${lat}" = "" ]
    then
	echo "SWEEP: Simulation run failed."
	if [ ${no_backtrack} -ge 1 ]
	then
	    break
	fi
	echo "SWEEP: Reducing step size."
	step="`awk "BEGIN{ print ${step} / 2 }"`"
	if [ ${step} -eq 0 ]
	then
	    echo "SWEEP: Step size too small."
	    break
	fi
	last_fail=${inj}
	step_size="`awk "BEGIN{ print ${step} / ${scale} }"`"
	echo "SWEEP: New step size is ${step_size}."
	inj="`awk "BEGIN{ print ${old_inj} + ${step} }"`"
	continue
    fi
    echo "SWEEP: Simulation run succeeded."
    echo "SWEEP: Latency is ${lat}."
    if [ ${no_addint} -eq 0 ]
    then
	delta="`awk "BEGIN{ print sqrt((20*(${lat} - ${old_lat}))^2+(${inj} - ${old_inj})^2) }"`"
	if [ "${ref_delta}" = "unset" ]
	then
	    ref_delta=${delta}
	    echo "SWEEP: Setting reference distance to ${delta}."
	else
	    echo "SWEEP: Distance was ${delta}."
	    ratio="`awk "BEGIN{ print ${delta} / (1.5 * ${ref_delta}) }"`"
	    steps=`printf "%1.0f" ${ratio}`
	    if [ ${steps} -gt 1 ]
	    then
		echo "SWEEP: Adding ${steps} intermediate steps..."
		ref_step="`awk "BEGIN{ print ${step} / ${steps} }"`"
		if [ ${ref_step} -gt 0 ]
		then
#		    step=${ref_step}
#		    step_size="`awk "BEGIN{ print ${step} / ${scale} }"`"
		    step_size="`awk "BEGIN{ print ${ref_step} / ${scale} }"`"
#		    echo "SWEEP: New step size is 
		    echo "SWEEP: Intermediate step size is ${step_size}."
#		    ref_inj="`awk "BEGIN{ print ${old_inj} + ${step} }"`"
		    ref_inj="`awk "BEGIN{ print ${old_inj} + ${ref_step} }"`"
		    while [ ${ref_inj} -lt ${inj} ]
		    do
			inj_rate="`awk "BEGIN{ print ${ref_inj} / ${scale} }"`"
			echo "SWEEP: Simulating for injection rate ${inj_rate}..."
			ref_inj="`awk "BEGIN{ print ${ref_inj} + ${ref_step} }"`"
			${sim} $* print_csv_results=1 injection_rate=${inj_rate} | tee ${sim}.${HOSTNAME}.${$}.log
			intm_lat=`grep "results:" ${sim}.${HOSTNAME}.${$}.log | cut -d , -f 5`
			rm ${sim}.${HOSTNAME}.${$}.log
			if [ "${intm_lat}" = "" ]
			then
			    echo "SWEEP: Simulation failed unexpectedly."
			    continue
			fi
			echo "SWEEP: Simulation run succeeded."
			echo "SWEEP: Latency is ${intm_lat}."
		    done
		else
		    echo "SWEEP: Refinement step too small."
		fi
		echo "SWEEP: Done adding intermediate steps."
	    fi
	fi
    fi

    old_inj=${inj}
    old_lat=${lat}
    inj="`awk "BEGIN{ print ${inj}+${step} }"`"

done

echo "SWEEP: Parameter sweep complete."
