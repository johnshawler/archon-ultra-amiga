#!/bin/bash
# usage: build.sh <ARCHON.EXE> <out dir>
set -e
H=$(cd "$(dirname "$0")" && pwd); R=$H/..; EXE=$1; OUT=$2; mkdir -p $OUT
python3 -I $H/export_objs.py $EXE $OUT
python3 -I $R/runtime/gen_stubs.py
XL_TRACE=$XL_TRACE python3 -I $R/translator/build.py $EXE $R/traces/cc4/executed_instruction_starts.txt.gz $OUT/obj1.s > $OUT/build.log
CC="m68k-linux-gnu-gcc -m68030 -O2 -fno-pic -fomit-frame-pointer -I$OUT"
DS=$(cat $OUT/defsyms.txt)
$CC $DS -Wa,-L -c -x assembler $OUT/obj1.s -o $OUT/obj1.o
$CC $DS -c -x assembler $R/runtime/rt_asm.s -o $OUT/rt_asm.o
for f in rt_core rt_vga rt_hw; do $CC -c $R/runtime/$f.c -o $OUT/$f.o; done
$CC -c $H/main.c -o $OUT/main.o
m68k-linux-gnu-gcc -m68030 -static $OUT/*.o -o $OUT/archon_h
echo built $OUT/archon_h
