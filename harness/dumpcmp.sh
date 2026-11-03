#!/bin/bash
# usage: dumpcmp.sh <instruction index>  : dump obj3 at that deduped-instruction index in DOSBox and in the replayed translated build, compare.
H=$(cd "$(dirname "$0")" && pwd); S=/tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad; N=$1
W=$S/work/dd; mkdir -p $W; printf '900 quit\n' > $W/script.txt
cd $S/work && sed 's#TRACE_WATCH=watch.txt#TRACE_DUMPIDX='$N' TRACE_ITEIP=193d2e:'$((N+5))' TRACE_WATCH=w1/watch.txt#' runvar.sh > rundd.sh && chmod +x rundd.sh && ./rundd.sh dd 200 >/dev/null 2>&1
cp $S/work/it6/irq_sched.txt $S/work/it6/portreads.txt $S/hbt/; cd $S/hbt && DUMPIDX=$N timeout 600 qemu-m68k -cpu m68030 ./archon_h . 100 d.bin >/dev/null 2>&1
python3 -I $H/../tools/memcmp.py $W/obj3_idx.bin $S/hbt/obj3_idx.bin ${2:-20} 160000
echo == heap ==; python3 -I $H/../tools/memcmp.py $W/heap_idx.bin $S/hbt/heap_idx.bin ${2:-20} 1e0000
