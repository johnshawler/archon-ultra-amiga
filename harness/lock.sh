#!/bin/bash
# rebuild traced harness, run, compare with the DOSBox instruction trace
H=$(cd "$(dirname "$0")" && pwd); S=/tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad
B=$S/hbt; XL_TRACE=1 $H/build.sh $S/au/ARCHON.EXE $B 2>&1 | grep -E " error|undefined"
cd $B && rm -f itrace_ours.log && timeout 600 qemu-m68k -cpu m68030 ./archon_h . ${1:-100} d.bin 2>&1 | tail -${2:-3}
python3 -I $H/../tools/itcmp.py $S/work/it1/itrace.log itrace_ours.log
