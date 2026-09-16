#!/usr/bin/env python3
"""chunklen.py <chunkid> <files...>  -- histogram of payload lengths / child ids for a chunk id"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from p3dwalk import walk, load
want=int(sys.argv[1],16)
hist=collections.Counter(); kids=collections.Counter(); tot=0
for path in sys.argv[2:]:
    try: d=load(path)
    except Exception: continue
    stack=[]
    def cb(cid,data,off,dlen,clen,depth):
        global tot
        del stack[depth:]
        par=stack[-1] if stack else 0
        stack.append(cid)
        if cid==want:
            hist[dlen-12]+=1; tot+=1
        if par==want: kids[cid]+=1
    walk(d,12,len(d),0,cb)
print('id %08x total=%d'%(want,tot))
for k,v in sorted(hist.items()): print('  payload %4d bytes : %d'%(k,v))
for k,v in sorted(kids.items()): print('  child %08x : %d'%(k,v))
