#!/bin/sh
# usage: func.sh <hexaddr> -- which IDB function contains this address
D=$(dirname "$0")
python3 -c "
import sys; a=int('$1'.replace('0x',''),16)
for l in open('$D/idb_funcs.txt'):
    s,e,n=l.split(None,2)
    if int(s,16)<=a<int(e,16): print(l.strip())
"
