import sys, struct; sys.path.insert(0,'.')
from p3dwalk import walk, load
d = load(sys.argv[1]); want = sys.argv[2]; n=int(sys.argv[3]) if len(sys.argv)>3 else 1
def s(b, o):
    l = b[o]; return b[o+1:o+1+l].rstrip(b'\0').decode('latin1'), o+1+l
hit=[0]; cur=[False]
def cb(cid, data, off, dlen, clen, depth):
    b = data[off+12:off+dlen]
    if cid in (0x09900190,0x09900191):
        a,o = s(b,0); nm,o = s(b,o); o=(o+3)&~3; cls,o = s(b,o)
        cur[0] = (cls == want or nm == want) and hit[0] < n
        if cur[0]:
            hit[0]+=1; print('%s%08x %s name=%s class=%s rest=%s' % ('  '*depth, cid, a, nm, cls, b[o:].hex()))
    elif cur[0] and cid == 0x09900192:
        k,o = s(b,0); o=(o+3)&~3; v,o = s(b,o)
        print('%s  prop %s = %r rest=%s' % ('  '*depth, k, v, b[o:].hex()))
    elif cur[0] and cid == 0x09900194:
        f = struct.unpack_from('<3f', b, 0); rest = b[12:]
        print('%s  loc pos=%s  %s' % ('  '*depth, f, ' '.join('%08x'%x for x in struct.unpack_from('<%dI'%(len(rest)//4), rest))))
walk(d, 12, len(d), 0, cb)
