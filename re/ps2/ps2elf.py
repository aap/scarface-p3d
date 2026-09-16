"""Minimal PS2 (EE, MIPS32 little-endian) ELF helper for SLES_541.82.

Loads all PT_LOAD segments into a flat address map and provides
  - read/u32/string access by virtual address
  - a full scan of lui/addiu|ori|lw|sw pairs so we can resolve 32-bit
    constants and data references ("who references address X")
  - a scan for immediate constants (chunk ids etc.)

Needs capstone (re/venv/bin/python has it).  Usage examples at the bottom
and in re/ps2/README.md.
"""
import struct, sys, collections

ELF = '/u/aap/lib/pure3d/scarface_ps2/SLES_541.82'

class Image:
    def __init__(self, path=ELF):
        d = open(path, 'rb').read()
        self.data = d
        phoff, = struct.unpack_from('<I', d, 0x1c)
        phentsize, phnum = struct.unpack_from('<HH', d, 0x2a)
        self.segs = []          # (vaddr, filesz, bytes)
        for i in range(phnum):
            o = phoff + i*phentsize
            typ, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<8I', d, o)
            if typ != 1 or filesz == 0:
                continue
            self.segs.append((vaddr, filesz, off))
        self.segs.sort()

    def seg_of(self, va):
        for vaddr, filesz, off in self.segs:
            if vaddr <= va < vaddr + filesz:
                return vaddr, filesz, off
        return None

    def off(self, va):
        s = self.seg_of(va)
        return None if s is None else s[2] + (va - s[0])

    def va_of_off(self, o):
        for vaddr, filesz, off in self.segs:
            if off <= o < off + filesz:
                return vaddr + (o - off)
        return None

    def read(self, va, n):
        o = self.off(va)
        return None if o is None else self.data[o:o+n]

    def u32(self, va):
        b = self.read(va, 4)
        return None if b is None or len(b) < 4 else struct.unpack('<I', b)[0]

    def cstr(self, va, maxlen=256):
        o = self.off(va)
        if o is None: return None
        e = self.data.find(b'\0', o, o+maxlen)
        return self.data[o:e if e >= 0 else o+maxlen]

    def code_segs(self):
        """executable-ish segments (everything but the first data one)"""
        return [s for s in self.segs if s[0] >= 0x1a0000]


def _disasm_all(md, buf, vaddr):
    """capstone's disasm() stops at the first undecodable word (EE VU macro ops,
    data in .text); restart 4 bytes later so we cover the whole segment."""
    p = 0
    n = len(buf) & ~3
    while p < n:
        last = p
        for i in md.disasm(buf[p:n], vaddr + p):
            yield i
            last = i.address - vaddr + i.size
        p = last + 4 if last == p else last


def scan(img, segs=None):
    """Disassemble linearly and return
         refs   : {target_va: [insn_va,...]}   from lui+addiu / lui+ori / lui+lw|sw
         consts : {value:     [insn_va,...]}   full 32-bit values built by lui+ori/addiu
         jals   : {target_va: [insn_va,...]}
    """
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 | capstone.CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    refs = collections.defaultdict(list)
    consts = collections.defaultdict(list)
    jals = collections.defaultdict(list)
    for vaddr, filesz, off in (segs or img.code_segs()):
        buf = img.data[off:off+filesz]
        hi = {}                                   # reg -> (upper16, insn_va)
        for i in _disasm_all(md, buf, vaddr):
            m = i.mnemonic
            if m == 'lui':
                rd = i.operands[0].reg
                hi[rd] = (i.operands[1].imm & 0xffff, i.address)
            elif m in ('addiu', 'ori', 'daddiu'):
                ops = i.operands
                if len(ops) == 3 and ops[1].type == capstone.mips.MIPS_OP_REG and ops[1].reg in hi:
                    up, ha = hi[ops[1].reg]
                    lo = ops[2].imm
                    if m == 'ori':
                        val = (up << 16) | (lo & 0xffff)
                    else:
                        val = (up << 16) + (lo if lo < 0x8000 else lo - 0x10000)
                    val &= 0xffffffff
                    consts[val].append(ha)
                    if img.seg_of(val):
                        refs[val].append(ha)
                    if ops[0].reg != ops[1].reg:
                        hi.pop(ops[1].reg, None)
                    hi.pop(ops[0].reg, None)
                else:
                    hi.pop(ops[0].reg, None)
            elif m in ('lw', 'sw', 'lb', 'lbu', 'lh', 'lhu', 'sb', 'sh', 'lq', 'sq', 'lwc1', 'swc1', 'ld', 'sd'):
                ops = i.operands
                if len(ops) == 2 and ops[1].type == capstone.mips.MIPS_OP_MEM and ops[1].mem.base in hi:
                    up, ha = hi[ops[1].mem.base]
                    d = ops[1].mem.disp
                    val = ((up << 16) + d) & 0xffffffff
                    consts[val].append(ha)
                    if img.seg_of(val):
                        refs[val].append(ha)
                if ops[0].type == capstone.mips.MIPS_OP_REG:
                    hi.pop(ops[0].reg, None)
            elif m in ('jal', 'j'):
                if i.operands and i.operands[0].type == capstone.mips.MIPS_OP_IMM:
                    jals[i.operands[0].imm].append(i.address)
            elif m == 'jr':
                hi.clear()
            else:
                for o in i.operands:
                    if o.type == capstone.mips.MIPS_OP_REG:
                        hi.pop(o.reg, None)
    return refs, consts, jals


def disasm(img, va, n=40):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 | capstone.CS_MODE_LITTLE_ENDIAN)
    out = []
    for i in _disasm_all(md, img.read(va, n*4), va):
        out.append('%08x  %-8s %s' % (i.address, i.mnemonic, i.op_str))
    return '\n'.join(out)


def func_start(img, va, back=0x800):
    """walk back to the previous 'jr $ra' + delay slot, i.e. the likely function start"""
    a = va
    lo = max(va - back, 0x1a0000)
    prev = None
    while a > lo:
        a -= 4
        w = img.u32(a)
        if w == 0x03e00008:      # jr $ra
            return a + 8
    return None


if __name__ == '__main__':
    img = Image()
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'segs'
    if cmd == 'segs':
        for v, n, o in img.segs:
            print('%08x-%08x  file %08x' % (v, v+n, o))
    elif cmd == 'dis':
        print(disasm(img, int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 40))
    elif cmd == 'xref':
        refs, consts, jals = scan(img)
        t = int(sys.argv[2], 16)
        print('data refs:', ' '.join('%08x' % a for a in refs.get(t, [])))
        print('consts   :', ' '.join('%08x' % a for a in consts.get(t, [])))
        print('calls    :', ' '.join('%08x' % a for a in jals.get(t, [])))
