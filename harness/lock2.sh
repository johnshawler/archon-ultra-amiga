#!/bin/bash
H=$(cd "$(dirname "$0")" && pwd); S=/tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad
B=$S/hbt; XL_TRACE=1 $H/build.sh $S/au/ARCHON.EXE $B 2>&1 | grep -E " error|undefined"
cp $S/work/${TRACE:-it6}/irq_sched.txt $S/work/${TRACE:-it6}/portreads.txt $B/; cp $S/work/${TRACE:-it6}/int33.log $B/ 2>/dev/null; rm -f $B/int33.log.bak; cd $B && rm -f iteip_ours.bin && export LOCKN=${LOCKN:-30000000} && timeout ${2:-900} qemu-m68k -cpu m68030 ./archon_h . ${1:-1000} d.bin 2>&1 | grep -v "^rt: in\|^rt: out\|hw:\|poll@" | tail -3
gcc -O2 -o /tmp/eipcmp $H/../tools/eipcmp.c && /tmp/eipcmp $S/work/${TRACE:-it6}/iteip.bin iteip_ours.bin $3
