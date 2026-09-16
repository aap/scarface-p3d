import sys; sys.path.insert(0,'.')
from p3dwalk import walk, load
d = load(sys.argv[1]); want = int(sys.argv[2],16); n = int(sys.argv[3]); cnt=[0]
def cb(cid, data, off, dlen, clen, depth):
    if cid == want and cnt[0] < n:
        cnt[0]+=1
        b = data[off+12:off+dlen]
        print('--- %08x @%x dlen=%d clen=%d' % (cid, off, dlen-12, clen))
        for i in range(0, len(b), 16):
            ch = b[i:i+16]
            print('%04x  %-48s %s' % (i, ' '.join('%02x'%c for c in ch), ''.join(chr(c) if 32<=c<127 else '.' for c in ch)))
walk(d, 12, len(d), 0, cb)
