#!/bin/bash
# usage: vgacmp.sh <idx>  compare VGA memory + DAC at that instruction index (DOSBox vs replayed translated build)
H=$(cd "$(dirname "$0")" && pwd); S=/tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad; N=$1
XL_TRACE=1 $H/build.sh $S/au/ARCHON.EXE $S/hbt 2>&1 | grep " error"
W=$S/work/dd; mkdir -p $W; printf '900 quit\n' > $W/script.txt
cd $S/work && sed 's#TRACE_WATCH=watch.txt#TRACE_DUMPIDX='$N' TRACE_ITEIP=193d2e:'$((N+5))' TRACE_WATCH=w1/watch.txt#' runvar.sh > rundd.sh && chmod +x rundd.sh && ./rundd.sh dd 600 >/dev/null 2>&1
cp $S/work/it6/irq_sched.txt $S/work/it6/portreads.txt $S/hbt/; cd $S/hbt && DUMPIDX=$N timeout 900 qemu-m68k -cpu m68030 ./archon_h . 100 d.bin >/dev/null 2>&1
python3 -I - <<PY
d=open('$W/vga_idx.bin','rb').read(); o=open('$S/hbt/vga_idx.bin','rb').read()
vd,vo=d[:262144],o[:262144]; dd,od=d[262144:],o[262144:]
nd=sum(1 for i in range(262144) if vd[i]!=vo[i]); print("VGA mem differing bytes:",nd,"of 262144; DAC identical:",dd==od)
if nd:
    runs=[];
    for i in range(262144):
        if vd[i]!=vo[i]:
            if runs and i-runs[-1][1]<=64: runs[-1][1]=i
            else: runs.append([i,i])
    print(len(runs),"runs; first:",[(hex(a),hex(b)) for a,b in runs[:8]])
PY
