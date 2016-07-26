#!/bin/bash

################################################################################
# This Bash script removes all the CPUs except the CPU 0 from the default CPU
# pool (Pool-0) and gives the CPUs to a newly created CPU pool.
#
# Author: Juan A. Colmenares <juancol@eecs.berkeley.edu>
################################################################################

DEFAULT_POOL="Pool-0"
NEW_CPUPOOL="GangSched-Pool"
MY_SCHED="gang"
#MY_SCHED="credit"

# VCPU 0 is the only allowed to Domain 0.
#sudo xl vcpu-set 0 0
# Domain 0's VCPU 0 can only run on CPU 0.
#sudo xl vcpu-pin 0 0 0

CPUS_IN_DEFAULT_POOL=`sudo xl cpupool-list -c | grep $DEFAULT_POOL | awk '{print $2}'`
echo "CPUs in default pool $DEFAULT_POOL: $CPUS_IN_DEFAULT_POOL"

CPUS=`echo $CPUS_IN_DEFAULT_POOL | sed -r 's/,/ /g'`
#echo "Default Pool: " $CPUS

CPUS_FOR_NEW_POOL=`echo $CPUS_IN_DEFAULT_POOL | sed -r 's/^0,//g'`
echo "Gang-Pool CPUs: " $CPUS_FOR_NEW_POOL

NUM_OF_CPUS_FOR_NEW_POOL=`echo $CPUS_FOR_NEW_POOL | wc -w`
#echo $NUM_OF_CPUS_FOR_NEW_POOL

cpu_count=0

for c in $CPUS; do
    if [ $c -gt 0 ]; then
        cpu_count=$((cpu_count+1))
        cmd="sudo xl cpupool-cpu-remove $DEFAULT_POOL $c"
        echo $cmd
        $cmd
    fi
done

#echo $cpu_count

if [ $cpu_count -gt 0 ]; then
  sudo xl cpupool-create name=\'$NEW_CPUPOOL\' sched=\'$MY_SCHED\' cpus=[$CPUS_FOR_NEW_POOL]
else
  echo "No CPUs in default pool $DEFAULT_POOL to move to the new pool $NEW_CPUPOOL."
fi

