#!/usr/bin/env python3
"""strrefs.py <lo> <hi> -- strings referenced by functions in an address range"""
import os, sys, struct, bisect, collections, re
D = os.path.dirname(os.path.abspath(__file__))
BASE=0x401000
IMG=open(D+'/scarface_unpacked.bin','rb').read()
funcs=[]
for l in open(D+'/idb_funcs.txt'):
    s,e,n=l.split(None,2); funcs.append((int(s,16),int(e,16),n.strip()))
funcs.sort(); fstart=[f[0] for f in funcs]
def func_of(a):
    i=bisect.bisect_right(fstart,a)-1
    if i>=0 and funcs[i][0]<=a<funcs[i][1]: return funcs[i][0]
    return None
def cstr(a,maxlen=160):
    o=a-BASE
    if o<0 or o>=len(IMG): return None
    e=IMG.find(b'\0',o,o+maxlen)
    if e<0: return None
    b=IMG[o:e]
    if len(b)<4: return None
    try: s=b.decode('ascii')
    except: return None
    if sum(1 for c in s if 32<=ord(c)<127)<len(s): return None
    return s
# src -> targets
src2tgt=collections.defaultdict(list)
for l in open(D+'/xrefs_to.txt'):
    if '<-' not in l: continue
    lhs,rhs=l.split('<-',1)
    tgt=int(lhs.split()[0],16)
    for tok in rhs.split():
        if ':' not in tok: continue
        sa,kind=tok.rsplit(':',1)
        try: sa=int(sa,16)
        except: continue
        f=func_of(sa)
        if f is not None: src2tgt[f].append((tgt,kind))

lo=int(sys.argv[1],16); hi=int(sys.argv[2],16) if len(sys.argv)>2 else lo+1
seen=set()
for f in fstart:
    if not (lo<=f<hi): continue
    for tgt,kind in src2tgt.get(f,()):
        if 0x72f000<=tgt<0x860000 or 0x9d5000<=tgt<0x9f0000:
            s=cstr(tgt)
            if s and s not in seen:
                seen.add(s); print('%08x %08x %s'%(f,tgt,s))
