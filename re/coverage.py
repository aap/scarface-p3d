#!/usr/bin/env python3
"""coverage.py -- attribute every function in the Scarface PC exe to a class/subsystem
and bucket it into LEAK / SHR-DERIVED / MISSING-ENGINE / THIRD-PARTY / UNKNOWN.

Evidence used, in order of confidence:
  vtable slots -> class      (RTTI ??_7 symbols, ~2150 vtables)
  mangled/user symbol names
  ctor/dtor: function taking the address of a vtable
  call-graph propagation (unattributed callee whose attributed callers all agree)
  contiguity: unattributed functions between two functions of the same class/namespace
  linker layout: module ranges derived from the above, used as a last-resort fallback
  strings / __FILE__ paths / import tables for third-party identification

Outputs re/coverage_funcs.txt: addr size bucket subsystem class how
"""
import os, sys, struct, bisect, collections, json, re, pickle

D = os.path.dirname(os.path.abspath(__file__))
BASE = 0x401000
IMG = open(D + '/scarface_unpacked.bin', 'rb').read()
CODE = [(0x401000, 0x72f000), (0x860000, 0x9ce000)]
def is_code(a): return any(lo <= a < hi for lo, hi in CODE)
def u32(a):
    o = a - BASE
    if o < 0 or o + 4 > len(IMG): return None
    return struct.unpack('<I', IMG[o:o+4])[0]
def cstr(a, maxlen=160):
    o = a - BASE
    if o < 0 or o >= len(IMG): return None
    e = IMG.find(b'\0', o, o + maxlen)
    if e < 0: return None
    b = IMG[o:e]
    if len(b) < 4: return None
    try: s = b.decode('ascii')
    except: return None
    return s if all(32 <= ord(c) < 127 for c in s) else None

# =========================================================== MSVC demangling
class MP:
    """minimal MSVC qualified-name parser"""
    def __init__(s, t): s.t = t; s.i = 0; s.back = []
    def peek(s): return s.t[s.i] if s.i < len(s.t) else ''
    def qname(s):
        """parse <frag>* '@' -> ['Inner','Outer',...] reversed to Outer::Inner"""
        parts = []
        while s.i < len(s.t):
            c = s.peek()
            if c == '@': s.i += 1; break
            if c == '?':
                if s.t.startswith('?$', s.i):
                    parts.append(s.template()); continue
                # ?A@ anonymous namespace, ?<digit> etc
                j = s.t.find('@', s.i)
                if j < 0: j = len(s.t)
                parts.append(s.t[s.i:j]); s.i = j + 1; continue
            if c.isdigit():
                k = int(c); s.i += 1
                parts.append(s.back[k] if k < len(s.back) else '?')
                continue
            j = s.t.find('@', s.i)
            if j < 0: j = len(s.t)
            frag = s.t[s.i:j]; s.i = j + 1
            parts.append(frag)
            if frag not in s.back: s.back.append(frag)
        return list(reversed(parts))
    def template(s):
        s.i += 2                      # skip ?$
        j = s.t.find('@', s.i)
        name = s.t[s.i:j]; s.i = j + 1
        if name not in s.back: s.back.append(name)
        args = []
        while s.i < len(s.t):
            if s.peek() == '@': s.i += 1; break
            args.append(s.type())
        return name + '<' + ','.join(a for a in args if a) + '>'
    def type(s):
        c = s.peek()
        if c == '': return ''
        if c in 'VUT':            # class / struct / union
            s.i += 1
            return '::'.join(s.qname())
        if s.t.startswith('W4', s.i):
            s.i += 2; return '::'.join(s.qname())
        if c == 'P' or c == 'A' or c == 'Q' or c == 'R':   # ptr / ref
            s.i += 1
            while s.peek() in 'ABCDEFGHIJ' and s.i + 1 < len(s.t) and s.t[s.i+1] in 'PAQRVUW_MNHDXZ0123456789':
                s.i += 1; break
            return s.type()
        if c == '_':
            s.i += 2; return '_'
        if c == '$':
            s.i += 1
            if s.peek() == '0':
                s.i += 1
                while s.i < len(s.t) and s.t[s.i] != '@': s.i += 1
                s.i += 1
                return ''
            return ''
        if c.isdigit():
            k = int(c); s.i += 1
            return s.back[k] if k < len(s.back) else '?'
        s.i += 1
        return {'M': 'float', 'H': 'int', 'N': 'double', 'D': 'char', 'X': 'void',
                'I': 'uint', 'J': 'long', 'K': 'ulong', 'F': 'short', 'G': 'ushort',
                'E': 'uchar', 'C': 'schar', 'Z': ''}.get(c, '')

def demangle_class(sym, prefix):
    """class name out of a ??_7X@@6B@ / ??_R0?AVX@@@8 / ??_R3X@@8 style symbol"""
    try:
        p = MP(sym[len(prefix):])
        q = p.qname()
        return '::'.join(q) if q else None
    except Exception:
        return None

def sym_class(n):
    """enclosing class of a mangled function symbol ?Meth@Class@NS@@... or ??0Class@..."""
    try:
        if n.startswith('??'):
            m = re.match(r'\?\?(_?[0-9A-Z])(.*)', n)
            if not m: return None
            rest = m.group(2)
            if rest.startswith('?$'):
                p = MP(rest); q = p.qname(); return '::'.join(q) if q else None
            q = MP(rest).qname()
            return '::'.join(q) if q else None
        if n.startswith('?'):
            rest = n[1:]
            if rest.startswith('?$'):
                p = MP(rest); p.template()
                q = p.qname()
                return '::'.join(q) if q else None
            j = rest.find('@')
            if j < 0: return None
            q = MP(rest[j+1:]).qname()
            return '::'.join(q) if q else None
    except Exception:
        return None
    if '::' in n: return '::'.join(n.split('::')[:-1])
    return None

# ============================================================== input data
funcs = []
for l in open(D + '/idb_funcs.txt'):
    s, e, n = l.split(None, 2)
    funcs.append((int(s, 16), int(e, 16), n.strip()))
funcs.sort()
fstart = [f[0] for f in funcs]
fset = set(fstart); fend = {f[0]: f[1] for f in funcs}; fname = {f[0]: f[2] for f in funcs}
def func_of(a):
    i = bisect.bisect_right(fstart, a) - 1
    return funcs[i][0] if i >= 0 and funcs[i][0] <= a < funcs[i][1] else None

names = {}
for l in open(D + '/idb_names.txt'):
    p = l.split(None, 1)
    if len(p) == 2: names.setdefault(int(p[0], 16), p[1].strip())
usernames = {}
for l in open(D + '/idb_usernames.txt'):
    p = l.split(None, 1)
    if len(p) == 2: usernames[int(p[0], 16)] = p[1].strip()

xref_to = collections.defaultdict(list)
src2tgt = collections.defaultdict(list)
for l in open(D + '/xrefs_to.txt'):
    if '<-' not in l: continue
    lhs, rhs = l.split('<-', 1)
    tgt = int(lhs.split()[0], 16)
    for tok in rhs.split():
        if ':' not in tok: continue
        sa, kind = tok.rsplit(':', 1)
        try: sa = int(sa, 16)
        except ValueError: continue
        xref_to[tgt].append((sa, kind))
        f = func_of(sa)
        if f is not None: src2tgt[f].append((tgt, kind))

# =================================================================== RTTI
vt_addrs = {}
for a, n in names.items():
    if n.startswith('??_7'):
        c = demangle_class(n, '??_7')
        if c: vt_addrs[a] = c
depth = {}; bases = collections.defaultdict(set)
td_class = {}
for a, n in names.items():
    if n.startswith('??_R0'):
        m = re.match(r'\?\?_R0\?A[VUW]4?(.*)@8$', n)
        if m:
            c = demangle_class('X' * 5 + m.group(1) + '@', '?????')
            if c: td_class[a] = c
for a, n in names.items():
    if not n.startswith('??_R3'): continue
    cls = demangle_class(n, '??_R3')
    if not cls: continue
    nb = u32(a + 8); pba = u32(a + 12)
    if nb is None or nb > 64: continue
    depth[cls] = nb
    if pba and 0x72f000 <= pba < 0x860000:
        for i in range(nb):
            p1 = u32(pba + 4*i)
            if not p1: continue
            ptd = u32(p1)
            if ptd in td_class and td_class[ptd] != cls: bases[cls].add(td_class[ptd])

vt_slots = {}
for a in sorted(vt_addrs):
    slots = []; i = 0
    while True:
        cur = a + 4*i
        if i > 0 and cur in names: break
        v = u32(cur)
        if v is None or not is_code(v) or v not in fset: break
        slots.append(v); i += 1
        if i > 400: break
    vt_slots[a] = slots

# ============================================== third-party identification
# 1. ActiveMark/SecuROM wrapper + static CRT/STL live in the high code region
# 2. libpng / zlib are statically linked in the low region -- locate by string refs
tp_seed = set()
PNGZ = re.compile(r'png_|zlib|IHDR|inflate|deflate|Incompatible libpng|zTXt|Adler')
for f in fstart:
    for tgt, kind in src2tgt.get(f, ()):
        s = cstr(tgt)
        if s and PNGZ.search(s): tp_seed.add(f); break
if tp_seed:
    pz_lo, pz_hi = min(tp_seed), max(tp_seed)
else:
    pz_lo = pz_hi = 0
# expand the png/zlib hull over the contiguous unattributed run it sits in
PNGZ_RANGE = (pz_lo, pz_hi)

def third_party(f):
    if f >= 0x860000: return 'CRT/STL + ActiveMark DRM wrapper'
    if PNGZ_RANGE[0] <= f <= PNGZ_RANGE[1]: return 'libpng / zlib'
    return None

# ============================================== attribution of class per func
attr = {}
def setattr_(f, cls, how, prio):
    old = attr.get(f)
    if old is None or prio < old[2]: attr[f] = (cls, how, prio)

slot_owner = collections.defaultdict(list)
for a, slots in vt_slots.items():
    for f in slots: slot_owner[f].append(vt_addrs[a])
for f, cl in slot_owner.items():
    setattr_(f, min(cl, key=lambda c: (depth.get(c, 99), len(c))), 'vtable', 10)

# IDA's FLIRT mislabels many game functions with CRT/STL names (86 copies of
# "??1stdiobuf@@UAE@XZ_N" that actually write game vtables).  Only trust a symbol
# name that occurs exactly once.
basecnt = collections.Counter(re.sub(r'_\d+$', '', n) for n in fname.values()
                              if not n.startswith('sub_'))
for f, n in fname.items():
    if n.startswith('sub_'): continue
    if basecnt[re.sub(r'_\d+$', '', n)] > 1: continue
    c = sym_class(n)
    if c: setattr_(f, c, 'symname', 8)
for a, n in usernames.items():
    f = a if a in fset else func_of(a)
    if f is None: continue
    c = sym_class(n)
    if c: setattr_(f, c, 'username', 7)

vt_users = collections.defaultdict(set)
for a, cls in vt_addrs.items():
    for src, kind in xref_to.get(a, []):
        f = func_of(src)
        if f is not None: vt_users[f].add(cls)
for f, cl in vt_users.items():
    if f in attr and attr[f][2] <= 10: continue
    setattr_(f, max(cl, key=lambda c: (depth.get(c, 0), len(c))), 'ctor/vtref', 12)

callers = collections.defaultdict(set)
for tgt, lst in xref_to.items():
    if tgt not in fset: continue
    for src, kind in lst:
        sf = func_of(src)
        if sf is not None and sf != tgt: callers[tgt].add(sf)

for rnd in range(3):
    new = {}
    for f in fstart:
        if f in attr: continue
        cs = {attr[c][0] for c in callers.get(f, ()) if c in attr}
        if len(cs) == 1: new[f] = cs.pop()
    for f, c in new.items(): setattr_(f, c, 'callers', 20 + rnd)

def tukey(c):
    p = c.split('::'); return '::'.join(p[:-1]) if len(p) > 1 else None
for rnd in range(2):
    idxs = [i for i, f in enumerate(fstart) if f in attr]
    for a, b in zip(idxs, idxs[1:]):
        if b - a <= 1 or b - a > 60: continue
        if fstart[b] - fend[fstart[a]] > 0x2000: continue
        ca, cb = attr[fstart[a]][0], attr[fstart[b]][0]
        if ca == cb:
            for k in range(a+1, b): setattr_(fstart[k], ca, 'between', 30)
        elif tukey(ca) and tukey(ca) == tukey(cb):
            for k in range(a+1, b): setattr_(fstart[k], tukey(ca) + '::?', 'between-ns', 32)

# ============================== module ranges from the resulting linker layout
# derive, per 4K page, the dominant top-level module of already-attributed code
pickle.dump(dict(attr=attr, vt_addrs=vt_addrs, depth=depth,
                 bases={k: list(v) for k, v in bases.items()}, vt_slots=vt_slots,
                 callers={k: list(v) for k, v in callers.items()}),
            open(D + '/.cov_cache.pkl', 'wb'))
print('stage1: %d/%d funcs attributed' % (len(attr), len(funcs)), file=sys.stderr)
