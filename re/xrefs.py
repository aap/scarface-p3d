import capstone, struct, collections, re
lo=0x401000; img=open('scarface_unpacked.bin','rb').read(); hi=lo+len(img)
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); cs.detail=True
funcs=[(int(a,16),int(b,16),n) for a,b,n in (l.split(None,2) for l in open('idb_funcs.txt'))]
funcs.sort()
xr = collections.defaultdict(list)
starts = [f[0] for f in funcs]
import bisect
def fname(a):
    i = bisect.bisect_right(starts, a)-1
    if i>=0 and funcs[i][0] <= a < funcs[i][1]: return funcs[i][2].strip(), funcs[i][0]
    return None, None
def sweep(s,e):
    a=s
    while a<e:
        last=a
        for ins in cs.disasm(img[a-lo:e-lo], a):
            last = ins.address+ins.size
            yield ins
        a = last if last>a else a+1
        if a==last and last<e: a+=1
for s,e in [(0x401000,0x72f000),(0x860000,0x9ce000)]:
    for ins in sweep(s,e):
        for op in ins.operands:
            v=None
            if op.type == capstone.x86.X86_OP_IMM: v=op.imm
            elif op.type == capstone.x86.X86_OP_MEM and op.mem.base==0 and op.mem.index==0: v=op.mem.disp
            elif op.type == capstone.x86.X86_OP_MEM: v=op.mem.disp
            if v is not None and lo <= v < hi:
                kind = 'call' if ins.mnemonic=='call' else ('jmp' if ins.mnemonic.startswith('j') else 'ref')
                xr[v].append((ins.address, kind))
# data refs: scan dwords in data/rdata segments for pointers into image (vtables etc.)
for off in range(0, len(img)-4, 4):
    v = struct.unpack_from('<I', img, off)[0]
    if lo <= v < hi: xr[v].append((lo+off, 'data'))
names = {}
for l in open('idb_names.txt'):
    a,n = l.split(None,1); names[int(a,16)] = n.strip()
with open('xrefs_to.txt','w') as f:
    for t in sorted(xr):
        f.write('%08x %s <- %s\n' % (t, names.get(t,''), ' '.join('%08x:%s%s' % (a,k, ('('+fname(a)[0]+')') if k!='data' and fname(a)[0] and not fname(a)[0].startswith('sub_') else '') for a,k in xr[t])))
print(len(xr))
