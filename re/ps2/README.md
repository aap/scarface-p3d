# PS2 ELF workflow (SLES_541.82)

`/u/aap/lib/pure3d/scarface_ps2/SLES_541.82` — PS2 EE executable,
ELF32 little-endian MIPS, 7 077 736 bytes, entry `0x1a0008`.
`.symtab`/`.strtab` exist but are **empty**, and all PROGBITS sections are
unnamed, so there are no symbols at all.  What we do have is a custom RTTI
system with *demangled* class-name strings (`renderer::WorldGeoRenderable`),
which gives us class names, base-class chains and vtables — see
`classnames.txt` and `../notes/ps2.md`.

## Memory map (PT_LOAD, vaddr == paddr, file offsets)

| vaddr | end | file off | what |
|---|---|---|---|
| `0x00100000` | `0x001563d1` | `0x0001e0` | data / scratch |
| `0x00180000` | `0x00180000` | `0x0565c0` | empty |
| `0x001a0000` | `0x00245380` | `0x056600` | code (entry point) |
| `0x00245380` | `0x00246f80` | `0x0fb980` | code |
| `0x00246f80` | `0x0037a880` | `0x0fd580` | code |
| `0x0037a880` | `0x00809600` | `0x230e80` | code **and** rodata/vtables/strings |
| `0x00846280` … | | | bss-ish, no file contents |

`$gp = 0x0080f1f0` (set at `0x1a005c`: `lui a0,0x81; addiu a0,a0,-3600`).
Lots of globals are reached as `lw $xx, disp($gp)` — a naive lui/addiu
scanner will miss those.

Rough split of the big segment: code up to ~`0x0079xxxx`, then RTTI strings
(`0x0078d000`-`0x007c0000`), then vtables (`0x007c0000`-`0x00809600`).

## Disassembling

**Use `ee-objdump`.**  The EE has `lq`/`sq`/`pcpyld`/COP2 macro-mode ops that
radare2 and capstone decode as MIPS32R6/MSA nonsense (`addu.qb`, `ext`,
`aver_u.h` where the real instruction is `sq`/`lq`):

```sh
EE=/usr/local/freesce/ee/gcc/bin
$EE/ee-objdump -d --start-address=0x750d60 --stop-address=0x750df0 SLES_541.82
# whole thing (≈54 MB, 1.55 M lines, ~1 s) — worth doing once and grepping:
$EE/ee-objdump -d --start-address=0x1a0000 --stop-address=0x809600 SLES_541.82 > ps2.dis
```

objdump prints `<+0x3d64e0>` instead of a symbol name; ignore it.
`ee-objdump -h` / `ee-readelf -l` for the segment table.

radare2 works but mis-decodes the EE-only opcodes, so only use it for quick
navigation:

```sh
r2 -e bin.relocs.apply=true -e asm.arch=mips -e asm.bits=32 -e cfg.bigendian=false SLES_541.82
# or on a raw dump:
r2 -a mips -b 32 -e cfg.bigendian=false -m 0x1a0000 seg.bin
```

## Scripts here

* `ps2elf.py` — loads the PT_LOAD segments into a flat address map
  (`read/u32/cstr/off/va_of_off`), plus `scan()` which walks every code
  segment with capstone and returns

      refs[target_va]  = [insn_va,…]   lui+addiu / lui+ori / lui+lw|sw pairs
      consts[value]    = [insn_va,…]   every 32-bit constant built that way
      jals[target_va]  = [insn_va,…]

  Run it with `../venv/bin/python` (capstone lives there).  The capstone
  decode is only used to *find* references — always re-check the actual
  instructions with `ee-objdump`.

      ../venv/bin/python ps2elf.py segs
      ../venv/bin/python ps2elf.py dis 0x750d60 40
      ../venv/bin/python ps2elf.py xref 0x7b82a8

  `scan()` takes ~1 minute over the whole image; cache the result
  (`pickle`) if you iterate.

* `classnames.py` — regenerates `classnames.txt` (~80 s):

      ../venv/bin/python classnames.py > classnames.txt

## classnames.txt

One entry per demangled class-name string (1055 of them, 996 with a vtable):

```
007b8260 ti=007b82a8 vt=007f1830 renderer::WorldGeoRenderable
         bases core::IRefCount content::LoadObject pure3d::Entity renderer::Renderable
         xref 00515724
```

* first column — address of the name string
* `ti=` — the `TypeInfo { const char *name; TypeInfo **bases; }` record
* `vt=` — vtable(s) whose `[-0]` slot points at that TypeInfo; this address
  is the **vptr value** stored in the object, method *k* is at `vt + 8 + 4*k`
* `bases` — the base-class chain from the TypeInfo bases list
* `xref` — code that builds the address of the string or of the TypeInfo
  (mostly `content::LoadInventory::DynamicCaster<T>` thunks)

Full description of the RTTI/vtable layout and the interesting starting
points is in `../notes/ps2.md`.
