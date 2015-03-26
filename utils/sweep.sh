#!/bin/sh

# $Id$

# Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
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
    initial_step=0.05
fi
if [ "${minimum_step}" = "" ]
then
    minimum_step=0.001
fi
if [ "${zero_load_inj}" = "" ]
then
    zero_load_inj=0.0025
fi
if [ "${no_backtrack}" = "" ]
then
    no_backtrack=0
fi
if [ "${no_addint}" = "" ]
then
    no_addint=0
fi

echo "SWEEP: Determining zero-load latency..."
${sim} $* print_csv_results=1 injection_rate=${zero_load_inj} | tee ${sim}.${HOSTNAME}.${$}.log
zero_load_lat=`grep "results:" ${sim}.${HOSTNAME}.${$}.log | cut -d , -f 6`
rm ${sim}.${HOSTNAME}.${$}.log
if [ "${zero_load_lat}" = "" ]
then
    echo "SWEEP: Simulation run failed."
    echo "SWEEP: Aborting."
    exit
fi
echo "SWEEP: Simulation run succeeded."
echo "SWEEP: Zero-load latency is ${zero_load_lat}."

step=${initial_step}
old_inj=0.0
inj=${step}
last_fail="`awk "BEGIN{ print 1.0 + ${minimum_step} }"`"

echo "SWEEP: Sweeping with initial step size ${step}"

while true
do
    if [ "`awk "BEGIN{ print ( ${inj} >= ${last_fail} ) }"`" = "1" ]
    then
	echo "SWEEP: Already failed for injection rate ${inj} before."
	echo "SWEEP: Simulation run skipped."
	lat=""
    else
	echo "SWEEP: Simulating for injection rate ${inj}..."
	${sim} $* print_csv_results=1 injection_rate=${inj} | tee ${sim}.${HOSTNAME}.${$}.log
	lat=`grep "results:" ${sim}.${HOSTNAME}.${$}.log | cut -d , -f 6`
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
	if [ "`awk "BEGIN{ print ( ${step} < ${minimum_step} ) }"`" = "1" ]
	then
	    echo "SWEEP: Step size too small."
	    break
	fi
	last_fail=${inj}
	echo "SWEEP: New step size is ${step}."
	inj="`awk "BEGIN{ print ${old_inj} + ${step} }"`"
	continue
    fi
    echo "SWEEP: Simulation run succeeded."
    echo "SWEEP: Latency is ${lat}."
    if [ ${no_addint} -eq 0 ]
    then
	ref_steps="`awk "BEGIN{ print int( ${lat} / ${zero_load_lat} ) }"`"
	if [ ${ref_steps} -gt 1 ]
	then
	    ref_step="`awk "BEGIN{ print ${step} / ${ref_steps} }"`"
	    if [ "`awk "BEGIN{ print ( ${ref_step} >= ${minimum_step} ) }"`" = "1" ]
	    then
		echo "SWEEP: Adding `awk "BEGIN{ print ${ref_steps} - 1 }"` intermediate step(s)..."
		echo "SWEEP: Intermediate step size is ${ref_step}."
		ref_inj="`awk "BEGIN{ print ${old_inj} + ${ref_step} }"`"
		while [ "`awk "BEGIN{ print ( ${ref_inj} < ${inj} ) }"`" = "1" ]
		do
		    echo "SWEEP: Simulating for injection rate ${ref_inj}..."
		    ${sim} $* print_csv_results=1 injection_rate=${ref_inj} | tee ${sim}.${HOSTNAME}.${$}.log
		    intm_lat=`grep "results:" ${sim}.${HOSTNAME}.${$}.log | cut -d , -f 6`
		    rm ${sim}.${HOSTNAME}.${$}.log
		    if [ "${intm_lat}" = "" ]
		    then
			echo "SWEEP: Simulation failed unexpectedly."
		    else
			echo "SWEEP: Simulation run succeeded."
			echo "SWEEP: Latency is ${intm_lat}."
		    fi
		    ref_inj="`awk "BEGIN{ print ${ref_inj} + ${ref_step} }"`"
		done
		echo "SWEEP: Done adding intermediate steps."
	    else
		echo "SWEEP: Interval too small to refine."
	    fi
	fi
    fi
    old_inj=${inj}
    inj="`awk "BEGIN{ print ${inj} + ${step} }"`"
done

echo "SWEEP: Parameter sweep complete."
echo "SWEEP: Zero-load latency: ${zero_load_lat}"
echo "SWEEP: Saturation throughput: ${old_inj}"
