#!/bin/bash
# usage: run.sh <build dir> [frames] -- rebuild everything and run
H=$(cd "$(dirname "$0")" && pwd); B=$1
$H/build.sh /tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad/au/ARCHON.EXE $B 2>&1 | grep -E "error|undefined|built" 
python3 -I $H/../translator/build.py /tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad/au/ARCHON.EXE $H/../traces/cc4/executed_instruction_starts.txt.gz /dev/null 2>/dev/null | sed -n 2p
cd $B && timeout ${3:-300} qemu-m68k -cpu m68030 ./archon_h . ${2:-300} dump.bin audio.raw 2>&1 | head -${4:-30}
