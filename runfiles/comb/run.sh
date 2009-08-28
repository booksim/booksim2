#!/bin/sh

sim=${1}
rundir=${2}
if [ "${3}" = "" ]
then
    queue="cva-batch.q"
else
    queue=${3}
fi
if [ "${4}" = "" ]
then
    outdir=${HOME}
else
    outdir=${4}
fi

for cfg in `ls ${rundir}`
do
    echo "`pwd`/sweep.sh" | qsub -N ${cfg} -v rundir=${rundir},cfg=${cfg},sim=${sim} -q ${queue} -o ${outdir} -e ${outdir}
done
