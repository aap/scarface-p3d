"""RCF ("ATG CORE CEMENT LIBRARY") reader: list or extract.

File layout (little endian, same on PC and PS2):
  0x00  char magic[32]   "ATG CORE CEMENT LIBRARY" + zero padding
  0x20  u32  version     0x01000102
  0x24  u32  dirOffset   (0x3c)
  0x28  u32  dirSize     nFiles*12
  0x2c  u32  nameOffset
  0x30  u32  nameSize
  0x34  u32  zero
  0x38  u32  nFiles

  directory entry (12 bytes, sorted ascending by hash):
        u32 hash, u32 offset, u32 size

  name table: u32 flags(0x800), u32 zero, then nFiles records
        u32 timestamp (unix), u32 flags(0x800), u32 zero, u32 nameLen,
        char name[nameLen]   (nameLen includes the terminating 0),
        then 3 filler bytes (always 3, the records are NOT aligned).
  The name records are in packer order, NOT in hash order, so names have to
  be matched to directory entries through the hash.

The filename hash (found in the PS2 ELF at 0x7541dc, PC: sub_6e5f10 area):
        h = 0
        skip one leading '\\'
        for each char c:  if (c < 'a') c += 32;  h = h*31 + c
i.e. a *blind* "tolower" that also shifts digits, '.', '\\', '_' ... and a
classic h*31 string hash.  Verified: all 4746 PC names reproduce the PC
directory exactly; 5177 of 5178 PS2 names do (one stale name record, see
notes/ps2.md).
"""
import struct, sys, os


def namehash(name):
    b = name.encode('latin1') if isinstance(name, str) else name
    if b[:1] == b'\\':
        b = b[1:]
    h = 0
    for c in b:
        if c < 97:
            c += 32
        h = (h * 31 + c) & 0xffffffff
    return h


def parse(path):
    """-> (entries, names, hdr) with entries = [(hash, offset, size)] and
    names = [name] in name-table order."""
    f = open(path, 'rb')
    h = f.read(0x3c)
    assert h[:23] == b'ATG CORE CEMENT LIBRARY'
    ver, diroff, dirsize, nameoff, namesize, _, nfiles = struct.unpack_from('<IIIIIII', h, 0x20)
    f.seek(diroff)
    ents = [struct.unpack('<III', f.read(12)) for _ in range(nfiles)]
    f.seek(nameoff)
    nd = f.read(namesize)
    names = []
    off = 8                                    # u32 flags, u32 zero
    for _ in range(nfiles):
        ts, flags, zero, nlen = struct.unpack_from('<IIII', nd, off)
        off += 16
        names.append(nd[off:off+nlen-1].decode('latin1'))
        off += nlen + 3                        # 3 filler bytes, no alignment
    assert len(names) == nfiles, (len(names), nfiles)
    return ents, names, (ver, diroff, dirsize, nameoff, namesize, nfiles)


def match(ents, names):
    """-> list of (name, hash, offset, size) for every directory entry."""
    byhash = {}
    for n in names:
        byhash.setdefault(namehash(n), []).append(n)
    out = []
    leftover_e = []
    used = set()
    for e in ents:
        l = byhash.get(e[0])
        if l:
            n = l.pop(0)
            used.add(n)
            out.append((n, e[0], e[1], e[2]))
        else:
            leftover_e.append(e)
    if leftover_e:
        # the PS2 archive has exactly one name record whose hash does not occur
        # in the directory (a stale/renamed entry); pair the leftovers 1:1.
        rest = [n for n in names if n not in used and namehash(n) not in
                {e[0] for e in ents}]
        for e, n in zip(leftover_e, rest + ['<unknown_%08x>' % e[0] for e in leftover_e]):
            out.append((n, e[0], e[1], e[2]))
    return out


def usage():
    print('usage: rcf.py <cement.rcf> list [substr]')
    print('       rcf.py <cement.rcf> extract <outdir> [substr]')
    print('       rcf.py <cement.rcf> hash <name>')
    sys.exit(1)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        usage()
    path, mode = sys.argv[1], sys.argv[2]
    if mode == 'hash':
        print('%08x' % namehash(sys.argv[3]))
        sys.exit(0)
    ents, names, hdr = parse(path)
    files = match(ents, names)
    if mode == 'list':
        pat = sys.argv[3].lower() if len(sys.argv) > 3 else ''
        for n, h, o, s in files:
            if pat and pat not in n.lower():
                continue
            print('%08x %10d %10d %s' % (h, o, s, n))
    elif mode == 'extract':
        out = sys.argv[3]
        pat = sys.argv[4].lower() if len(sys.argv) > 4 else ''
        f = open(path, 'rb')
        n = 0
        for name, h, o, s in files:
            if pat and pat not in name.lower():
                continue
            p = os.path.join(out, name.replace('\\', '/'))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            f.seek(o)
            open(p, 'wb').write(f.read(s))
            n += 1
        print('extracted', n)
    else:
        usage()
