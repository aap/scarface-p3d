# Scarface PS2 (SLES_541.82 / cement.rcf) — RE notes

Companion to `renderables.md` (PC) and `shr_vs_scarface.md`.
Files: `/u/aap/lib/pure3d/scarface_ps2/SLES_541.82`, `.../cement.rcf` (1.44 GiB).
Tooling: `re/rcf.py`, `re/p3dwalk.py`, `re/ps2/` (see `re/ps2/README.md`).

---

## 1. RCF ("ATG CORE CEMENT LIBRARY") — the filename hash, solved   **[V]**

### 1.1 The hash

The directory is sorted by a 32-bit hash of the (backslash) path.  It is
**not** CRC32, not djb2/sdbm/FNV, and not the game's `Key32`
(`GetHash`, PC `0x6dc190`, `h = (h*65599 & 0x7fffffff) ^ tolower(c)`, `| 0x80000000`).
It is a plain `h*31` string hash with a *blind* lower-casing:

```c
u32 CementHash(const char *s)
{
    u32 h = 0;
    if (*s == '\\') s++;            /* skip ONE leading backslash */
    for (; *s; s++) {
        int c = (signed char)*s;
        if (c < 'a') c += 32;       /* NOT tolower(): '.' -> 'N', '0' -> 'P',
                                       '\\' -> '|', '_' -> DEL, … */
        h = h*31 + c;
    }
    return h;
}
```

`CementHash("packages\\z04\\Common.p3d") == 0xe88a61eb`, and that entry is
976429056/1888526 bytes — byte-identical to the repo's `Common.p3d`.

**Verified:** all 4746 PC names reproduce the PC directory hashes exactly
(0 collisions, multiset equality).  On PS2 5177/5178 match; the one odd one
out is the *name record* `art\nis\MNIS_MC2A_Whippet_Intro_Cam\static.p3d`
(hash `0x070f7b5c`) for which no directory entry exists, while the directory
has an orphan `0x070f7b31`.  The delta (43) is not a multiple of 31, so it
can only come from the last character — a stale/renamed name record left by
the packer.  `rcf.py` pairs the two leftovers 1:1.

Where it lives:

* PS2: `0x007541dc` (tiny leaf function; result is handed back through the
  FPU: `sw $a3,12($sp); lwc1 $f0,12($sp); swc1 $f0,0($a0)`).
* PC: the equivalent is in the same `core::` file layer; the RCF magic check
  is PC `sub_6e6790` / PS2 `0x00750d60` (`memcmp(hdr+0x24,"ATG CORE CEMENT LIBRARY",0x18)`
  plus `hdr[0x44]==2 && hdr[0x45]<2 && hdr[0x46]==0 && hdr[0x47]==0`).
* The library itself is only ever addressed **by hash**:
  `CementLibrary::OpenFile(hash,…)` (PC `0x6e6810`, PS2 `0x00751250`) does
  `sprintf(buf,"0x%X",hash)` and uses that as the sub-file's debug name —
  which is why no filename ever reaches the archive code.

Other Radical/`radcore` hashes in the PS2 ELF, for reference
(`radKey32` family, all `h*65599`-ish):

| addr | function |
|---|---|
| `0x0075f480` | `h = (h*65599) ^ c` (case sensitive) |
| `0x0075f4e0` | `h = (h*65599 & 0x7fffffff) ^ tolower(c)`, `\| 0x80000000` = PC `GetHash` |
| `0x0075f560` | same, char sign-extended differently |

### 1.2 File format (corrected, `re/rcf.py`)

```
0x00  char  magic[32]     "ATG CORE CEMENT LIBRARY" + zero padding
0x20  u32   version       0x01000102
0x24  u32   dirOffset     0x3c
0x28  u32   dirSize       nFiles*12
0x2c  u32   nameOffset
0x30  u32   nameSize
0x34  u32   zero
0x38  u32   nFiles

directory[nFiles], sorted ascending by hash:
      u32 hash, u32 offset, u32 size

name table (packer order, NOT hash order):
      u32 flags(0x800), u32 zero                     <- 8 byte header
      then nFiles records:
      u32 unixTimestamp, u32 flags(0x800), u32 zero, u32 nameLen,
      char name[nameLen]        (nameLen includes the trailing NUL)
      byte filler[3]            <- ALWAYS 3, records are not 4-aligned
```

|  | PC | PS2 |
|---|---|---|
| size | 1 587 035 648 | 1 541 885 440 |
| nFiles | 4746 | 5178 |
| first timestamp | 2006-08-29 | 2006-09-08 |

Content magics inside: `P3D\xff` 3677/4089, `0x00000001` 636, `ATG ` (nested
cement libraries!) 227, `\r\nfi` 190, `Rend` 14, PS2-only `MWo3` 19.
The 227 nested libraries are sound banks (`snd_*.rcf`) holding `RSD6` and
`P3D\xff` files.  **There is not a single `P3DZ` file in either archive.**

Usage:

```sh
python3 re/rcf.py <cement.rcf> list [substr]
python3 re/rcf.py <cement.rcf> extract <outdir> [substr]
python3 re/rcf.py <cement.rcf> hash 'packages\z04\Common.p3d'
```

(The pre-existing `re/pc_rcf.txt` / `re/ps2_rcf.txt` were generated before the
hash was known and still have names and offsets unmatched — regenerate them
with `list`.)

### 1.3 P3DZ / LZR

`p3dwalk.py` used to try zlib.  Wrong: `Pure3D::DATA_FILE_COMPRESSED`
(`0x5A443350` = `"P3DZ"`) is followed by `u32 totalUncompressedSize` and then
LZR blocks `{u32 compressedSize, u32 uncompressedSize, u8 data[]}`, and LZR is
Radical's own **Lempel-Ziv-Radical** (`pure3d/p3d/lzr.cpp`,
`tFileMem::SetCompressed` in `p3d/file.cpp`, `tChunkFile::tChunkFile` in
`p3d/chunkfile.cpp`).  `p3dwalk.lzr_decompress()` now implements it
source-faithfully; since no shipped file uses it, it is only self-tested.

---

## 2. Extracted assets

```
assets_ps2/packages/z04/*.p3d       220 files, 135 MiB   (PS2 cement.rcf)
assets_ps2/art/ecoprops/*.p3d        38 files, 1.7 MiB
assets/art/ecoprops/*.p3d            38 files, 2.5 MiB   (PC cement.rcf)
```

There is no `packages\Common*` in either archive — the only Common is
`packages\z04\Common.p3d` (PC 1 888 526, PS2 1 753 582 bytes).
All extracted files are plain `P3D\xff`, none compressed.

---

## 3. Chunk histogram PC vs PS2 (z04, 220 files each)

`re/hist_z04.txt` (PC) vs `re/hist_z04_ps2.txt` (PS2): 146 vs 145 distinct
chunk ids, 142 common, 85 of those with *identical* counts.

### 3.1 Only on one platform

| id | PC | PS2 | `constants/chunkids.hpp` |
|---|---|---|---|
| `0x00010006` | 18852 | – | Mesh::NORMALLIST |
| `0x0001000a` | 27132 | – | Mesh::INDEXLIST |
| `0x00010010` | 18852 | – | Mesh::PACKEDNORMALLIST |
| `0x0001001c` | 1174 | – | Mesh::MULTICOLOURLIST |
| `0x00010012` | – | 23944 | **Mesh::MEMORYIMAGEVERTEXLIST** |
| `0x00121112` | – | 3992 | (Scarface-only animation channel) |
| `0x00121114` | – | 3220 | (Scarface-only animation channel) |

### 3.2 Big count differences (same id)

| id | PC | PS2 | meaning |
|---|---|---|---|
| `0x00010000` MESH | 6961 | 5170 | |
| `0x00010003/4` BOX/SPHERE | 35749 | 30748 | |
| `0x00010005` POSITIONLIST | 28236 | **1082** | |
| `0x00010007` UVLIST | 28098 | **9** | |
| `0x00010008` COLOURLIST | 16996 | **10** | |
| `0x00010011` VERTEXSHADER | 27164 | 23954 | |
| `0x00010012` MEMIMGVERTEXLIST | – | 23944 | |
| `0x00010020` (Scarface primgroup) | 27164 | 23954 | |
| `0x00011003/4/5` shader int/float/colour param | 25808/3715/**18** | 29080/6816/**4530** | |
| `0x00121105` QUATERNION channel | 7601 | **389** | replaced by `0x121112/14` |
| `0x00023001` | 11086 | 5966 | |
| `0x00122000`/`0x00123001` | 8275/8276 | 6484/6485 | |

So the expectation holds: **PS2 geometry is one pre-swizzled
`0x00010012` memory-image vertex list per primitive group instead of the
separate position/normal/uv/colour/index lists**, `0x00010020` (Scarface's
own primgroup chunk) carries `0x00010011` + `0x00010012` on PS2 and
`0x00010011` alone on PC.  Animation quaternion channels use two
Scarface-only compressed formats on PS2.  PS2 shaders lean much harder on
float/colour params (no vertex colours in the mesh).

### 3.3 Script-object and renderer chunks are **layout-identical**   **[V]**

`0x09900190..94` and `0x08800002..0x0880000a` have the same counts on both
platforms (±0.1 %, from a handful of files with different content), and a
byte comparison of the raw chunk payloads over all 220 z04 file pairs gives:

| chunk | chunks | files where the payload stream differs (of 220) |
|---|---|---|
| `0x08800002` | 2 | 0 |
| `0x08800003` WorldGeo | 420 | 68 (different *names*: `shells_fntrck01s` vs `details_fntrck01s`) |
| `0x08800004` ZonePkg | 152 | 0 |
| `0x08800005` SFStateProp | 1009/1005 | 4 (tonymansion TS0..TS3: different prop set) |
| `0x08800007/8/9/a` | 151/44/420/1022 | 0 |
| `0x09900190/91/92/94` | 6351/644/84139/47634 | 1 (`cargoShip_01_detail_CS0.p3d`, one extra object) |

`sbeachn_01_detail.p3d` — every chunk of those ids is byte-for-byte equal
between PC and PS2, e.g. the `instanceobject` `sp_garbage_dumster`:

```
09900190 objects.ds name=sp_garbage_dumster class=instanceobject
    prop modelname = ''  rest=0000000b 64756d707374657261 0000     ("dumpstera")
    prop instanceposition rest=0003000000
      loc pos=(854.968, 368.0, -534.339)  00000000 0000012c 00000000 00000028
                                          0000000f 00000000 00000001 00000000 00000000
      loc pos=(669.834, 359.0, -281.821)  … (identical on both)
```

=> the PC work on `0x0990019x` / `0x088000xx` transfers to PS2 unchanged;
only the mesh payload below `0x00010000` needs a platform switch.

---

## 4. PS2 ELF: RTTI, class names and vtables

See `re/ps2/README.md` for the memory map and the objdump/r2 recipes, and
`re/ps2/classnames.txt` for the generated table (1055 classes, 996 vtables).

### 4.1 Layout   **[V]**

Per class, laid out contiguously in the rodata area `0x0078d000..0x007c0000`:

```
<string>    "renderer::WorldGeoRenderable\0"      16-byte aligned
<bases>     { TypeInfo *base; u32 pad; } … terminated by an all-zero entry
<TypeInfo>  { const char *name; TypeInfo **bases; }      <- 8 bytes
```

and in `0x007c0000..0x00809600`:

```
<vtable>    { TypeInfo *rtti; u32 zero; void *method[N]; }
```

An object's **vptr is the address of the `rtti` word**, so a virtual call is
`lw $t9,0($obj); lw $t9,8+4*k($t9); jalr $t9` — method *k* lives at
`vptr + 8 + 4*k`.  This matches the PC vtable numbering in `renderables.md`
(slot 0 AddRef, 1 Release, 2 GetRef, 3 dtor, …).

The class name is therefore *not* referenced from a `GetClassName` virtual;
the only code that builds these addresses is the
`content::LoadInventory::DynamicCaster<T>` thunks, e.g. for WorldGeo:

```
00515720   move $a0,$a1 ; $a1=0 ; $a2=0x007b82a8 (WorldGeoRenderable TypeInfo)
           $a3=0x00794f70 (core::IRefCount TypeInfo) ; $t0=0 ; j 0x006e9e00
```

`0x006e9e00` is the generic walk-the-bases dynamic cast.

### 4.2 The two renderables asked for   **[V]**

```
renderer::WorldGeoRenderable   string 007b8260  TypeInfo 007b82a8  vtable 007f1830
  bases  core::IRefCount  content::LoadObject  pure3d::Entity  renderer::Renderable
renderer::InstanceRenderable   string 007b85a0  TypeInfo 007b85e8  vtable 007f1bb0
  bases  core::IRefCount  content::LoadObject  pure3d::Entity  renderer::Renderable
renderer::InstanceContainer    string 007b85f0  TypeInfo 007b8640  vtable 007f1c00
  bases  core::IRefCount content::LoadObject pure3d::Entity
         pure3d::DrawableHierarchy pure3d::DrawableContainer
renderer::Renderable           string 007b75e0  TypeInfo 007b7620  vtable 007f16e0
renderer::WorldGeoLoader       string 007b8210  TypeInfo 007b8258  vtable 007f1800
```

| slot | WorldGeoRenderable `007f1830` | InstanceRenderable `007f1bb0` |
|---|---|---|
| 0 | `00392bc0` | `00392bc0` (AddRef) |
| 1 | `00392bd0` | `00392bd0` (Release) |
| 2 | `00392c80` | `00392c80` (GetRef) |
| 3 | `0050d650` | `00510230` (dtor — writes the vptr back) |
| 4 | `0068bab0` | `0068bab0` |
| 5 | `0068bac0` | `0068bac0` |
| 6 | `004eedf0` | `004eedf0` |
| 7 | `004ed250` | `004ed250` |
| 8 | `0050dc30` | `00509c50` |
| 9 | `0050dd20` | `0050fe20` |
| 10 | `0050d700` | `0050ff00` |
| 11 | `004ed240` | `004ed240` |
| 12 | `00508d40` | `00514ea0` |
| 13 | `00509d40` | `00509d40` |
| 14 | `0050dcc0` | `00509bc0` |
| 15 | `0050dd60` | `00509b20` |

Same shape as the PC classes, 16 virtuals, 9 of them inherited unchanged.
`renderer::WorldGeoLoader` vtable `007f1800`: `00392bc0 00392bd0 00392c80
0050d560 0061d730 003a5e80 0050c9d0` (7 slots — the PC `SimpleChunkHandler`
shape: AddRef/Release/GetRef/dtor/Load/GetChunkId/LoadObject).

### 4.3 Chunk-id starting points   **[V]**

Registration happens in one big function at **`0x002131ac`** (continuing to
~`0x00216400`), shape per entry:
`operator new(0x14)` → *loader ctor* → `RegisterHandler(mgr, obj, chunkid)`
(`0x0061ad60`, then `0x0061bb70`).  The ctor stores the chunk id at `obj+8`,
exactly like the PC `content::SimpleChunkHandler` in `renderables.md`.

Sites that build the three requested ids (`lui 0x0880 / ori`, `lui 0x0990 / ori`):

| id | registration site | loader ctor | loader vtable / class |
|---|---|---|---|
| `0x08800003` | `0x0021322c` | `0x0050d5f0` (const at `0x0050d620`) | `0x007f1800` **renderer::WorldGeoLoader** |
| `0x08800005` | `0x0021328c` | `0x00504a00` (const at `0x00504a30`) | `0x007f1420` **renderer::SFStatePropLoader** |
| `0x09900190` | `0x002152b8` | `0x004a2c60` (const at `0x004a2c90`) | `0x007edb00` **ScriptObjectDataLoader** |

plus the ScriptObject chunk parser at **`0x004a27f8..0x004a2a40`**, which
switches on `0x09900191` (`0x004a2930`), `0x09900192` (`0x004a293c`, also
`0x004a26f8`); `0x09900194` is read at `0x004a2330`.
`0x0880000a` is at `0x0021331c` / `0x0060abe0`.

### 4.4 Full PS2 chunk-handler table (from `0x002131ac`)

```
chunk      ctor       vtable     rtti class name
00019000   006a4810   00801180   pure3d::TextureLoader
00011000   006a9e00   008017c0   pure3d::ShaderLoader
00013000   00662fd0   007ff140   pure3d::LightLoader
00014000   0068ccb0   008007b0   pure3d::LocatorLoader
00019001   006a0810   00800ea0   pure3d::ImageLoader
00022000   0062dca0   007fe100   pure3d::TextureFontLoader
00022002   00629ca0   007fdf40   pure3d::ImageFontLoader
00017006   006b5110   00801d70   pure3d::BillboardObjectLoader
00023000   00685670   008003b0   pure3d::SkeletonLoader
00010001   00681050   008000d0   pure3d::PolySkinLoader
00123000   00679460   007ffa20   pure3d::CompositeDrawableLoader
0001001a   00669c60   007ff220   pure3d::ShadowMeshLoader
00010019   0066a2f0   007ff1f0   pure3d::ShadowSkinLoader
00121000   0066cec0   007ff530   pure3d::AnimationLoader
00121201   0067db30   007ffc80   pure3d::FrameControllerLoader
00121202   0067fe40   007fff90   pure3d::MultiControllerLoader
0001580c   0069d220   00800cc0   pure3d::ParticleSystemLoader
00021001   0067a9c0   007ffb40   pure3d::ExpressionGroupLoader
00021002   00687cd0   00800550   pure3d::VertexOffsetExpressionMixerLoader
00021000   0067a1e0   007ffba0   pure3d::ExpressionLoader
0802000d   0068aa40   008006b0   pure3d::prop::StatePropDataLoader
0802000a   0068aa40   008006b0   pure3d::prop::StatePropDataLoader
00018000   006c09e0   00802350   pure3d::frontend::ProjectLoader
0001800d   006c8250   00802590   pure3d::TextBibleLoader
0001001d   -          -          (ctor not resolved by the script)
08800001   00501460   007f1280   renderer::VehicleLoader
08800002   005056a0   007f14a0   renderer::SkyLoader
08800003   0050d5f0   007f1800   renderer::WorldGeoLoader
08800004   0050c660   007f1780   renderer::ZonePkgLoader
08800005   00504a00   007f1420   renderer::SFStatePropLoader
08800007   0050f880   007f1b30   renderer::SFLightGroupLoader
08800008   00507cc0   007f1660   renderer::ShadowLoader
0880000a   0060abb0   007fcb70   occlude::OccluderLoader
00019005   0062bef0   007fdfd0   pure3d::SpriteLoader
0100500x   0038f760   -          (font/frontend group, shared ctor)
08011000   003a7200   007dfb60   NavMeshLoader
08011005   004a4dc0   007edc60   RoadGraphLoader
08800181   004e8130   007f0e40   GameGroupLoader
08800180   004e3750   007f0920   ScriptObjectLoader
09900190   004a2c60   007edb00   ScriptObjectDataLoader
09900191   004a2b60   007edad0   GameGroupDataLoader
08800104   00465750   007ea560   ScriptFileChunkLoader
08800105   00465d60   007ea590   LoadPackageChunkLoader
08800112   00465f00   007ea5c0   LoadPackageIdentifierLoader
08800108   0051b810   007f22c0   CementLoader
```

(Names come straight from the RTTI records, so they are the *real* class
names — a nice cross-check for the PC guesses in `renderables.md`.)

### 4.5 Other useful PS2 addresses

| addr | what |
|---|---|
| `0x00750d60` | `CementLibrary::CheckHeader` (magic + version) |
| `0x00751250` | `CementLibrary::OpenFile(hash,…)`, `sprintf("0x%X")` at `0x007481a8` |
| `0x007541dc` | **cement filename hash** (section 1.1) |
| `0x0075f480/4e0/560` | radKey32 variants |
| `0x006e9e00` | generic dynamic cast over the TypeInfo base list |
| `0x0061ad60`, `0x0061bb70` | LoadManager register-handler pair |
| `0x0080f1f0` | `$gp` |
| strings | `"ATG CORE CEMENT LIBRARY"` `0x007db5a0`, `"cement.rcf"` `0x007b9098`, `"Hash Dictionary Entry Pool"` `0x007b9b40` |
