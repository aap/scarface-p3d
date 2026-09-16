#!/bin/sh
# usage: xr.sh <hexaddr-without-0x-or-with>   -- who references this address (code+data)
D=$(dirname "$0"); a=$(printf '%08x' $((0x${1#0x})))
grep -E "^$a " "$D/xrefs_to.txt" | tr ' ' '\n'
