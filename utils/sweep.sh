#!/bin/sh

# $Id$

# Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
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
# Neither the name of the Stanford University nor the names of its contributors
# may be used to endorse or promote products derived from this software without
# specific prior written permission.
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
# start with "results:"; miscellaneous status information for the script itself 
# is printed out in lines that begin with "SWEEP: ".

sim=${1}
shift

initial_step=500
zero_load_inj=10

inj_rate="0"`echo "scale=4; ${zero_load_inj} / 10000" | bc`

echo "SWEEP: Determining zero-load latency..."
${sim} $* print_csv_results=1 injection_rate=${inj_rate} | tee ${sim}.${$}.log
lat=`grep "results:" ${sim}.${$}.log | cut -d , -f 8`
rm ${sim}.${$}.log
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
last_fail=10000

step_size="0"`echo "scale=4; ${step} / 10000" | bc`
echo "SWEEP: Sweeping with initial step size ${step_size}"

while true
do
    inj_rate="0"`echo "scale=4; ${inj} / 10000" | bc`
    if [ ${inj} -ge ${last_fail} ]
    then
	echo "SWEEP: Already failed for injection rate ${inj_rate} before."
	echo "SWEEP: Simulation run skipped."
	lat=""
    else
	echo "SWEEP: Simulating for injection rate ${inj_rate}..."
	${sim} $* print_csv_results=1 injection_rate=${inj_rate} | tee ${sim}.${$}.log
	lat=`grep "results:" ${sim}.${$}.log | cut -d , -f 8`
	rm ${sim}.${$}.log
    fi
    if [ "${lat}" = "" ]
    then
	echo "SWEEP: Simulation run failed."
	echo "SWEEP: Reducing step size."
	step=$[${step}/2];
	if [ ${step} -eq 0 ]
	then
	    echo "SWEEP: Step size too small."
	    break;
	fi
	last_fail=${inj}
	step_size="0"`echo "scale=4; ${step} / 10000" | bc`
	echo "SWEEP: New step size is ${step_size}."
	inj=$[${old_inj} + ${step}]
	continue
    fi
    echo "SWEEP: Simulation run succeeded."
    echo "SWEEP: Latency is ${lat}."
    delta=`echo "scale=4; sqrt((20*(${lat} - ${old_lat}))^2+(${inj} - ${old_inj})^2)" | bc`
    if [ "${ref_delta}" = "unset" ]
    then
	ref_delta=${delta}
	echo "SWEEP: Setting reference distance to ${delta}."
    else
	echo "SWEEP: Distance was ${delta}."
	ratio=`echo "scale=4; ${delta} / (1.5 * ${ref_delta})" | bc`
	steps=`printf "%1.0f" ${ratio}`
	if [ ${steps} -gt 1 ]
	then
	    echo "SWEEP: Adding ${steps} intermediate steps..."
	    ref_step=$[${step} / ${steps}]
	    if [ ${ref_step} -gt 0 ]
	    then
#		step=${ref_step}
#		step_size="0"`echo "scale=4; ${step} / 10000" | bc`
		step_size="0"`echo "scale=4; ${ref_step} / 10000" | bc`
#		echo "SWEEP: New step size is 
		echo "SWEEP: Intermediate step size is ${step_size}."
#		ref_inj=$[${old_inj} + ${step}]
		ref_inj=$[${old_inj} + ${ref_step}]
		while [ ${ref_inj} -lt ${inj} ]
		do
		    inj_rate="0"`echo "scale=4; ${ref_inj} / 10000" | bc`
		    echo "SWEEP: Simulating for injection rate ${inj_rate}..."
		    ref_inj=$[${ref_inj} + ${ref_step}]
		    ${sim} $* print_csv_results=1 injection_rate=${inj_rate} | tee ${sim}.${$}.log
		    intm_lat=`grep "results:" ${sim}.${$}.log | cut -d , -f 8`
		    rm ${sim}.${$}.log
		    if [ ${?} -eq 0 ]
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
    
    old_inj=${inj}
    old_lat=${lat}
    inj=$[${inj}+${step}]

done

echo "SWEEP: Parameter sweep complete."
