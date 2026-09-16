#!/bin/sh
# usage: dis.sh <addr-or-flagname> [n-instructions]
#   no n: analyze function at addr and print it (pdf); with n: print n instructions linearly
# flag names: sym.<idb name, non-alnum -> _>; look them up in r2flags.r2 (grep -i)
D=$(dirname "$0")
if [ -n "$2" ]; then CMD="s $1; pd $2"; else CMD="s $1; af; pdf"; fi
r2 -q -m 0x401000 -b 32 -a x86 -i "$D/r2flags.r2" -c "e scr.color=0; e asm.comments=false; $CMD" "$D/scarface_unpacked.bin" 2>/dev/null
