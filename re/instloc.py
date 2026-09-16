# Decode the instanceobject / eco-prop placements of a p3d package.
#
#   python3 instloc.py <file.p3d> [modelname-substring]
#
# Dumps one line per 0x09900194 location:
#   <modelname> pos(x,y,z) rot(x,y,z in degrees) scale(x,y,z) tint alpha state
#
# See notes/instances.md for the format.  The encoding is:
#   chunk 0x09900194 = 3 floats (x, yInCm, z) + 9 u32
#     w0,w1,w2  rotation X,Y,Z in whole degrees, 0..359
#     w3,w4,w5  scale, 6 bits each
#     w6        uniform-scale flag
#     w7        tint 0..15
#     w8        initial state, 1-based index into STATE_NAMES (0 = none)
#   if w6: uniform scale = (w3 | w4<<6 | w5<<12) / 1000.0
#   else:  scale = (w3/20.0, w4/20.0, w5/20.0)
#   y = yInCm / 100.0     (the game stores it as a short in 1/100 units)
#   alpha = 1.0 if w7 == 0 else w7/15.0
# Note the game quantises rotation X and Z to 2 degrees when it packs them.

import sys, struct, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from p3dwalk import walk, load

# table at 0x0081d238 in the exe, index is 1-based
STATE_NAMES = ['', 'idle', 'final', 'damage_1', 'damage_2', 'damage_3',
               'damage_4', 'explosion', 'open', 'opening', 'closed', 'closing']


def pstr(b, o):
    l = b[o]
    return b[o + 1:o + 1 + l].rstrip(b'\0').decode('latin1'), o + 1 + l


def decode_loc(b):
    x, ycm, z = struct.unpack_from('<3f', b, 0)
    w = struct.unpack_from('<9I', b, 12)
    if w[6]:
        s = (w[3] | (w[4] << 6) | (w[5] << 12)) / 1000.0
        scale = (s, s, s)
    else:
        scale = (w[3] / 20.0, w[4] / 20.0, w[5] / 20.0)
    return dict(pos=(x, ycm / 100.0, z), rot=(w[0], w[1], w[2]), scale=scale,
                uniform=w[6], tint=w[7], state=w[8],
                alpha=1.0 if w[7] == 0 else w[7] / 15.0)


def main():
    if len(sys.argv) < 2:
        print(__doc__ or 'usage: instloc.py <file.p3d> [substr]', file=sys.stderr)
        return 1
    data = load(sys.argv[1])
    want = sys.argv[2].lower() if len(sys.argv) > 2 else None

    cur = {'cls': None, 'name': None, 'model': None}
    n = [0]

    def cb(cid, d, off, dlen, clen, depth):
        b = d[off + 12:off + dlen]
        if cid in (0x09900190, 0x09900191):
            _, o = pstr(b, 0)
            nm, o = pstr(b, o)
            cls, o = pstr(b, o)
            cur.update(cls=cls, name=nm, model=None)
        elif cid == 0x09900192 and cur['cls'] == 'instanceobject':
            k, o = pstr(b, 0)
            t = struct.unpack_from('<I', b, o)[0]
            if k == 'modelname' and t == 0:
                cur['model'] = pstr(b, o + 4)[0]
        elif cid == 0x09900194 and cur['cls'] == 'instanceobject':
            m = cur['model'] or '?'
            if want and want not in m.lower():
                return
            L = decode_loc(b)
            n[0] += 1
            print('%-20s %-24s pos %9.3f %8.3f %9.3f  rot %3d %3d %3d  '
                  'scale %5.3f %5.3f %5.3f %s tint %2d alpha %.3f state %s'
                  % (m, cur['name'], L['pos'][0], L['pos'][1], L['pos'][2],
                     L['rot'][0], L['rot'][1], L['rot'][2],
                     L['scale'][0], L['scale'][1], L['scale'][2],
                     'U' if L['uniform'] else '-', L['tint'], L['alpha'],
                     STATE_NAMES[L['state']] if L['state'] < len(STATE_NAMES) else L['state']))

    walk(data, 12, len(data), 0, cb)
    print('# %d locations' % n[0], file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
