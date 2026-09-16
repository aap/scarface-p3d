#!/usr/bin/env python3
"""vt.py <addr> [n]  -- dump n dwords at addr from scarface_unpacked.bin, resolving names"""
import sys, os, struct
D=os.path.dirname(os.path.abspath(__file__))
data=open(D+'/scarface_unpacked.bin','rb').read()
base=0x401000
names={}
for l in open(D+'/idb_names.txt'):
    p=l.split(None,1)
    if len(p)==2: names.setdefault(int(p[0],16), p[1].strip())
funcs=[]
for l in open(D+'/idb_funcs.txt'):
    s,e,n=l.split(None,2); funcs.append((int(s,16),int(e,16),n.strip()))
funcs.sort()
def nm(a):
    if a in names: return names[a]
    import bisect
    i=bisect.bisect_right(funcs,(a,0xffffffff,''))-1
    if i>=0 and funcs[i][0]<=a<funcs[i][1]:
        return '%s+0x%x'%(funcs[i][2],a-funcs[i][0])
    return ''
a=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 16
for i in range(n):
    off=a+i*4-base
    if off<0 or off+4>len(data): break
    v=struct.unpack('<I',data[off:off+4])[0]
    print('%08x [%2d] %08x  %s'%(a+i*4,i,v,nm(v)))
