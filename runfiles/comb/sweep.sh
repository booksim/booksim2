#!/bin/sh

cd ${rundir}

sed -i "s/\(injection_rate_uses_flits *=\) [^;]*/\1 1/" ${rundir}/${cfg}

echo "SWEEP: Starting parameter sweep for configuration ${cfg}..."
for use_read_write in 1 #0
do
  echo "SWEEP: Simulating for use_read_write=${use_read_write}..."
  sed -i "s/\(use_read_write *=\) [^;]*/\1 ${use_read_write}/" ${rundir}/${cfg}
  
  for traffic in uniform #bitcomp bitrev transpose shuffle tornado neighbor diagonal
    do
    sed -i "s/\(traffic *=\) [^;]*/\1 ${traffic}/" ${rundir}/${cfg}
    echo "SWEEP: Simulating for ${traffic} traffic..."
    for pkt_size in 5 #3 9 17 #2 4 8 16
      do
      sed -i "s/\(const_flits_per_packet *=\) [^;]*/\1 ${pkt_size}/" ${rundir}/${cfg}
      sed -i "s/\(read_request_size *=\) [^;]*/\1 1/" ${rundir}/${cfg}
      sed -i "s/\(read_reply_size *=\) [^;]*/\1 ${pkt_size}/" ${rundir}/${cfg}
      sed -i "s/\(write_request_size *=\) [^;]*/\1 ${pkt_size}/" ${rundir}/${cfg}
      sed -i "s/\(write_reply_size *=\) [^;]*/\1 1/" ${rundir}/${cfg}
#      old_inj=0
      echo "SWEEP: Simulating for packet size ${pkt_size}..."
      for inj in 1 `seq 25 25 950`
	do
	inj_rate="0"`echo "scale=4; ${inj} / 1000" | bc`
	echo "SWEEP: Simulating for injection rate ${inj_rate}..."
	sed -i "s/\(injection_rate *=\) [^;]*/\1 ${inj_rate}/" ${rundir}/${cfg}	    
	echo "SWEEP: Starting simulation run..."
	${sim} ${rundir}/${cfg}
	if [ ${?} -eq 0 ]
	    then
#	    while true
#	      do
	      echo "SWEEP: Simulation run failed."
#	      echo "SWEEP: Backtracking one half step."
#	      new_inj=$[(${inj} + ${old_inj})/2]
#	      if [ ${new_inj} -eq ${inj} ]
#		  then
#		  echo "SWEEP: Interval too small."
#		  break;
#	      fi
#	      inj_rate="0"`echo "scale=4; ${new_inj} / 1000" | bc`
#	      inj=${new_inj}
#	      echo "SWEEP: Simulating for injection rate ${inj_rate}..."
#	      sed -i "s/\(injection_rate *=\) [^;]*/\1 ${inj_rate}/" ${rundir}/${cfg}	    
#	      echo "SWEEP: Starting simulation run..."
#	      ${sim} ${rundir}/${cfg}
#	      if [ ${?} -ne 0 ]
#		  then
#		  echo "SWEEP: Simulation run succeeded."
#		  break
#	      fi
#	    done
	    break
	else
	    old_inj=${inj}
	    echo "SWEEP: Simulation run succeeded."
	fi
      done
    done
  done
done
echo "SWEEP: Parameter sweep complete."
