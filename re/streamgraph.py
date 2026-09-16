#!/usr/bin/env python3
"""streamgraph.py -- decode art/levels/z04/streamgraph.p3d, the world streaming graph.

The retail StreamManager (engine/stream, not in the leak) loads the map through this
file, not through scripts: 297 polygon triggers (chunk 0x08800101, StreamTriggerLoader
0x4b9440) each name a subzone tag and the packages to have resident while the player is
inside the polygon, every package tagged with the stream *slot* it goes into:

    Global_S / Global_D    miami_lod / miami_lod_d, islands_lod / islands_lod_d
    Region_S / Region_D    <region>_region / <region>_region_d (shaders, textures, eco-prop models)
    Shell                  <subzone>_shell : the streets, sea, big buildings (+ collision)
    Detail                 <subzone>_detail[B|C] : the smaller stuff on top
    ZoneAnimation          packages/ZoneAnimations/<x>_anim
    Region_S also holds the odd extra library (luxuryhotel_region, indchopshop_region ...)

216 chunks 0x08800100 declare the package names first (name + a u32).

    python3 re/streamgraph.py [--file F] triggers      # every trigger: tag, bbox, packages
    python3 re/streamgraph.py [--file F] zones         # per subzone tag: region, shells, details
    python3 re/streamgraph.py [--file F] regions       # per region: subzone tags, packages
    python3 re/streamgraph.py [--file F] at X Z        # which triggers contain the point
    python3 re/streamgraph.py [--file F] json          # everything, for other tools

Coordinates are native Pure3D (left handed); p3dview shows -X.
"""
import sys, os, struct, json

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import p3dwalk as W

PACKAGE = 0x08800100
TRIGGER = 0x08800101
DEFAULT_PATHS = ['assets/art/levels/z04/streamgraph.p3d', 'assets/packages/z04/streamgraph.p3d']


def pstr(d, o):
    """padded pstring: the length byte counts the NUL padding up to a 4-byte multiple"""
    n = d[o]
    return d[o+1:o+1+n].split(b'\0', 1)[0].decode('latin1'), o + 1 + n


class Trigger(object):
    __slots__ = ('name', 'tag', 'height', 'points', 'loads')

    def __init__(self):
        self.loads = []          # [(package, slot)]

    def bbox(self):
        xs = [p[0] for p in self.points]; zs = [p[2] for p in self.points]
        return min(xs), max(xs), min(zs), max(zs)

    def contains(self, x, z):
        """2-D point in polygon (even-odd), the triggers are prisms of 'height'"""
        inside = False
        pts = self.points
        j = len(pts) - 1
        for i in range(len(pts)):
            xi, zi = pts[i][0], pts[i][2]
            xj, zj = pts[j][0], pts[j][2]
            if (zi > z) != (zj > z) and x < (xj - xi) * (z - zi) / (zj - zi) + xi:
                inside = not inside
            j = i
        return inside

    def slot(self, name):
        return [p for p, s in self.loads if s.lower() == name.lower()]

    def region(self):
        """the region library the subzone belongs to: first Region_S that is a *_region"""
        for p in self.slot('Region_S'):
            if p.lower().endswith('_region'):
                return p[:-len('_region')]
        return None


class StreamGraph(object):
    def __init__(self, path):
        self.path = path
        self.packages = []       # [(name, u32)]
        self.triggers = []
        d = W.load(path)
        off = 12
        while off + 12 <= len(d):
            cid, dl, cl = struct.unpack_from('<III', d, off)
            if cl < 12:
                break
            o = off + 12
            if cid == PACKAGE:
                name, o = pstr(d, o)
                (v,) = struct.unpack_from('<I', d, o)
                self.packages.append((name, v))
            elif cid == TRIGGER:
                t = Trigger()
                t.name, o = pstr(d, o)
                t.tag, o = pstr(d, o)
                t.height, n = struct.unpack_from('<fI', d, o); o += 8
                t.points = [struct.unpack_from('<3f', d, o + 12*i) for i in range(n)]; o += 12*n
                (np_,) = struct.unpack_from('<I', d, o); o += 4
                names = []
                for i in range(np_):
                    s, o = pstr(d, o); names.append(s)
                (ns,) = struct.unpack_from('<I', d, o); o += 4
                slots = []
                for i in range(ns):
                    s, o = pstr(d, o); slots.append(s)
                assert np_ == ns, (t.name, np_, ns)
                t.loads = list(zip(names, slots))
                self.triggers.append(t)
            off += cl

    # -- derived views --------------------------------------------------
    def zones(self):
        """tag -> {'region', 'shells', 'details', 'anims', 'support', 'triggers'}"""
        z = {}
        for t in self.triggers:
            e = z.setdefault(t.tag, {'region': None, 'shells': set(), 'details': set(),
                                     'anims': set(), 'support': set(), 'triggers': []})
            e['triggers'].append(t)
            if e['region'] is None:
                e['region'] = t.region()
            e['shells'].update(t.slot('Shell'))
            e['details'].update(t.slot('Detail'))
            e['anims'].update(t.slot('ZoneAnimation'))
            for p, s in t.loads:
                if s.lower() in ('global_s', 'global_d', 'region_s', 'region_d'):
                    e['support'].add(p)
        return z

    def regions(self):
        """region -> {'zones': [tag], 'packages': set}"""
        r = {}
        for tag, e in self.zones().items():
            reg = e['region'] or '?'
            x = r.setdefault(reg, {'zones': [], 'packages': set()})
            x['zones'].append(tag)
            x['packages'] |= e['shells'] | e['details']
        for x in r.values():
            x['zones'].sort()
        return r

    def at(self, x, z):
        return [t for t in self.triggers if t.contains(x, z)]


def find_default():
    for p in DEFAULT_PATHS:
        if os.path.exists(p):
            return p
    return None


def main():
    args = sys.argv[1:]
    path = None
    if args and args[0] == '--file':
        path = args[1]; args = args[2:]
    path = path or find_default()
    if not path:
        sys.exit('streamgraph.p3d not found; extract it with\n'
                 '  python3 re/rcf.py <cement.rcf> extract assets streamgraph')
    g = StreamGraph(path)
    mode = args[0] if args else 'zones'
    if mode == 'triggers':
        for t in g.triggers:
            x0, x1, z0, z1 = t.bbox()
            print('%-20s %-26s h=%5.1f %2d pts x[%6.0f %6.0f] z[%6.0f %6.0f]' %
                  (t.name, t.tag, t.height, len(t.points), x0, x1, z0, z1))
            for p, s in t.loads:
                print('      %-14s %s' % (s, p))
    elif mode == 'zones':
        z = g.zones()
        print('%-28s %-14s %-48s %s' % ('subzone', 'region', 'shells', 'details'))
        for tag in sorted(z, key=lambda k: (z[k]['region'] or '', k)):
            e = z[tag]
            print('%-28s %-14s %-48s %s' % (tag, e['region'], ' '.join(sorted(e['shells'])),
                                            ' '.join(sorted(e['details']))))
    elif mode == 'regions':
        for reg, x in sorted(g.regions().items()):
            print('%s: %d subzones, %d packages' % (reg, len(x['zones']), len(x['packages'])))
            print('    zones:    ' + ' '.join(x['zones']))
            print('    packages: ' + ' '.join(sorted(x['packages'])))
    elif mode == 'at':
        x, z = float(args[1]), float(args[2])
        for t in g.at(x, z):
            print('%-20s %-26s region=%s shells=%s details=%s' %
                  (t.name, t.tag, t.region(), ','.join(t.slot('Shell')), ','.join(t.slot('Detail'))))
    elif mode == 'json':
        out = {'packages': g.packages,
               'triggers': [{'name': t.name, 'tag': t.tag, 'height': t.height,
                             'points': t.points, 'loads': t.loads} for t in g.triggers]}
        json.dump(out, sys.stdout, indent=1)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
