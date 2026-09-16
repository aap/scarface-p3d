#!/usr/bin/env python3
"""Export the Scarface: The World Is Yours (PC) map from Pure3D packages to glTF 2.0.

    python3 re/p3d2gltf.py --out world.glb [--region sbeachn | --files a.p3d b.p3d | --all]
                           [--no-instances] [--lod] [--flip-x]

Coordinates are native Pure3D by default, which is a LEFT handed space: the world comes
out mirrored (text reads backwards) though winding and normals stay consistent.  Pass
--flip-x for a right handed, correctly readable world at the price of a negated X.

What comes out
    * one glTF node per package, holding one node per renderer::WorldGeoRenderable
      (0x08800003); the world geo's CompositeDrawable (0x00123000) becomes one child
      node per element, each referencing a shared glTF mesh (= one pure3d::Geometry,
      0x00010000, one primitive per 0x00010020 prim group).
    * one node per instanceobject model (trees, street lights, benches, ...) holding one
      node per 0x09900194 placement; each placement node has the <model>InstanceShape and
      <model>LODShape meshes as children -> Blender gets linked duplicates.
    * one material per pure3d::Shader (0x00011000) with the base texture's PNG embedded.

See re/notes/gltf_export.md for the conventions (UV flip, colour byte order, handedness).
"""

import argparse, collections, json, mmap, os, struct, sys, time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from p3dwalk import load as p3d_load                     # noqa: E402  (P3DZ fallback)

# ---------------------------------------------------------------------------
# chunk ids (pure3d, see geometry.h / shader.h / texture.h and
# /u/aap/fun/ps2engines/extracted/shr/libs/pure3d/constants/chunkids.hpp)
# ---------------------------------------------------------------------------
MESH            = 0x00010000
BOX             = 0x00010003
SPHERE          = 0x00010004
POSITIONLIST    = 0x00010005
NORMALLIST      = 0x00010006
UVLIST          = 0x00010007
COLOURLIST      = 0x00010008
INDEXLIST       = 0x0001000A
MULTICOLOURLIST = 0x0001001C
PRIMGROUP       = 0x00010020

SHADER          = 0x00011000
SHADER_TEXTURE  = 0x00011002
SHADER_INT      = 0x00011003
SHADER_FLOAT    = 0x00011004
SHADER_COLOUR   = 0x00011005
SHADER_VECTOR   = 0x00011006
SHADER_MATRIX   = 0x00011007

TEXTURE         = 0x00019000
IMAGE           = 0x00019001
IMAGE_DATA      = 0x00019002

SKELETON        = 0x00023000
SKELETON_JOINT  = 0x00023001

COMPOSITE       = 0x00123000
COMPOSITE_PRIM  = 0x00123001

WORLDGEO        = 0x08800003          # renderer::WorldGeoLoader
ZONEPKG         = 0x08800004          # renderer::ZonePkgLoader
ZONEENTRY       = 0x08800009

SCRIPTOBJECT    = 0x09900190
SCRIPTGROUP     = 0x09900191
SCRIPTPROP      = 0x09900192
SCRIPTLOC       = 0x09900194

PDDI_PRIM_TRIANGLES = 0
PDDI_PRIM_TRISTRIP  = 1

PDDI_V_NORMAL  = 1 << 4
PDDI_V_COLOUR  = 1 << 5
PDDI_V_COLOUR2 = 1 << 14

# table at 0x0081d238 in the retail exe, 1 based
STATE_NAMES = ['', 'idle', 'final', 'damage_1', 'damage_2', 'damage_3',
               'damage_4', 'explosion', 'open', 'opening', 'closed', 'closing']

# renderer::WorldGeoLoader type field (re/notes/renderables.md section 4)
WORLDGEO_TYPES = {0: 'normal', 1: 'normal1', 3: 'low_LOD', 5: 'underwater',
                  6: 'cbvlitdecals', 7: 'interiorfloors', 8: 'cardsnight'}

DEG2RAD = 0.017453292


# ---------------------------------------------------------------------------
# names
# ---------------------------------------------------------------------------
def gethash(s, seed=0):
    """core::GetHash, retail 0x006dc190 -- the inventory key.

    Blind "tolower": every char below 'a' gets +0x20, so digits and punctuation
    move too.  Rolling: GetHash("LODShape", GetHash("treeA")) == GetHash("treeALODShape").
    """
    if not s:
        return seed
    h = seed & 0x7fffffff
    for ch in s.encode('latin1'):
        c = ch - 256 if ch >= 128 else ch
        if c < 0x61:
            c += 0x20
        h = (h * 65599) & 0x7fffffff
        h ^= c & 0xffffffff
        h &= 0x7fffffff
    return h | 0x80000000


# ---------------------------------------------------------------------------
# chunk reading
# ---------------------------------------------------------------------------
def chunks(data, start, end):
    """Iterate sibling chunks in [start,end) -> (id, dataStart, dataEnd, childStart, childEnd)."""
    off = start
    while off + 12 <= end:
        cid, dlen, clen = struct.unpack_from('<III', data, off)
        if clen < 12 or off + clen > end:
            break
        yield cid, off + 12, off + dlen, off + dlen, off + clen
        off += clen


def rstr(data, off):
    """Pure3D pstring: u8 len; char[len] with the NUL and the 4-alignment padding inside."""
    n = data[off]
    raw = bytes(data[off + 1:off + 1 + n])
    z = raw.find(b'\0')
    if z >= 0:
        raw = raw[:z]
    return raw.decode('latin1'), off + 1 + n


def ru32(data, off):
    return struct.unpack_from('<I', data, off)[0], off + 4


def rfcc(data, off):
    """a pddi shader parameter key, stored as four chars ('TEX\\0', 'BLMD', ...)"""
    raw = bytes(data[off:off + 4]).rstrip(b'\0')
    return raw.decode('latin1'), off + 4


# ---------------------------------------------------------------------------
# database records (all of them keep offsets into the mmap, not copies)
# ---------------------------------------------------------------------------
class PrimGroup(object):
    __slots__ = ('data', 'shader', 'primType', 'fmt', 'nverts', 'nindices',
                 'pos', 'normal', 'uv', 'colour', 'indices')

    def __init__(self, data):
        self.data = data
        self.shader = None
        self.primType = PDDI_PRIM_TRIANGLES
        self.fmt = 0
        self.nverts = 0
        self.nindices = 0
        self.pos = None          # (offset, count)
        self.normal = None
        self.uv = {}             # channel -> (offset, count)
        self.colour = None
        self.indices = None


class Mesh(object):
    __slots__ = ('name', 'pkg', 'groups')

    def __init__(self, name, pkg):
        self.name = name
        self.pkg = pkg
        self.groups = []


class Shader(object):
    __slots__ = ('name', 'pddiShader', 'translucent', 'params')

    def __init__(self, name, pddiShader, translucent):
        self.name = name
        self.pddiShader = pddiShader
        self.translucent = translucent
        self.params = {}


class Texture(object):
    __slots__ = ('name', 'width', 'height', 'data', 'png', 'fmt')

    def __init__(self, name, w, h):
        self.name = name
        self.width = w
        self.height = h
        self.data = None
        self.png = None          # (offset, length)
        self.fmt = 0


class Composite(object):
    __slots__ = ('name', 'skeleton', 'elements')

    def __init__(self, name, skeleton):
        self.name = name
        self.skeleton = skeleton
        self.elements = []       # (childName, type, jointId)


class Skeleton(object):
    __slots__ = ('name', 'parents', 'local', 'world')

    def __init__(self, name):
        self.name = name
        self.parents = []
        self.local = []
        self.world = None

    def build(self):
        """world[i] = local[i] * world[parent[i]]  (row vector convention, as
        Skeleton::Rebuild in skeleton.cpp)."""
        self.world = []
        for i, m in enumerate(self.local):
            p = self.parents[i]
            self.world.append(m if i == 0 or p >= i else np.dot(m, self.world[p]))


class WorldGeo(object):
    __slots__ = ('name', 'composite', 'type', 'pkg', 'zone', 'support')

    def __init__(self, name, composite, type_, pkg, support=False):
        self.name = name
        self.composite = composite
        self.type = type_
        self.pkg = pkg
        self.zone = None
        self.support = support


class Placement(object):
    __slots__ = ('model', 'obj', 'pkg', 'pos', 'rot', 'scale', 'tint', 'state', 'support')


# ---------------------------------------------------------------------------
# package loading
# ---------------------------------------------------------------------------
class Database(object):
    def __init__(self):
        self.meshes = {}         # hash -> Mesh
        self.shaders = {}        # hash -> Shader
        self.textures = {}       # hash -> Texture
        self.composites = {}     # hash -> Composite
        self.skeletons = {}      # hash -> Skeleton
        self.worldgeos = []      # [WorldGeo] in load order
        self.zone = {}           # hash(worldgeo name) -> dict
        self.placements = []     # [Placement]
        self.maps = []           # keep the mmaps alive
        self.packages = []
        self.support = False     # is the package being parsed a shared library?

    # -- lookups (the engine keys everything by GetHash, so this is case folding)
    def mesh(self, name):
        return self.meshes.get(gethash(name))

    def shader(self, name):
        return self.shaders.get(gethash(name))

    def texture(self, name):
        return self.textures.get(gethash(name))

    def composite(self, name):
        return self.composites.get(gethash(name))

    def skeleton(self, name):
        return self.skeletons.get(gethash(name))

    # ------------------------------------------------------------------
    def load(self, path, support=False, verbose=False):
        f = open(path, 'rb')
        head = f.read(4)
        if head == b'P3D\xff':
            f.seek(0)
            data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
            self.maps.append((f, data))
        else:                                       # P3DZ or junk: decompress in RAM
            f.close()
            data = p3d_load(path)
            if data[:4] != b'P3D\xff':
                print('skip %s: not a pure3d file' % path, file=sys.stderr)
                return
        pkg = os.path.basename(path)
        self.packages.append(pkg)
        self.support = support
        n0 = len(self.worldgeos), len(self.placements)
        for cid, ds, de, cs, ce in chunks(data, 12, len(data)):
            self._top(data, pkg, cid, ds, de, cs, ce)
        if verbose:
            print('  %-32s %d world geos, %d placements' %
                  (pkg, len(self.worldgeos) - n0[0], len(self.placements) - n0[1]))

    # ------------------------------------------------------------------
    def _top(self, d, pkg, cid, ds, de, cs, ce):
        if cid == MESH:
            self._mesh(d, pkg, ds, de, cs, ce)
        elif cid == SHADER:
            self._shader(d, ds, de, cs, ce)
        elif cid == TEXTURE:
            self._texture(d, ds, de, cs, ce)
        elif cid == COMPOSITE:
            self._composite(d, ds, de, cs, ce)
        elif cid == SKELETON:
            self._skeleton(d, ds, de, cs, ce)
        elif cid == WORLDGEO:
            name, o = rstr(d, ds)
            comp, o = rstr(d, o)
            type_, o = ru32(d, o)
            self.worldgeos.append(WorldGeo(name, comp, type_, pkg, self.support))
        elif cid == ZONEPKG:
            self._zonepkg(d, ds, de, cs, ce)
        elif cid in (SCRIPTOBJECT, SCRIPTGROUP):
            self._scriptobject(d, pkg, cid, ds, de, cs, ce)

    # -- 0x00010000 Mesh ------------------------------------------------
    def _mesh(self, d, pkg, ds, de, cs, ce):
        name, o = rstr(d, ds)
        key = gethash(name)
        if key in self.meshes:
            return                                   # first package wins, like the inventory
        mesh = Mesh(name, pkg)
        for cid, gds, gde, gcs, gce in chunks(d, cs, ce):
            if cid != PRIMGROUP:
                continue
            g = PrimGroup(d)
            o = gds + 4                              # version
            g.shader, o = rstr(d, o)
            (g.primType, g.fmt, g.nverts, g.nindices, _nm,
             _a, _b, _c, _e) = struct.unpack_from('<9I', d, o)
            for ccid, cds, cde, _ccs, _cce in chunks(d, gcs, gce):
                if ccid == POSITIONLIST:
                    g.pos = (cds + 4, struct.unpack_from('<I', d, cds)[0])
                elif ccid == NORMALLIST:
                    g.normal = (cds + 4, struct.unpack_from('<I', d, cds)[0])
                elif ccid == UVLIST:
                    n, ch = struct.unpack_from('<2I', d, cds)
                    g.uv[ch] = (cds + 8, n)
                elif ccid == COLOURLIST:
                    g.colour = (cds + 4, struct.unpack_from('<I', d, cds)[0])
                elif ccid == MULTICOLOURLIST and g.colour is None:
                    n, ch = struct.unpack_from('<2I', d, cds)
                    if ch == 0:
                        g.colour = (cds + 8, n)
                elif ccid == INDEXLIST:
                    g.indices = (cds + 4, struct.unpack_from('<I', d, cds)[0])
            mesh.groups.append(g)
        self.meshes[key] = mesh

    # -- 0x00011000 Shader ----------------------------------------------
    def _shader(self, d, ds, de, cs, ce):
        name, o = rstr(d, ds)
        key = gethash(name)
        if key in self.shaders:
            return
        o += 4                                       # version
        pddiName, o = rstr(d, o)
        translucent, o = ru32(d, o)
        sh = Shader(name, pddiName, translucent)
        for cid, pds, pde, _pcs, _pce in chunks(d, cs, ce):
            k, o = rfcc(d, pds)
            if cid == SHADER_TEXTURE:
                sh.params[k], _ = rstr(d, o)
            elif cid == SHADER_INT:
                sh.params[k] = struct.unpack_from('<i', d, o)[0]
            elif cid == SHADER_FLOAT:
                sh.params[k] = round(struct.unpack_from('<f', d, o)[0], 6)
            elif cid == SHADER_COLOUR:
                c = struct.unpack_from('<I', d, o)[0]   # pddiColour == 0xAARRGGBB
                sh.params[k] = [(c >> 16) & 255, (c >> 8) & 255, c & 255, (c >> 24) & 255]
            elif cid == SHADER_VECTOR:
                sh.params[k] = list(struct.unpack_from('<3f', d, o))
        self.shaders[key] = sh

    # -- 0x00019000 Texture ---------------------------------------------
    def _texture(self, d, ds, de, cs, ce):
        name, o = rstr(d, ds)
        key = gethash(name)
        if key in self.textures:
            return
        _ver, w, h = struct.unpack_from('<3I', d, o)
        tex = Texture(name, w, h)
        for cid, ids, ide, ics, ice in chunks(d, cs, ce):
            if cid != IMAGE:
                continue
            _n, io = rstr(d, ids)
            _ver, iw, ih, _bpp, _pal, _alpha, fmt = struct.unpack_from('<7I', d, io)
            tex.fmt = fmt
            for dcid, dds, dde, _a, _b in chunks(d, ics, ice):
                if dcid == IMAGE_DATA:
                    sz = struct.unpack_from('<I', d, dds)[0]
                    tex.data = d
                    tex.png = (dds + 4, sz)
                    tex.width, tex.height = iw, ih
                    break
            break                                    # mip 0 only
        self.textures[key] = tex

    # -- 0x00123000 CompositeDrawable -----------------------------------
    def _composite(self, d, ds, de, cs, ce):
        o = ds + 4                                   # version
        name, o = rstr(d, o)
        skel, o = rstr(d, o)
        key = gethash(name)
        if key in self.composites:
            return
        comp = Composite(name, skel)
        for cid, eds, ede, _a, _b in chunks(d, cs, ce):
            if cid != COMPOSITE_PRIM:
                continue
            o = eds + 8                              # unknown, makeCopy
            child, o = rstr(d, o)
            type_, o = ru32(d, o)
            jid, o = ru32(d, o)
            comp.elements.append((child, type_, jid))
        self.composites[key] = comp

    # -- 0x00023000 Skeleton --------------------------------------------
    def _skeleton(self, d, ds, de, cs, ce):
        name, o = rstr(d, ds)
        key = gethash(name)
        if key in self.skeletons:
            return
        sk = Skeleton(name)
        for cid, jds, jde, _a, _b in chunks(d, cs, ce):
            if cid != SKELETON_JOINT:
                continue
            _jn, o = rstr(d, jds)
            parent, o = ru32(d, o)
            sk.parents.append(parent)
            sk.local.append(np.frombuffer(d, '<f4', 16, o).reshape(4, 4).astype(np.float64))
        sk.build()
        self.skeletons[key] = sk

    # -- 0x08800004 ZonePkg ---------------------------------------------
    def _zonepkg(self, d, ds, de, cs, ce):
        for cid, eds, ede, _a, _b in chunks(d, cs, ce):
            if cid != ZONEENTRY:
                continue
            wg, o = rstr(d, eds)
            n, o = ru32(d, o)
            f = list(struct.unpack_from('<%df' % n, d, o)) if n else []
            e = {'drawDistMin': f[0] if n > 0 else 0.0,
                 'drawDistMax': f[1] if n > 1 else 0.0}
            e['drawDistFade'] = f[2] if n >= 3 else (e['drawDistMax'] - e['drawDistMin']) * 0.2
            if n >= 5:
                e['otherPosition'] = [f[4], 0.0, -f[5] if n >= 6 else 0.0]
            self.zone[gethash(wg)] = e

    # -- 0x09900190/91 ScriptObject -> instanceobject placements --------
    def _scriptobject(self, d, pkg, cid, ds, de, cs, ce):
        _script, o = rstr(d, ds)
        objName, o = rstr(d, o)
        cls, o = rstr(d, o)
        model = None
        if cls == 'instanceobject':
            for pcid, pds, pde, pcs, pce in chunks(d, cs, ce):
                if pcid != SCRIPTPROP:
                    continue
                k, po = rstr(d, pds)
                t, po = ru32(d, po)
                if k == 'modelname' and t == 0:
                    model, po = rstr(d, po)
                    break
            if model:
                for pcid, pds, pde, pcs, pce in chunks(d, cs, ce):
                    if pcid != SCRIPTPROP:
                        continue
                    for lcid, lds, lde, _a, _b in chunks(d, pcs, pce):
                        if lcid == SCRIPTLOC:
                            loc = decode_location(d, lds, model, objName, pkg)
                            loc.support = self.support
                            self.placements.append(loc)
        if cid == SCRIPTGROUP:
            for ccid, cds, cde, ccs, cce in chunks(d, cs, ce):
                if ccid in (SCRIPTOBJECT, SCRIPTGROUP):
                    self._scriptobject(d, pkg, ccid, cds, cde, ccs, cce)


def decode_location(d, off, model, objName, pkg):
    """0x09900194, 48 bytes -- see re/notes/instances.md section 1.4 and re/instloc.py."""
    x, ycm, z = struct.unpack_from('<3f', d, off)
    w = struct.unpack_from('<9I', d, off + 12)
    if w[6]:
        s = (w[3] | (w[4] << 6) | (w[5] << 12)) / 1000.0
        scale = (s, s, s)
    else:
        scale = (w[3] / 20.0, w[4] / 20.0, w[5] / 20.0)
    p = Placement()
    p.model = model
    p.obj = objName
    p.pkg = pkg
    p.pos = (x, ycm / 100.0, z)
    p.rot = (w[0], w[1], w[2])
    p.scale = scale
    p.tint = w[7]
    p.state = w[8]
    p.support = False
    return p


def flipx_matrix(m):
    """conjugate a row-vector 4x4 with diag(-1,1,1,1): M' = S*M*S"""
    out = list(m)
    for i in range(4):
        for j in range(4):
            if (i == 0) != (j == 0):
                out[i * 4 + j] = -out[i * 4 + j]
    return out


def instance_matrix(p):
    """BuildInstanceMatrix 0x004b0000 / InstancePrimitive::AddInstance 0x0046f4b0:
    Matrix::SetRotation(-rx, -ry, +rz), rows scaled, translation in row 3.

    The result is row-major with the basis vectors in the rows, which is bit for bit
    the same 16 floats glTF wants (column major, translation in elements 12..14)."""
    a = -p.rot[0] * DEG2RAD
    b = -p.rot[1] * DEG2RAD
    c = p.rot[2] * DEG2RAD
    c1, s1 = np.cos(a), np.sin(a)
    c2, s2 = np.cos(b), np.sin(b)
    c3, s3 = np.cos(c), np.sin(c)
    m = [c3 * c2, s3 * c2, -s2, 0.0,
         c3 * s1 * s2 - s3 * c1, s2 * s3 * s1 + c3 * c1, c2 * s1, 0.0,
         c3 * c1 * s2 + s3 * s1, s3 * c1 * s2 - c3 * s1, c2 * c1, 0.0,
         p.pos[0], p.pos[1], p.pos[2], 1.0]
    for r, sc in enumerate(p.scale):
        for k in range(3):
            m[r * 4 + k] *= sc
    return [float(v) for v in m]


# ---------------------------------------------------------------------------
# glTF writing
# ---------------------------------------------------------------------------
FLOAT, UBYTE, USHORT, UINT = 5126, 5121, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


class GltfWriter(object):
    def __init__(self, binpath):
        self.bin = open(binpath, 'wb')
        self.binlen = 0
        self.g = {
            'asset': {'version': '2.0',
                      'generator': 'scarface-p3d re/p3d2gltf.py'},
            'scene': 0,
            'scenes': [{'nodes': []}],
            'nodes': [], 'meshes': [], 'materials': [], 'textures': [],
            'images': [], 'samplers': [], 'accessors': [], 'bufferViews': [],
        }

    # -- raw buffer ------------------------------------------------------
    def _write(self, buf, target=None):
        pad = (-self.binlen) & 3
        if pad:
            self.bin.write(b'\0' * pad)
            self.binlen += pad
        off = self.binlen
        self.bin.write(buf)
        self.binlen += len(buf)
        bv = {'buffer': 0, 'byteOffset': off, 'byteLength': len(buf)}
        if target:
            bv['target'] = target
        self.g['bufferViews'].append(bv)
        return len(self.g['bufferViews']) - 1

    def accessor(self, arr, ctype, atype, target, minmax=False):
        bv = self._write(arr.tobytes(), target)
        n = len(arr) if arr.ndim == 1 else arr.shape[0]
        a = {'bufferView': bv, 'componentType': ctype, 'count': int(n), 'type': atype}
        if ctype in (UBYTE, USHORT) and atype == 'VEC4':
            a['normalized'] = True
        if minmax:
            a['min'] = [float(v) for v in arr.min(axis=0)]
            a['max'] = [float(v) for v in arr.max(axis=0)]
        self.g['accessors'].append(a)
        return len(self.g['accessors']) - 1

    def imagebuffer(self, buf, mime):
        bv = self._write(buf)
        self.g['images'].append({'bufferView': bv, 'mimeType': mime})
        return len(self.g['images']) - 1

    def node(self, **kw):
        self.g['nodes'].append(kw)
        return len(self.g['nodes']) - 1

    # -- output ----------------------------------------------------------
    def finish(self, outpath):
        pad = (-self.binlen) & 3
        if pad:
            self.bin.write(b'\0' * pad)
            self.binlen += pad
        self.bin.flush()
        self.g['buffers'] = [{'byteLength': self.binlen}]
        for k in list(self.g):
            if isinstance(self.g[k], list) and not self.g[k]:
                del self.g[k]
        if outpath.endswith('.gltf'):
            binname = os.path.basename(outpath)[:-5] + '.bin'
            self.g['buffers'][0]['uri'] = binname
            self.bin.close()
            os.replace(self.bin.name, os.path.join(os.path.dirname(outpath) or '.', binname))
            with open(outpath, 'w') as f:
                json.dump(self.g, f)
            return
        js = json.dumps(self.g, separators=(',', ':')).encode('utf-8')
        js += b' ' * ((-len(js)) & 3)
        total = 12 + 8 + len(js) + 8 + self.binlen
        tmpname = self.bin.name
        self.bin.close()
        with open(outpath, 'wb') as out, open(tmpname, 'rb') as src:
            out.write(struct.pack('<4sII', b'glTF', 2, total))
            out.write(struct.pack('<I4s', len(js), b'JSON'))
            out.write(js)
            out.write(struct.pack('<I4s', self.binlen, b'BIN\0'))
            while True:
                blk = src.read(1 << 22)
                if not blk:
                    break
                out.write(blk)
        os.unlink(tmpname)


# ---------------------------------------------------------------------------
# geometry conversion
# ---------------------------------------------------------------------------
def strip_to_list(idx):
    """tristrip -> trilist, dropping the degenerate stitching triangles."""
    n = len(idx)
    if n < 3:
        return np.zeros((0, 3), np.uint32)
    a, b, c = idx[0:n - 2], idx[1:n - 1], idx[2:n]
    tri = np.empty((n - 2, 3), np.uint32)
    odd = np.arange(n - 2) & 1
    tri[:, 0] = np.where(odd, b, a)
    tri[:, 1] = np.where(odd, a, b)
    tri[:, 2] = c
    good = (tri[:, 0] != tri[:, 1]) & (tri[:, 1] != tri[:, 2]) & (tri[:, 0] != tri[:, 2])
    return tri[good]


class Exporter(object):
    def __init__(self, db, w, args):
        self.db = db
        self.w = w
        self.args = args
        self.meshCache = {}
        self.matCache = {}
        self.texCache = {}
        self.sampler = None
        self.stats = collections.Counter()
        self.missingShaders = set()
        self.missingMeshes = set()
        self.missingModels = collections.Counter()

    # -- textures / materials -------------------------------------------
    def get_sampler(self):
        if self.sampler is None:
            self.w.g['samplers'].append({'magFilter': 9729, 'minFilter': 9987,
                                         'wrapS': 10497, 'wrapT': 10497})
            self.sampler = len(self.w.g['samplers']) - 1
        return self.sampler

    def get_texture(self, name):
        key = gethash(name)
        if key in self.texCache:
            return self.texCache[key]
        tex = self.db.textures.get(key)
        idx = None
        if tex is not None and tex.png is not None and tex.fmt == 1:
            off, sz = tex.png
            img = self.w.imagebuffer(bytes(tex.data[off:off + sz]), 'image/png')
            self.w.g['images'][img]['name'] = tex.name
            self.w.g['textures'].append({'sampler': self.get_sampler(), 'source': img})
            idx = len(self.w.g['textures']) - 1
            self.stats['textures'] += 1
        self.texCache[key] = idx
        return idx

    def get_material(self, name):
        key = gethash(name)
        if key in self.matCache:
            return self.matCache[key]
        sh = self.db.shaders.get(key)
        if sh is None:
            self.missingShaders.add(name)
            self.matCache[key] = None
            return None
        p = sh.params
        blmd = int(p.get('BLMD', 0) or 0)
        atst = int(p.get('ATST', 0) or 0)
        mat = {'name': sh.name,
               'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1],
                                        'metallicFactor': 0.0, 'roughnessFactor': 1.0},
               'doubleSided': bool(p.get('2SID', 0)),
               'extras': {'pddiShader': sh.pddiShader,
                          'hasTranslucency': int(sh.translucent),
                          'params': dict(p)}}
        if blmd == 1:
            mat['alphaMode'] = 'BLEND'
        elif atst:
            mat['alphaMode'] = 'MASK'
            mat['alphaCutoff'] = float(p.get('ACTH', 0.5))
        elif sh.translucent:
            mat['alphaMode'] = 'BLEND'
        else:
            mat['alphaMode'] = 'OPAQUE'
        texname = p.get('TEX')
        if texname:
            t = self.get_texture(texname)
            if t is not None:
                mat['pbrMetallicRoughness']['baseColorTexture'] = {'index': t}
            else:
                mat['extras']['missingTexture'] = texname
        self.w.g['materials'].append(mat)
        self.matCache[key] = len(self.w.g['materials']) - 1
        self.stats['materials'] += 1
        return self.matCache[key]

    # -- meshes ----------------------------------------------------------
    def get_mesh(self, name, optional=False):
        key = gethash(name)
        if key in self.meshCache:
            return self.meshCache[key]
        mesh = self.db.meshes.get(key)
        if mesh is None:
            if not optional:
                self.missingMeshes.add(name)
            self.meshCache[key] = None
            return None
        prims = []
        for g in mesh.groups:
            p = self.primitive(g)
            if p:
                prims.append(p)
        idx = None
        if prims:
            self.w.g['meshes'].append({'name': mesh.name, 'primitives': prims})
            idx = len(self.w.g['meshes']) - 1
            self.stats['meshes'] += 1
            self.stats['primitives'] += len(prims)
        self.meshCache[key] = idx
        return idx

    def primitive(self, g):
        d, w = g.data, self.w
        if g.nverts == 0 or g.pos is None or g.indices is None:
            self.stats['skippedEmpty'] += 1
            return None
        off, n = g.indices
        idx = np.frombuffer(d, '<u4', n, off)
        if g.primType == PDDI_PRIM_TRISTRIP:
            tri = strip_to_list(idx)
        elif g.primType == PDDI_PRIM_TRIANGLES:
            tri = idx[:len(idx) - len(idx) % 3].reshape(-1, 3)
            good = (tri[:, 0] != tri[:, 1]) & (tri[:, 1] != tri[:, 2]) & (tri[:, 0] != tri[:, 2])
            if not good.all():
                tri = tri[good]
        else:
            self.stats['skippedPrimType'] += 1       # lines / points, not exported
            return None
        if len(tri) == 0:
            self.stats['skippedEmpty'] += 1
            return None

        if self.args.flip_x:
            tri = tri[:, [0, 2, 1]]                   # mirroring inverts the winding

        attrs = {}
        off, n = g.pos
        pos = np.frombuffer(d, '<f4', 3 * n, off).reshape(-1, 3)
        if self.args.flip_x:
            pos = pos * np.float32([-1, 1, 1])
        attrs['POSITION'] = w.accessor(pos, FLOAT, 'VEC3', ARRAY_BUFFER, minmax=True)
        if g.normal and g.normal[1] == len(pos):
            off, n = g.normal
            nrm = np.frombuffer(d, '<f4', 3 * n, off).reshape(-1, 3)
            if self.args.flip_x:
                nrm = nrm * np.float32([-1, 1, 1])
            attrs['NORMAL'] = w.accessor(nrm, FLOAT, 'VEC3', ARRAY_BUFFER)
        nv = len(pos)
        for slot, ch in enumerate(sorted(g.uv)[:2]):  # renumber: glTF wants 0,1,... dense
            off, n = g.uv[ch]
            if n != nv:
                continue
            uv = np.frombuffer(d, '<f4', 2 * n, off).reshape(-1, 2).copy()
            uv[:, 1] = -uv[:, 1]                      # the engine samples at (u,-v)
            attrs['TEXCOORD_%d' % slot] = w.accessor(uv, FLOAT, 'VEC2', ARRAY_BUFFER)
        if g.colour and g.colour[1] == nv:
            off, n = g.colour
            c = np.frombuffer(d, np.uint8, 4 * n, off).reshape(-1, 4)
            rgba = c[:, [2, 1, 0, 3]].copy()          # pddiColour is 0xAARRGGBB -> B,G,R,A
            attrs['COLOR_0'] = w.accessor(rgba, UBYTE, 'VEC4', ARRAY_BUFFER)

        if int(tri.max()) >= len(pos):               # broken index list, drop the group
            self.stats['skippedBadIndices'] += 1
            return None
        mx = int(tri.max())
        ind = tri.reshape(-1)
        if mx < 0x10000:
            acc = w.accessor(ind.astype(np.uint16), USHORT, 'SCALAR', ELEMENT_ARRAY_BUFFER)
        else:
            acc = w.accessor(ind.astype(np.uint32), UINT, 'SCALAR', ELEMENT_ARRAY_BUFFER)
        prim = {'attributes': attrs, 'indices': acc, 'mode': 4}
        mat = self.get_material(g.shader)
        if mat is not None:
            prim['material'] = mat
        self.stats['triangles'] += len(tri)
        return prim

    # -- world geo -------------------------------------------------------
    def composite_children(self, compName):
        comp = self.db.composite(compName)
        if comp is None:
            self.missingMeshes.add(compName)
            return []
        skel = self.db.skeleton(comp.skeleton) if comp.skeleton else None
        out = []
        for child, type_, jid in comp.elements:
            if not (type_ & 1):                       # only Geometry elements
                continue
            m = self.get_mesh(child)
            if m is None:
                continue
            node = {'name': child, 'mesh': m}
            if skel and skel.world is not None and 0 <= jid < len(skel.world):
                wm = skel.world[jid]
                if not np.allclose(wm, np.eye(4)):
                    m = [float(v) for v in wm.reshape(-1)]
                    node['matrix'] = flipx_matrix(m) if self.args.flip_x else m
            out.append(self.w.node(**node))
        return out

    def export_worldgeos(self):
        bypkg = collections.OrderedDict()
        for wg in self.db.worldgeos:
            if wg.support and not self.args.lod:      # shared shader/model library
                continue                              # (holds the city-wide LOD backdrop)
            if wg.type == 3 and not self.args.lod:
                self.stats['skippedLowLOD'] += 1
                continue
            bypkg.setdefault(wg.pkg, []).append(wg)
        roots = []
        for pkg, lst in bypkg.items():
            kids = []
            for wg in lst:
                children = self.composite_children(wg.composite)
                if not children:
                    self.stats['emptyWorldGeo'] += 1
                    continue
                extras = {'worldGeoType': wg.type,
                          'worldGeoTypeName': WORLDGEO_TYPES.get(wg.type, str(wg.type)),
                          'composite': wg.composite, 'package': pkg,
                          'flags': worldgeo_flags(wg.name)}
                z = self.db.zone.get(gethash(wg.name))
                if z:
                    extras.update(z)
                kids.append(self.w.node(name=wg.name, children=children, extras=extras))
                self.stats['worldGeos'] += 1
            if kids:
                roots.append(self.w.node(name=pkg, children=kids))
        return roots

    # -- instances -------------------------------------------------------
    def export_instances(self):
        bymodel = collections.OrderedDict()
        for p in self.db.placements:
            if p.support:
                continue
            bymodel.setdefault(p.model, []).append(p)
        roots = []
        for model, lst in bymodel.items():
            shape = self.get_mesh(model + 'InstanceShape', optional=True)
            lod = self.get_mesh(model + 'LODShape', optional=True)
            if shape is None and lod is None:
                self.missingModels[model] += len(lst)
                continue
            kids = []
            for p in lst:
                sub = []
                if shape is not None:
                    sub.append(self.w.node(name=model + 'InstanceShape', mesh=shape))
                if lod is not None:
                    sub.append(self.w.node(name=model + 'LODShape', mesh=lod))
                extras = {'model': model, 'tint': p.tint, 'state':
                          STATE_NAMES[p.state] if p.state < len(STATE_NAMES) else p.state,
                          'object': p.obj, 'package': p.pkg,
                          'alpha': 1.0 if p.tint == 0 else round(p.tint / 15.0, 4)}
                m = instance_matrix(p)
                if self.args.flip_x:
                    m = flipx_matrix(m)
                kids.append(self.w.node(name='%s_%d' % (model, len(kids)),
                                        matrix=m, children=sub, extras=extras))
                self.stats['placements'] += 1
            roots.append(self.w.node(name=model, children=kids))
            self.stats['instanceModels'] += 1
        if not roots:
            return []
        return [self.w.node(name='instances', children=roots)]


def worldgeo_flags(name):
    """the prefix tests of the retail WorldGeoLoader (re/notes/renderables.md section 4)"""
    n = name.lower()
    f = []
    if n.startswith('details_') or n.startswith('cbvlitdecals_'):
        f.append('isDetails')
    if n.startswith('skyline_'):
        f.append('isSkyline')
    if n.startswith('shells_') or n.startswith('underwater_'):
        f.append('drawFirst')
    if n.startswith('low_lod_'):
        f.append('isLowLOD')
    return f


# ---------------------------------------------------------------------------
# package selection
# ---------------------------------------------------------------------------
# Packages every region needs but that hold no map geometry of their own: the global
# shader/texture libraries (Common.p3d, *_region.p3d, miami_lod.p3d) and the eco-prop
# model libraries the instance placements resolve against (*_region_D.p3d, *_lod_D.p3d).
# They are loaded into the database but their own world geos / placements are not
# exported.  21 MB in total, so it is cheaper to load all of them than to guess.
SUPPORT_GLOBS = ['Common.p3d', '*_region*.p3d', '*_LOD*.p3d', 'miami_lod*.p3d']


def support_packages(pkgdir, already):
    import fnmatch
    out = []
    for f in sorted(os.listdir(pkgdir)):
        if not f.endswith('.p3d') or f.lower() in already:
            continue
        for g in SUPPORT_GLOBS:
            if fnmatch.fnmatch(f.lower(), g.lower()):
                out.append(os.path.join(pkgdir, f))
                break
    return out


def region_packages(pkgdir, region):
    """<region>_*.p3d plus every <prefix>_region*.p3d whose prefix the region starts with."""
    all_ = sorted(os.listdir(pkgdir))
    low = region.lower()
    files = [f for f in all_ if f.lower().startswith(low + '_') and f.endswith('.p3d')]
    extra = []
    for f in all_:
        if '_region' not in f.lower() or not f.endswith('.p3d'):
            continue
        base = f.lower().split('_region')[0]
        if low.startswith(base):
            extra.append(f)
    return [os.path.join(pkgdir, f) for f in files + extra]


def all_packages(pkgdir):
    return [os.path.join(pkgdir, f) for f in sorted(os.listdir(pkgdir)) if f.endswith('.p3d')]


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default='world.glb', help='output .glb or .gltf')
    ap.add_argument('--region', help='e.g. sbeachn, havana, downtown, ind')
    ap.add_argument('--files', nargs='+', help='explicit package list')
    ap.add_argument('--all', action='store_true', help='every package in the zone dir')
    ap.add_argument('--pkgdir', default=os.path.join(HERE, '..', 'assets', 'packages', 'z04'))
    ap.add_argument('--no-instances', dest='instances', action='store_false',
                    help='skip the eco-prop / instanceobject placements')
    ap.add_argument('--lod', action='store_true',
                    help='also export the type 3 low-LOD world geos, including the '
                         'city-wide backdrop in miami_lod.p3d / islands_LOD.p3d')
    ap.add_argument('--flip-x', action='store_true',
                    help='negate world X (and the winding) so the map is not mirrored; '
                         'this is what p3dview does on screen. Off by default: the default '
                         'export keeps the native Pure3D coordinates, which are left handed, '
                         'so text reads backwards in Blender. See re/notes/gltf_export.md.')
    ap.add_argument('--no-support', dest='support', action='store_false',
                    help='do not auto-load the shared shader/model libraries')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()

    pkgdir = os.path.normpath(args.pkgdir)
    if args.files:
        files = [f if os.path.sep in f else os.path.join(pkgdir, f) for f in args.files]
    elif args.region:
        files = region_packages(pkgdir, args.region)
    elif args.all:
        files = all_packages(pkgdir)
    else:
        ap.error('one of --region, --files or --all is required')
    if not files:
        ap.error('no packages matched')
    support = []
    if args.support:
        support = support_packages(pkgdir, set(os.path.basename(f).lower() for f in files))

    t0 = time.time()
    db = Database()
    for f, is_support in [(f, False) for f in files] + [(f, True) for f in support]:
        if not os.path.exists(f):
            print('missing package %s' % f, file=sys.stderr)
            continue
        if os.path.getsize(f) < 16:
            continue
        db.load(f, is_support, args.verbose)
    t1 = time.time()
    print('loaded %d packages in %.1fs: %d meshes, %d shaders, %d textures, '
          '%d composites, %d world geos, %d placements'
          % (len(db.packages), t1 - t0, len(db.meshes), len(db.shaders), len(db.textures),
             len(db.composites), len(db.worldgeos), len(db.placements)))

    out = os.path.abspath(args.out)
    w = GltfWriter(out + '.tmpbin')
    ex = Exporter(db, w, args)
    roots = ex.export_worldgeos()
    if args.instances:
        roots += ex.export_instances()
    w.g['scenes'][0]['nodes'] = roots
    w.g['asset']['extras'] = {
        'source': 'Scarface: The World Is Yours (PC) Pure3D packages',
        'packages': db.packages,
        'coordinates': 'native Pure3D (left handed, X mirrored)' if not args.flip_x
                       else 'X negated w.r.t. native Pure3D (right handed, as on screen)',
        'uv': 'V negated (the engine samples at (u,-v))',
    }
    w.finish(out)
    t2 = time.time()

    s = ex.stats
    print('exported %d world geos, %d placements of %d models, %d meshes / %d primitives / '
          '%d triangles, %d materials, %d textures in %.1fs'
          % (s['worldGeos'], s['placements'], s['instanceModels'], s['meshes'],
             s['primitives'], s['triangles'], s['materials'], s['textures'], t2 - t1))
    if s['skippedLowLOD']:
        print('  skipped %d low-LOD world geos (--lod to keep them)' % s['skippedLowLOD'])
    if s['skippedEmpty'] or s['skippedPrimType']:
        print('  skipped %d empty/degenerate prim groups, %d non-triangle prim groups'
              % (s['skippedEmpty'], s['skippedPrimType']))
    if ex.missingModels:
        n = sum(ex.missingModels.values())
        print('  %d models with no InstanceShape/LODShape (%d placements): %s'
              % (len(ex.missingModels), n,
                 ', '.join(m for m, _ in ex.missingModels.most_common(8))))
    if ex.missingShaders:
        print('  %d shaders not found (untextured primitives)' % len(ex.missingShaders))
    if ex.missingMeshes:
        print('  %d meshes/composites not found' % len(ex.missingMeshes))
    print('%s: %.1f MB' % (out, os.path.getsize(out) / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main())
