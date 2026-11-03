#!/bin/bash
# usage: run.sh <outdir> <seconds> [shot names at seconds...] : boot the ADF+HDF in FS-UAE headless, take screenshots of the X display
# env: KICK=<kickstart.rom> (test only).  Screenshots: <outdir>/shot_<secs>.png
here=$(cd "$(dirname "$0")" && pwd); out=$1; shift
: "${KICK:?set KICK}"
cfg=$out/run.fs-uae
sed -e "s|@KICK@|$KICK|" -e "s|@ADF@|$out/archon.adf|" -e "s|@HDF@|$out/data.hdf|" $here/amiga.fs-uae > $cfg; [ -n "$NOHDF" ] && sed -i "/^hard_drive_0/d" $cfg
export OUT=$out
xvfb-run -a -s "-screen 0 1024x768x24" bash -c '
  fs-uae "'"$cfg"'" >"'"$out"'/fsuae.log" 2>&1 & P=$!
  t=0
  for s in '"$*"'; do sleep $((s-t)); t=$s; import -window root "'"$out"'/shot_$s.png"; done
  kill $P 2>/dev/null; sleep 1
'
