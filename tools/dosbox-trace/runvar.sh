#!/bin/bash
# usage: runvar.sh name   (script in name/script.txt)
cd /tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad/work
r=$1; mkdir -p $r; sed "s#CAPDIR#$PWD/$r#; s#../au#$PWD/../au#" db.tpl > $r/db.conf
TRACE_CODELO=0x16d000 TRACE_CODEHI=0x1a9353 TRACE_IDLE=0x18d8a6,0x18d8b0 TRACE_DIR=$r TRACE_SCRIPT=$r/script.txt SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${2:-900} dosbox-0.74-3/src/dosbox -conf $r/db.conf -noprimaryconf >$r/out.txt 2>&1
