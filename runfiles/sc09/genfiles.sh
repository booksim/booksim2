#!/bin/sh

basecfg=${1}
rundir=${2}

for vc_buf_size in 8 #4 16
  do
  for wait_for_tail_credit in 0 #1
    do
    for router in iq_combined #iq #iq_combined iq_split
      do
      
      if [ ${router} = iq ]
	  then
	  vcas="separable_input_first" #separable_output_first wavefront"
      else
	  vcas="ignored"
      fi
      
      for vca in ${vcas}
	do
	for swa in separable_input_first separable_output_first wavefront
	  do
	  
	  if [ ${vca} = separable_input_first -o ${vca} = separable_output_first -o ${swa} = separable_input_first -o ${swa} = separable_output_first ]
	      then
	      arbs="round_robin matrix"
	  else
	      arbs="ignored"
	  fi
	  
	  for arb in ${arbs}
	    do
	    
	    if [ ${router} = iq ]
		then
		if [ ${vca} = separable_input_first -a ${swa} = separable_input_first ]
		    then
		    speculations="nonspec spec_new" #spec"
		else
		    speculations="spec_new"
		fi		    
	    else
		speculations="ignored"
	    fi
	    
	    for speculation in ${speculations}
	      do
	      
	      if [ ${speculation} = spec ]
		  then
		  spec_masks="ignored"
		  speculative=1
	      elif [ ${speculation} = spec_new ]
		  then
		  spec_masks="confl_nonspec_reqs confl_nonspec_gnts" #any_nonspec_gnts"
		  speculative=2
	      elif [ ${speculation} = nonspec ]
		  then	
		  spec_masks="ignored"
		  speculative=0
	      else
		  spec_masks="ignored"
		  speculative=0
	      fi
	      
	      for spec_mask in ${spec_masks}
		do
		cat ${basecfg} \
		    | sed "s/\(vc_buf_size *=\) [^;]*/\1 ${vc_buf_size}/" \
		    | sed "s/\(wait_for_tail_credit *=\) [^;]*/\1 ${wait_for_tail_credit}/" \
		    | sed "s/\(router *=\) [^;]*/\1 ${router}/" \
		    | sed "s/\(vc_allocator *=\) [^;]*/\1 ${vca}/" \
		    | sed "s/\(vc_alloc_arb_type *=\) [^;]*/\1 ${arb}/" \
		    | sed "s/\(sw_allocator *=\) [^;]*/\1 ${swa}/" \
		    | sed "s/\(sw_alloc_arb_type *=\) [^;]*/\1 ${arb}/" \
		    | sed "s/\(speculative *=\) [^;]*/\1 ${speculative}/" \
		    | sed "s/\(filter_spec_grants *=\) [^;]*/\1 ${spec_mask}/" \
		    > ${rundir}/${basecfg}+${vc_buf_size}+${wait_for_tail_credit}+${router}+${vca}+${swa}+${arb}+${speculation}+${spec_mask}
		
	      done
	    done
	  done
	done
      done
    done
  done
done
