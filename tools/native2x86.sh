#!/bin/bash
# usage: native2x86.sh <archon_h> <native addr hex>   -> nearest preceding translated label
m68k-linux-gnu-nm -n "$1" | awk -v a="$2" 'BEGIN{t=strtonum("0x" a)} $3 ~ /^L[0-9a-f]+$/ { v=strtonum("0x" $1); if (v<=t) {last=$3} else {print last; exit} }'
