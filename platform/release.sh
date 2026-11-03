#!/bin/bash
# usage: release.sh <game dir with ARCHON.EXE ARCHON.PKG TITLE.PKG> <outdir>
# Produces archon.adf (boot disk), data.hdf (raw data disk read through the Gayle IDE port) and SHA256SUMS. No debug overlay, no test input.
set -e
H=$(cd "$(dirname "$0")" && pwd); G=$1; O=$2; mkdir -p "$O"
$H/build.sh "$G/ARCHON.EXE" "$O/build" > "$O/build.log" 2>&1
cp "$O/build/archon.adf" "$O/archon.adf"
python3 $H/mkhdf.py "$O/data.hdf" archon.pkg="$G/ARCHON.PKG" title.pkg="$G/TITLE.PKG"
(cd "$O" && sha256sum archon.adf data.hdf > SHA256SUMS && cat SHA256SUMS)
