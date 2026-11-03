# DOSBox reference tracing

The reference emulator is DOSBox 0.74-3 (Ubuntu source 0.74-3-5build2, `./configure --enable-debug=heavy`, core=normal, fixed 30000 cycles) with trace
hooks. `src/` holds the complete modified copies of the eight files that carry the hooks (overwrite the same paths in the source tree, then `make`);
`trace.patch` is the earlier diff of `hardware.cpp` only.

Environment variables: `TRACE_ITEIP=<start eip>:<count>` (per-instruction EIP stream, `iteip.bin`, plus `irq_sched.txt` with hardware/software interrupt
instruction indices), `TRACE_PORTREAD=1` (recorded port reads, `portreads.txt`), `TRACE_DUMPIDX=<n>` (dump data segment, VGA memory + palette, heap and low
memory at deduped instruction index n), `TRACE_SCRIPT` (scripted keyboard/mouse input per frame), `INT 33h` results go to `int33.log`.
