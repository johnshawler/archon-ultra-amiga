#!/bin/bash
# usage: build.sh <ARCHON.EXE> <outdir>  -> <outdir>/archon.adf  (+ data.hdf is built by mkhdf.py)
set -e
P=$(cd "$(dirname "$0")" && pwd); R=$P/..; EXE=$1; OUT=$2; mkdir -p $OUT
python3 -I $R/harness/export_objs.py $EXE $OUT
python3 -I $R/runtime/gen_stubs.py
python3 -I $R/translator/build.py $EXE $R/traces/cc4/executed_instruction_starts.txt.gz $OUT/obj1.s > $OUT/build.log
cat > $OUT/objdata.s <<EOT
	.section .data
	.balign 4
	.globl obj1_data, obj2_data, obj3_data, obj1_len, obj2_len, obj3_len
obj1_data: .incbin "$OUT/obj1.bin"
	.balign 4
obj2_data: .incbin "$OUT/obj2.bin"
	.balign 4
obj3_data: .incbin "$OUT/obj3.bin"
	.balign 4
obj1_len: .long $(stat -c %s $OUT/obj1.bin)
obj2_len: .long $(stat -c %s $OUT/obj2.bin)
obj3_len: .long $(stat -c %s $OUT/obj3.bin)
EOT
CC="m68k-linux-gnu-gcc -m68030 -msoft-float -O2 -fno-pic -fomit-frame-pointer -ffreestanding -nostdlib -fno-builtin -fno-tree-loop-distribute-patterns -I$OUT -Wall -Wno-misleading-indentation -Wno-unused"
DS="$(cat $OUT/defsyms.txt)"
rm -f $OUT/*.o
for f in obj1.s objdata.s; do $CC $DS -c -x assembler $OUT/$f -o $OUT/${f%.s}.o; done
$CC $DS -c -x assembler $R/runtime/rt_asm.s -o $OUT/rt_asm.o
for f in rt_core rt_vga rt_hw; do $CC -c $R/runtime/$f.c -o $OUT/$f.o; done
$CC ${DEBUGBAR:+-DDEBUGBAR} ${DBGSTOP:+-DDBG_STOP=$DBGSTOP} -c $P/platform.c -o $OUT/platform.o
$CC -c $P/libc.c -o $OUT/libc.o
for f in crt0 isr c2p; do $CC -c -x assembler $P/$f.s -o $OUT/$f.o; done
m68k-linux-gnu-ld -z noexecstack --no-warn-rwx-segments -q -T $P/link.ld -o $OUT/archon.elf $OUT/crt0.o $OUT/isr.o $OUT/c2p.o $OUT/platform.o $OUT/rt_core.o $OUT/rt_vga.o $OUT/rt_hw.o $OUT/rt_asm.o $OUT/obj1.o $OUT/objdata.o $OUT/libc.o $(m68k-linux-gnu-gcc -m68030 -msoft-float -print-libgcc-file-name)
m68k-linux-gnu-gcc -m68030 -c -x assembler $P/boot.s -o $OUT/boot.o; m68k-linux-gnu-objcopy -O binary -j .text $OUT/boot.o $OUT/boot.bin
m68k-linux-gnu-gcc -m68030 -c -x assembler $P/stage2.s -o $OUT/stage2.o; m68k-linux-gnu-objcopy -O binary -j .text $OUT/stage2.o $OUT/stage2.bin
python3 -I $P/mkimage.py $OUT/archon.elf $OUT $OUT/boot.bin $OUT/stage2.bin
m68k-linux-gnu-size $OUT/archon.elf
