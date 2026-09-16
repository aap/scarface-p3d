#!/usr/bin/env python3
"""coverage_report.py -- bucket the attribution from coverage.py and print the tables.
Writes re/coverage_funcs.txt  (addr size bucket subsystem class how)."""
import os, sys, pickle, collections, re, bisect, struct, json

D = os.path.dirname(os.path.abspath(__file__))
LEAK = '/u/aap/lib/pure3d/scarface_src'
SHR  = '/u/aap/fun/ps2engines/extracted/shr'
C = pickle.load(open(D + '/.cov_cache.pkl', 'rb'))
attr = C['attr']

funcs = []
for l in open(D + '/idb_funcs.txt'):
    s, e, n = l.split(None, 2); funcs.append((int(s, 16), int(e, 16), n.strip()))
funcs.sort()
fstart = [f[0] for f in funcs]; fend = {f[0]: f[1] for f in funcs}
TOT = sum(e - s for s, e, _ in funcs)

BASE = 0x401000
IMG = open(D + '/scarface_unpacked.bin', 'rb').read()
def cstr(a, ml=160):
    o = a - BASE
    if o < 0 or o >= len(IMG): return None
    e = IMG.find(b'\0', o, o + ml)
    if e < 0: return None
    b = IMG[o:e]
    if len(b) < 4: return None
    try: s = b.decode('ascii')
    except: return None
    return s if all(32 <= ord(c) < 127 for c in s) else None
def func_of(a):
    i = bisect.bisect_right(fstart, a) - 1
    return funcs[i][0] if i >= 0 and funcs[i][0] <= a < funcs[i][1] else None
src2tgt = collections.defaultdict(list)
for l in open(D + '/xrefs_to.txt'):
    if '<-' not in l: continue
    lhs, rhs = l.split('<-', 1); tgt = int(lhs.split()[0], 16)
    for tok in rhs.split():
        if ':' not in tok: continue
        sa, kind = tok.rsplit(':', 1)
        try: sa = int(sa, 16)
        except ValueError: continue
        f = func_of(sa)
        if f is not None: src2tgt[f].append(tgt)

# ============================================== leak / SHR class definition index
defre = re.compile(r'^\s*(?:class|struct)\s+([A-Za-z_]\w*)\s*(?::|\{|\s*$)')
def index(root, skip=(), exts=('.hpp', '.h', '.inl')):
    cls = {}
    for r, dirs, files in os.walk(root):
        if any(s in r for s in skip): continue
        for fn in files:
            if fn.startswith('._') or not fn.endswith(exts): continue
            p = os.path.join(r, fn)
            try: txt = open(p, errors='ignore').read()
            except OSError: continue
            for line in txt.splitlines():
                m = defre.match(line)
                if m: cls.setdefault(m.group(1), os.path.relpath(p, root))
    return cls
leak_cls = index(LEAK, exts=('.hpp', '.h', '.inl', '.cpp'))
# namespaces defined in the leak, and every X:: that the leak's .cpp files define
NOTOURS = {'std','pure3d','core','content','renderer','om','audio','math','container',
           'ravenphysics','movie','fight','pddi','occlude','prop','that','stdext'}
leak_ns = {}
leak_owner = {}
for r, dirs, files in os.walk(LEAK):
    for fn in files:
        if fn.startswith('._') or not fn.endswith(('.cpp', '.hpp')): continue
        p = os.path.join(r, fn)
        try: txt = open(p, errors='ignore').read()
        except OSError: continue
        rel = os.path.relpath(p, LEAK)
        for m in re.finditer(r'\bnamespace\s+([A-Za-z_]\w*)', txt):
            if m.group(1) not in NOTOURS: leak_ns.setdefault(m.group(1), rel)
        if fn.endswith('.cpp'):
            # a real out-of-line definition starts in column 0, has a '(' and no ';'
            for line in txt.splitlines():
                if not line or line[0].isspace() or line.lstrip().startswith(('//', '/*', '#')):
                    continue
                if '(' not in line or line.rstrip().endswith(';'): continue
                m = re.match(r'(?:[\w:<>*&~\[\] ]+?[ *&]+)?([A-Za-z_]\w*)::(~?[A-Za-z_]\w*)\s*\(', line)
                if m and m.group(1) not in NOTOURS:
                    leak_owner.setdefault(m.group(1), rel)
shr_cls  = index(SHR, skip=('/sdks/',))
# SHR classes are often 't'-prefixed (tContext, tTexture) or p3d-prefixed
shr_alias = {}
for c, p in shr_cls.items():
    for a in (c, c[1:] if c.startswith('t') else None,
              c[4:] if c.startswith('pddi') else None,
              c[3:] if c.startswith('p3d') else None):
        if a and len(a) > 3: shr_alias.setdefault(a, p)

# leak: does a .cpp actually implement the class?
leak_impl = set()
for r, dirs, files in os.walk(LEAK):
    for fn in files:
        if fn.startswith('._') or not fn.endswith('.cpp'): continue
        try: txt = open(os.path.join(r, fn), errors='ignore').read()
        except OSError: continue
        for m in re.finditer(r'\b([A-Za-z_]\w*)::[~A-Za-z_]', txt): leak_impl.add(m.group(1))

# missing engine headers named by the leak's #include list
inc = collections.Counter()
for r, dirs, files in os.walk(LEAK):
    for fn in files:
        if fn.startswith('._') or not fn.endswith(('.cpp', '.hpp', '.h')): continue
        try: txt = open(os.path.join(r, fn), errors='ignore').read()
        except OSError: continue
        for m in re.finditer(r'#\s*include\s*[<"]([^">]+)[">]', txt): inc[m.group(1)] += 1
MISSDIRS = ('engine/', 'game/', 'math/', 'core/', 'container/', 'ravenphysics/',
            'fight/', 'content/', 'pure3d/', 'script/', 'object/', 'audio/')
missinc = {k: v for k, v in inc.items() if k.startswith(MISSDIRS)}
hdr2dir = {}
for h in missinc:
    b = os.path.basename(h).rsplit('.', 1)[0].lower()
    hdr2dir.setdefault(b, '/'.join(h.split('/')[:-1]))

# ==================================================================== bucketing
FACTORY = ('ConcreteClassRep', 'DynamicCaster', 'EntityType', 'MetaType',
           'MetaAttribScalar', 'MetaAttribArray', 'TrackFactoryTemplate',
           'ConditionFactoryTemplate', 'ActionFactoryTemplate', 'ParamName',
           'ClassRep', 'Singleton', 'AutoPtr', 'Ptr')
def unwrap(cls):
    """ConcreteClassRep<X> is emitted into X's translation unit -> charge it to X"""
    m = re.match(r'^([\w:]+?)<(.+)>$', cls)
    if not m: return cls
    tn = m.group(1).split('::')[-1]
    arg = m.group(2).split(',')[0]
    if tn in FACTORY and arg and arg[0].isupper() or (tn in FACTORY and '::' in arg):
        return arg if arg and not arg.islower() else cls
    return cls

CRT = re.compile(r'^(std|stdext|Concurrency|__non_rtti|type_info|exception|bad_|'
                 r'logic_error|runtime_error|length_error|out_of_range|ios_base|'
                 r'filebuf|stdiobuf|streambuf|Am[A-Z]|NN$)')

NS_RULE = [
    ('pure3d',       'SHR-DERIVED',   'atg/pure3d'),
    ('pddi',         'SHR-DERIVED',   'atg/pure3d-pddi'),
    ('core',         'SHR-DERIVED',   'atg/core'),
    ('container',    'SHR-DERIVED',   'atg/container'),
    ('math',         'SHR-DERIVED',   'atg/math'),
    ('content',      'SHR-DERIVED',   'atg/content'),
    ('movie',        'SHR-DERIVED',   'atg/movie'),
    ('audio',        'MISSING-ENGINE','atg/audio'),
    ('ravenphysics', 'MISSING-ENGINE','atg/ravenphysics'),
    ('renderer',     'MISSING-ENGINE','engine/render'),
    ('occlude',      'MISSING-ENGINE','engine/render-occlusion'),
    ('om',           'MISSING-ENGINE','engine/om'),
    ('fight',        'MISSING-ENGINE','lib/fight'),
    ('script',       'MISSING-ENGINE','engine/script'),
]
NSMAP = {k: (b, s) for k, b, s in NS_RULE}

NAME_HINT = [
    (re.compile(r'^(d3d|pddi|p3d|win32|Pure3D|Simple(Shader|Display))'), 'atg/pure3d'),
    (re.compile(r'^Fight|Track$|Condition$|^Param(Name|Value)'), 'lib/fight'),
    (re.compile(r'PresentationState$|^Presentation'), 'game'),
    (re.compile(r'^Cement|FileHandler$|^Inventory|Chunk'), 'engine/resource'),
    (re.compile(r'^Variety'),               'engine/object'),
    (re.compile(r'^CScript|Console|Namespace'), 'engine/script'),
    (re.compile(r'FlowClient$'),            'engine/flow'),
    (re.compile(r'^Script|Script(File|Object|Scheduler|Context)'), 'engine/script'),
    (re.compile(r'Renderable$|^Render'),    'engine/render'),
    (re.compile(r'Collision'),              'engine/collision'),
    (re.compile(r'^Path|Pathfind'),         'engine/pathfind'),
    (re.compile(r'Sound|Audio'),            'engine/sound'),
    (re.compile(r'^NIS'),                   'engine/nis'),
    (re.compile(r'Stream'),                 'engine/stream'),
    (re.compile(r'StateProp'),              'engine/stateprop'),
    (re.compile(r'^FE|Frontend'),           'engine/frontend'),
    (re.compile(r'Overlay'),                'engine/overlay'),
    (re.compile(r'Controller|Input'),       'engine/controller'),
    (re.compile(r'SaveGame'),               'engine/savegame'),
    (re.compile(r'Memory|Heap|Allocator'),  'engine/memory'),
    (re.compile(r'Resource|Package|Drive'), 'engine/resource'),
    (re.compile(r'Trigger'),                'engine/trigger'),
    (re.compile(r'Database|Broker'),        'engine/database'),
    (re.compile(r'Vehicle|Locomotion|Wheel|Boat|Airplane|Helicopter'), 'engine/vehicle'),
    (re.compile(r'Character|Anim'),         'engine/character'),
]

SDKDIR = {'math', 'container', 'core', 'pure3d', 'content',
          'atg/math', 'atg/container', 'atg/core', 'atg/pure3d', 'atg/content',
          'atg/pure3d-pddi', 'atg/movie'}
SDKFIX = {'math': 'atg/math', 'container': 'atg/container', 'core': 'atg/core',
          'content': 'atg/content'}
def _mk(sub):
    top = sub.split('/')[0]
    if sub in SDKDIR or top in SDKDIR:
        if top in SDKFIX: sub = SDKFIX[top] + sub[len(top):]
        elif top == 'pure3d': sub = 'atg/pure3d'
        return 'SHR-DERIVED', sub
    return 'MISSING-ENGINE', sub

def classify(cls):
    """-> (bucket, subsystem)"""
    eff = unwrap(cls)
    root = eff.split('::')[0]
    bare = eff.split('<')[0]
    leaf = bare.split('::')[-1]
    outer = bare.split('::')[0]
    if CRT.match(root) or CRT.match(leaf):
        return 'THIRD-PARTY', 'CRT+STL-inlined'
    if root in NSMAP: return NSMAP[root]
    # the leaf name is the strongest signal; then the engine header that declares it
    for nm in (leaf, outer):
        if nm in leak_cls:   return 'LEAK', 'gameobject/' + leak_cls[nm].split('/')[0]
        if nm in leak_owner: return 'LEAK', 'gameobject/' + leak_owner[nm].split('/')[0]
        if nm.lower() in hdr2dir: return _mk(hdr2dir[nm.lower()])
        b = re.sub(r'(FlowClient|Client)$', '', nm).lower()
        if b and b + 'manager' in hdr2dir: return _mk(hdr2dir[b + 'manager'])
        if b and b in hdr2dir: return _mk(hdr2dir[b])
    if outer in leak_ns: return 'LEAK', 'gameobject/' + leak_ns[outer].split('/')[0]
    for rx, sub in NAME_HINT:
        if rx.search(leaf) or rx.search(outer): return _mk(sub)
    if leaf.endswith('Manager'): return 'MISSING-ENGINE', 'engine/MANAGER?'
    return 'MISSING-ENGINE', 'engine/UNCLASSIFIED'

# ------------------------------------------------- third-party by address/strings
PNGZ = re.compile(r'png_|zlib|IHDR|inflate|deflate|libpng|zTXt|Adler|Hufman|'
                  r'incorrect data check|invalid distance')
seed = set()
for f in fstart:
    if f >= 0x860000: continue
    for t in src2tgt.get(f, ()):
        s = cstr(t)
        if s and PNGZ.search(s): seed.add(f); break
pz_lo, pz_hi = (min(seed), max(seed)) if seed else (0, 0)

def tp(f):
    if f >= 0x860000: return 'CRT+STL+ActiveMark-wrapper'
    if pz_lo <= f <= pz_hi: return 'libpng+zlib'
    return None

# ============================ module ranges from the layout, for the leftovers
res = {}   # f -> (bucket, subsystem, cls, how)
for f in fstart:
    t = tp(f)
    if t is not None:
        res[f] = ('THIRD-PARTY', t, attr.get(f, ('-',))[0], 'region')
    elif f in attr:
        c, how, prio = attr[f]
        b, s = classify(c)
        res[f] = (b, s, c, how)

# smooth: unattributed functions get the subsystem of the nearest attributed
# neighbours when both agree (same object-file run in the linker's output)
known = [i for i, f in enumerate(fstart) if f in res]
ki = {i: True for i in known}
for a, b in zip(known, known[1:]):
    if b - a <= 1: continue
    sa, sb = res[fstart[a]][1], res[fstart[b]][1]
    ba, bb = res[fstart[a]][0], res[fstart[b]][0]
    if sa == sb and fstart[b] - fend[fstart[a]] < 0x4000:
        for k in range(a+1, b):
            res[fstart[k]] = (ba, sa, '(module-range)', 'layout')
# remaining: nearest attributed neighbour within 8K, else UNKNOWN
for i, f in enumerate(fstart):
    if f in res: continue
    j = bisect.bisect_left(known, i)
    cand = []
    if j < len(known): cand.append(known[j])
    if j > 0: cand.append(known[j-1])
    best = None
    for k in cand:
        d = abs(fstart[k] - f)
        if best is None or d < best[0]: best = (d, k)
    if best: res[f] = (res[fstart[best[1]]][0], res[fstart[best[1]]][1], '(nearby)', 'layout-weak')
    else: res[f] = ('UNKNOWN', 'unknown', '-', '-')

# ====================================================================== output
with open(D + '/coverage_funcs.txt', 'w') as fp:
    fp.write('# addr size bucket subsystem class how\n')
    for f in fstart:
        b, s, c, h = res[f]
        fp.write('%08x %6d %-14s %-34s %-50s %s\n' % (f, fend[f]-f, b, s, c, h))

bb = collections.Counter(); bn = collections.Counter()
sb = collections.Counter(); sn = collections.Counter(); sbuck = {}
cb = collections.Counter()
for f in fstart:
    b, s, c, h = res[f]; n = fend[f]-f
    bb[b] += n; bn[b] += 1; sb[(b, s)] += n; sn[(b, s)] += 1
    cb[(b, s, c)] += n
print('TOTAL %d functions, %d code bytes\n' % (len(funcs), TOT))
print('%-16s %10s %6s %9s %6s' % ('BUCKET', 'bytes', '%', 'funcs', '%'))
for b, v in bb.most_common():
    print('%-16s %10d %5.1f%% %9d %5.1f%%' % (b, v, 100*v/TOT, bn[b], 100*bn[b]/len(funcs)))
print()
print('%-14s %-38s %10s %6s %7s' % ('BUCKET', 'SUBSYSTEM', 'bytes', '%', 'funcs'))
for (b, s), v in sb.most_common(70):
    print('%-14s %-38s %10d %5.1f%% %7d' % (b, s, v, 100*v/TOT, sn[(b, s)]))
CONF = {'vtable': 'strong', 'symname': 'strong', 'username': 'strong',
        'ctor/vtref': 'strong', 'between': 'strong', 'between-ns': 'medium',
        'callers': 'medium', 'region': 'strong', 'layout': 'weak',
        'layout-weak': 'weak', '-': 'weak'}
conf = collections.Counter(); bconf = collections.Counter()
for f in fstart:
    b, s_, c, h = res[f]; n = fend[f]-f
    k = CONF.get(h.split('(')[0], 'weak')
    conf[k] += n; bconf[(b, k)] += n
print('\n--- evidence strength (bytes) ---')
for k in ('strong', 'medium', 'weak'):
    print('  %-7s %9d %5.1f%%' % (k, conf[k], 100*conf[k]/TOT))
print('  %-14s %8s %8s %8s' % ('bucket', 'strong', 'medium', 'weak'))
for b in bb:
    print('  %-14s %8d %8d %8d' % (b, bconf[(b,'strong')], bconf[(b,'medium')], bconf[(b,'weak')]))

# RTTI class census by bucket (distinct vtable classes, not bytes)
vtc = set()
for a, c in C['vt_addrs'].items(): vtc.add(c)
cls_b = collections.Counter()
for c in vtc: cls_b[classify(c)[0]] += 1
print('\n--- distinct RTTI (vtable) classes by bucket: %d total ---' % len(vtc))
for b, n in cls_b.most_common(): print('  %-15s %5d  %4.1f%%' % (b, n, 100*n/len(vtc)))
nontp = TOT - bb['THIRD-PARTY']
print('\n--- share of the game/engine/SDK code only (third-party excluded, %d bytes) ---' % nontp)
for b in ('LEAK', 'SHR-DERIVED', 'MISSING-ENGINE', 'UNKNOWN'):
    if bb[b]: print('  %-15s %9d  %4.1f%%' % (b, bb[b], 100*bb[b]/nontp))

print('\n--- 30 largest MISSING subsystems ---')
for (b, s), v in sb.most_common():
    if b != 'MISSING-ENGINE': continue
    print('%-40s %8d %5.1f%%  %5d funcs' % (s, v, 100*v/TOT, sn[(b, s)]))
print('\n--- 40 largest MISSING classes ---')
mc = collections.Counter({k: v for k, v in cb.items() if k[0] == 'MISSING-ENGINE'})
for (b, s, c), v in mc.most_common(40): print('%8d  %-34s %s' % (v, s, c))
print('\n--- 25 largest LEAK classes ---')
lc = collections.Counter({k: v for k, v in cb.items() if k[0] == 'LEAK'})
for (b, s, c), v in lc.most_common(25):
    print('%8d  %-26s %-34s %s' % (v, s, c, 'impl' if c.split('<')[0] in leak_impl else 'HEADER-ONLY'))
print('\n--- most-included missing headers (leak needs these to compile) ---')
for h, n in sorted(missinc.items(), key=lambda kv: -kv[1])[:45]:
    print('%5d  %s' % (n, h))
print('\nmissing include families: %s' % dict(collections.Counter(
    h.split('/')[0] for h in missinc).most_common()))
print('distinct missing headers: %d (total %d include sites)' % (len(missinc), sum(missinc.values())))
print('\nleak classes: %d defined, %d with a .cpp impl' % (len(leak_cls), len(leak_cls & leak_impl.__class__(leak_cls.keys()) & leak_impl) if 0 else len(set(leak_cls) & leak_impl)))
json.dump({'sb': {f'{b}|{s}': v for (b, s), v in sb.items()}, 'bb': dict(bb), 'TOT': TOT},
          open(D + '/.cov_report.json', 'w'), indent=1)
