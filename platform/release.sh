#!/bin/bash
# usage: release.sh <game dir with ARCHON.EXE ARCHON.PKG TITLE.PKG> <outdir>
# Produces archon.hdf (ONE bootable hard-disk image: boot volume + data), plus the floppy alternative archon.adf + data.hdf, and SHA256SUMS.
# No debug overlay, no test input.
set -e
H=$(cd "$(dirname "$0")" && pwd); G=$1; O=$2; mkdir -p "$O"
$H/build.sh "$G/ARCHON.EXE" "$O/build" > "$O/build.log" 2>&1
cp "$O/build/archon.adf" "$O/archon.adf"
python3 $H/mkhdf.py "$O/data.hdf" archon.pkg="$G/ARCHON.PKG" title.pkg="$G/TITLE.PKG"
m68k-linux-gnu-gcc -m68030 -c -x assembler $H/launch.s -o "$O/build/launch.o"
m68k-linux-gnu-objcopy -O binary -j .text "$O/build/launch.o" "$O/build/launch.bin"
python3 $H/mkhdfboot.py "$O/archon.hdf" "$O/build/launch.bin" "$O/build/payload.bin" archon.pkg="$G/ARCHON.PKG" title.pkg="$G/TITLE.PKG"
(cd "$O" && sha256sum archon.hdf archon.adf data.hdf > SHA256SUMS && cat SHA256SUMS)
