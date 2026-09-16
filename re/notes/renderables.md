# Scarface-specific chunk loaders and Renderable classes (PC)

All addresses are PC EXE VAs (unpacked image, `re/dis.sh`).
Marking: **[V]** = verified from disassembly and/or file data, **[?]** = guess / partially traced.

---

## 0. The registration function

`content::LoadManager::AddHandler(ObjectLoader*, u32 chunkID)` = `sym.AddHandler` (call target
`0x6ea060`-ish; see `re/r2flags.r2`).  The Scarface engine-layer registration lives in one big
function:

    0x00467b60 .. 0x00468461   InstallHandlers (unnamed; the pure3d-only part is a separate
                               short function at 0x00467aa0 that registers .p3d + chunk 0x19005)

Every entry is the same shape: `memory_alloc2(size)` -> ctor -> `AddHandler(obj, id)`.
All Scarface loaders are 0x14 bytes:

```
content::SimpleChunkHandler (0x14 bytes)                     [V]
  +0x00  vtable
  +0x04  refcount
  +0x08  chunkID                (GetChunkId = 0x004979a0: `return [this+8]`)
  +0x0c  0                      (ChunkFile* while loading?)
  +0x10  0
vtable slots: 0 AddRef, 1 Release, 2 GetRef, 3 dtor,
              4 content::SimpleChunkHandler::Load (0x6e9e70), 5 GetChunkId (0x4979a0),
              6 LoadObject  <-- the only per-class override
```

### Full list registered by 0x467b60 (in order)                    [V]

| addr of AddHandler | chunk id | size | ctor | class |
|---|---|---|---|---|
| 0x467bc5 | ".tga" (FileLoader) | 0x18 | ImageHandler::ctor | pure3d::TargaHandler (vt 0x7378bc) |
| 0x467c04 | ".bmp" | 0x18 | ImageHandler::ctor | pure3d::BmpHandler |
| 0x467c3b | 0x00010000 | 0x48 | 0x69c720 | pure3d::GeometryLoader |
| 0x467c72 | 0x00019000 | 0x1c | 0x69c4e0 | pure3d::TextureLoader |
| 0x467ca9 | 0x00011000 | 0x14 | 0x67ebb0 | pure3d::ShaderLoader |
| 0x467ce0 | 0x00002200 | 0x14 | 0x69bc30 | pure3d, vtable 0x76b2d4 (camera?) [?] |
| 0x467d17 | 0x00013000 | 0x14 | 0x69b6f0 | pure3d, vtable 0x76b21c (LightLoader) [?] |
| 0x467d4e | 0x00014000 | 0x14 | 0x69ba60 | pure3d::LocatorLoader |
| 0x467d85 | 0x00002380 | 0x14 | 0x69b710 | pure3d::LightGroupLoader |
| 0x467dbc | 0x00019001 | 0x1c | 0x68c960 | pure3d::ImageLoader |
| 0x467df3 | 0x00022000 | 0x14 | 0x69a9e0 | pure3d::TextureFontLoader |
| 0x467e2a | 0x00022002 | 0x14 | 0x699bf0 | pure3d::ImageFontLoader |
| 0x467e61 | 0x00017006 | 0x18 | 0x698790 | pure3d::BillboardObjectLoader |
| 0x467e98 | 0x00023000 | 0x18 | 0x668900 | pure3d::SkeletonLoader |
| 0x467ecf | 0x00010001 | 0x48 | 0x6951d0 | pure3d::PolySkinLoader |
| 0x467f06 | 0x00123000 | 0x14 | 0x694230 | pure3d::CompositeDrawableLoader |
| 0x467f3d | 0x0001001a | 0x14 | 0x680f30 | pure3d::ShadowMeshLoader |
| 0x467f74 | 0x00010019 | 0x14 | 0x681630 | pure3d::ShadowSkinLoader |
| 0x467fab | 0x00121000 | 0x24 | 0x66ef90 | pure3d::AnimationLoader |
| 0x467fe2 | 0x00121201 | 0x28 | 0x66b4d0 | pure3d::FrameControllerLoader |
| 0x468019 | 0x00121202 | 0x14 | 0x693000 | pure3d::MultiControllerLoader |
| 0x46805a | 0x0001580c | 0x3c | 0x6926d0 | pure3d::ParticleSystemLoader |
| 0x468091 | 0x00021001 | 0x14 | 0x690000 | pure3d::ExpressionGroupLoader |
| 0x4680c8 | 0x00021002 | 0x14 | 0x690860 | pure3d::VertexOffsetExpressionMixerLoader |
| 0x4680ff | 0x00021000 | 0x14 | 0x68ffe0 | pure3d::ExpressionLoader |
| 0x468136 | 0x0802000d | 0x14 | 0x68efd0 | pure3d::prop::StatePropDataLoader (other agent) |
| 0x46816d | 0x0802000a | 0x14 | 0x68efd0 | pure3d::prop::StatePropDataLoader (same class) |
| 0x4681a4 | 0x00018000 | 0x18 | 0x677090 | pure3d::frontend::ProjectLoader |
| 0x4681db | 0x0001800d | 0x14 | 0x68ec70 | pure3d frontend, vtable 0x76a988 [?] |
| 0x468212 | 0x00007000 | 0x14 | - | pure3d::IgnoreLoader |
| 0x468223 | 0x00007001 | shared | - | pure3d::IgnoreLoader |
| 0x468234 | 0x0001001d | shared | - | pure3d::IgnoreLoader |
| **0x468283** | **0x08800000** | 0x14 | **0x46ca80** | **renderer::CharacterLoader** |
| **0x4682ba** | **0x08800001** | 0x14 | **0x479cf0** | **renderer::VehicleLoader** |
| **0x4682f1** | **0x08800002** | 0x14 | **0x477660** | **renderer::SkyLoader** |
| **0x468328** | **0x08800003** | 0x14 | **0x471d80** | **renderer::WorldGeoLoader** |
| **0x46835f** | **0x08800004** | 0x14 | **0x471e50** | **renderer::ZonePkgLoader** |
| **0x468396** | **0x08800005** | 0x14 | **0x478160** | **renderer::SFStatePropLoader** |
| **0x4683cd** | **0x08800007** | 0x14 | **0x470260** | **renderer::SFLightGroupLoader** |
| **0x468404** | **0x08800008** | 0x14 | **0x4758e0** | **renderer::ShadowLoader** |
| **0x46843b** | **0x0880000a** | 0x14 | **0x461840** | **occlude::OccluderLoader** |

(0x08800006 and 0x0880000b do not exist anywhere in the image.  0x08800009 is
a *child* chunk read inline by ZonePkgLoader, not a registered handler.)

### Other 0x088xxxxx handlers, registered elsewhere                [V]

| chunk id | registered at | ctor | class |
|---|---|---|---|
| 0x08800102 | 0x43f9b3 (NISManager::ctor) | 0x4401a0 | NISPackageLoader |
| 0x08800103 | -- | -- | child of 0x8800102, read inline |
| 0x08800113 | 0x43fa1a | inline | NISStreamDataLoader |
| 0x08800114 | 0x43f9df | 0x440cd0 | NISStreamPartitionInfoLoader |
| 0x08800104 | 0x47d6b6 (FileHandlerManager::ctor) | 0x489340 | ScriptFileChunkLoader |
| 0x08800105 | 0x47d6ed | 0x5dd9d0 | LoadPackageChunkLoader |
| 0x08800112 | 0x47d724 | 0x5dd9b0 | **LoadPackageIdentifierLoader** |
| 0x08800108 | 0x47d75b | 0x47c630 | CementLoader |
| 0x08800180 | 0x47d627 | 0x445a70 | (shader-properties group) |
| 0x08800181 | 0x47d5fb | 0x441e40 | (art-template group) |
| 0x09900190/91 | 0x47d653/0x47d67f | 0x488f50/90 | ScriptObjectLoader (other agent) |
| 0x08011000 / 0x08011005 / 0xffff0000 | 0x47d577/0x47d5a3/0x47d5cf | | GameGroup / ? / MemorySectionLoader |
| 0x08800150 | 0x4eca99 (CVManager::ctor) | 0x5f7a10 | CVManager (cutscene/vehicle?) |
| 0x08800160 | 0x4ecac5 | 0x602a30 | **TrafficConfigLoader** |
| 0x08800161 | 0x4ecaf1 | 0x602ac0 | **SubzoneTrafficTriggerLoader** |
| 0x08800201 | 0x55c273 | inline | **WaterDataShallowAreaLoader** |
| 0x08800202 | 0x55c2ae | inline | (WaterData ... unnamed) |
| 0x08800203 | 0x55c2e9 | inline | **WaterDataDockLoader** |
| 0x08800200 | -- | -- | child of 0x8800201, read inline at 0x55b7cd (sub_55b710) |

Note: 0x8800160/161/200-203 are **not** an LOD system - they are traffic config
and water data that happen to live in `miami_lod.p3d` / `islands_LOD.p3d`.

---

## 1. Base classes

### pure3d::Entity                                                  [V]
`sym.Entity::ctor` (called from 0x474bbd; `./xr.sh` it for the exact VA).  Occupies
0x0c bytes (vtable + refcount + one dword at +8).  `SetName` is the no-op stub 0x438400
in the retail build, so the name is not kept anywhere - only the `GetHash` UID.

### renderer::Renderable  -- ctor 0x00474ba0, vtable 0x0073838c     [V]

```
Renderable : Entity                                         (base size 0x84)
  +0x00  vtable
  +0x04  refcount
  +0x08  (Entity: name / UID)
  +0x0c  0
  +0x10  0
  +0x14  Matrix  (4x4 float, 0x40 bytes)   -- set Identity in ctor
  +0x54  i32 typeMask       (see table below; base ctor writes -1)
  +0x58  DisplayListElement *elements_begin
  +0x5c  DisplayListElement *elements_end
  +0x60  (capacity)
  +0x64  u32 uniqueId       (from global counter at 0x008113f4, ++, wraps at -1)
  +0x68  float              (set by 0x473aa0; worldgeo: 3000.0 or 0.0; stateprop: 250.0)
  +0x6c  float = 0.0        (0x473aa0 sets 1.0)
  +0x70  0
  +0x74  float = 1.0        (0x473aa0 sets 0.0)
  +0x78  0
  +0x7c  i32 = 5
  +0x80  flag byte: bit0 (0x01) "flag1"  (cleared when a SHADER_UNTEXTURED/VERTEXFADE prim was seen)
                    bit1 (0x02) doDistFade? (Shadow ctor clears it)
                    bit2 (0x04) cleared by 0x473aa0
                    bit4 (0x10) isVisible  (SetVisible, 0x473c20)
                    bit5 (0x20) stateprop "fits in pool"
                    bit7 (0x80) stateprop flag
  +0x81  flag byte: bit0,bit1 cleared in ctor; bit1 (0x02) = isMatrixDirty (set by SetMatrix)
```

`DisplayListElement` stride is **0x30 = 48 bytes** (all the `imul 0x2aaaaaab; sar 3`
divisions).  Layout matches `renderer/renderable.h`:

```
DisplayListElement (0x30)                                    [V]
  +0x00  float drawDist[0] (min)      default 0.0
  +0x04  float drawDist[1] (max)      default 100.0
  +0x08  float drawDist[2] (fade)     default 10.0
  +0x0c  DisplayListPrimitive prim    (0x20 bytes)
  +0x2c  bool isFading                default 0
DisplayListPrimitive (0x20)                                  [V]
  +0x00  vtable (GameDrawableInfo)
  +0x04  ?
  +0x08  Renderable *parent           (SetParent = 0x5dc6a0: `[this+8] = x`)
  +0x0c  DrawableHierarchy *drawable  (GetDrawable = Get_0xC)
  +0x10..0x1b  children LinkedList
  +0x1c  flags: bit0 isVisible, bit1 isInList, bit2 flag4
         SetVisible   = 0x458ec0
         SetDrawable  = 0x458e50
         RemoveFromList/Hide = 0x458ea0
```

Renderable member functions:

| addr | name | notes |
|---|---|---|
| 0x474ba0 | `Renderable::Renderable(i32 n)` | |
| 0x474aa0 | dtor (vslot 3) | |
| 0x474ac0 | `SetNumElements(n)` | allocates n*0x30, zeroes drawDist+isFading |
| 0x473e70 | `SetElement(drawable,i,flag)` | CalcBounds (vslot 14 of drawable), SetParent, SetDrawable, drawDist={0,100,10} |
| 0x473f00 | `SetElementDrawDist(i, min, max, fade)` | writes elements[i]+0,+4,+8 |
| 0x473aa0 | `SetFadeDist(f)` | clears flag bit2, [0x6c]=1.0, [0x74]=0.0, [0x68]=f |
| 0x473c20 | `SetVisible(bool)` (vslot 8) | sets flag bit 0x10 + prim.SetVisible on every element |
| 0x4740f0 | `Display()` (vslot 10) | see below |
| 0x438400 | `SetName(const char*)` (vslot 6) | **`ret 4` - a no-op stub in the retail build.**  Renderables store no name; only the `GetHash(name)` UID registered in the LoadInventory matters. |
| 0x473b40 | `SetMatrix(Matrix*)` (vslot 12) | memcpy 0x40 to +0x14, sets flag[0x81] bit1 |
| 0x473c00 | `GetPosition(Vector*)` (vslot 13) | copies [this+0x44..0x4c] (matrix row 3) |
| 0x473c80 | `Hide()` (vslot 14) | calls 0x458ea0 (RemoveFromList) on every element |
| 0x473fc0 | `GetElementDrawable(i)` | |
| 0x473fe0 | `GetNumElements()` | |

Renderable vtable = 16 slots (0..15), RTTI ptr at vtable-4.

`Renderable::Display()` (0x4740f0) uses the camera (0x461ad0 returns the camera/view
object; `Get_0xC` + 0x10 gives the view matrix), transforms the element bound sphere,
does the min/max/fade distance test and then calls `DisplayListPrimitive::Display()`
which pushes the drawable into `Display_List`.  Layer selection is **not** done here -
the layer is baked into each `DrawablePrimitive` at load time (`SetLayer` = 0x703300,
`[prim+0x0c] = layer`).

### Renderable::typeMask (+0x54) values                            [V]

| value | class | set at |
|---|---|---|
| -1 | Renderable (base) | 0x474bd0 |
| 0x00000001 | SkyRenderable | 0x47774e |
| 0x00000002 | TraceFireRenderable | 0x4784d8 |
| 0x00000004 | ParticleEffectRenderable | 0x470df0 |
| 0x00000008 | WorldGeoRenderable, PropRenderable | 0x4714bb, 0x47141d |
| 0x00000010 | **StatePropRenderable** | 0x478250 |
| 0x00000020 | CharacterRenderable | 0x46c6a1 |
| 0x00000040 | OceanRenderable | 0x470b11 |
| 0x00000080 | WakeRenderable | 0x47b521 |
| 0x00000100 | **ShadowRenderable** | 0x474d2d |
| 0x00000200 | InstanceRenderable | 0x46f921 |
| 0x00000400 | **LightingRenderable** | 0x46fec7 / 0x46ff62 |
| 0x00000800 | RainRenderable | 0x473384 |
| 0x00001000 | MaskRenderable | 0x4704f9 |
| 0x00002000 | (unnamed, 0x46eb41 - decal?) | 0x46eb41 |
| 0x00004000 | VehicleRenderable | 0x478b6c / 0x47a1e0 |
| 0x00008000 | **ZonePkgRenderable** | 0x472740 |
| 0x00020000 | NISRenderable | 0x4705e6 |
| 0x00040000 | PlugInRenderable | 0x4712ea / 0x471381 |
| 0x00100000 | SkidmarkRenderable | 0x476b11 |

---

## 2. 0x08800005 renderer::SFStatePropLoader -> StatePropRenderable

* loader ctor `0x00478160`, vtable `0x00738690`
* **LoadObject `0x00478180`**
* renderable vtable `0x0073864c`, clone-ctor `0x00477b20`, dtor `0x00477d10`
* `size = 0x94` (148 bytes)

### Chunk layout                                                    [V]
```
0x08800005 {                        (no children; 1009 in z04, payload 16..88 bytes)
    pstring  name;                  // e.g. "iBenchA"
    pstring  dataName;              // usually identical; key into inventory
}
```
(p3d string = u8 len + len chars, padded to a multiple of 4.  `dlen-12` is always
a multiple of 8 here because both strings are padded.)

### Class layout                                                    [V]
```
StatePropRenderable : Renderable                (0x94)
  +0x00..0x83  Renderable            typeMask (+0x54) = 0x10
  +0x84  prop::StatePropData *data   (AddRef'd)
  +0x88  byte  = 1
  +0x8c  i32   sizeClass             0 = small pool, 1 = medium pool
  +0x90  byte  = 0
  +0x91  byte  = 1                   (set at the very end of LoadObject)
  flags[0x80]: |= 0x80 always; bit5 (0x20) = "fits in a state-prop pool"
```
Overridden vslots vs Renderable: 3 (dtor, 0x477d10), 9 (0x478070).
`Display` (vslot 10) is **not** overridden - plain `Renderable::Display`.

### LoadObject                                                      [V]
1. `GetString(name)`; `*pUID = GetHash(name, 0)`   (`GetHash` j-thunk at 0x6dc230)
2. `new StatePropRenderable` (alloc 0x94, `Renderable::Renderable(0)`), typeMask=0x10
3. `GetString(dataName)`; `inventory->Find<pure3d::prop::StatePropData>(GetHash(dataName))`
   (DynamicCaster vtable `___7__DynamicCaster_VStatePropData_prop_pure3d__...`);
   AddRef / Release-old / store in `+0x84`
4. if data != nil:
   * `0x68ee50(data)`                      - StatePropData "prepare"
   * `SetNumElements(1)`                   (0x474ac0)
   * `drawable = this->InstantiateDrawable(data, 0, 0, 1)` (0x477a00: allocates a
     0x130-byte `DrawableContainer`-ish object via 0x69dd30, then vslot 14 = CalcBounds)
   * `SetElement(drawable, 0, 0)`          (0x473e70)
   * `SetElementDrawDist(0, 0.0f, 1000.0f, 200.0f)`  (0x473f00, consts 0x447a0000 / 0x43480000)
   * `StatePropManager(g_0x8111d0->[0x1c])->Register(this)` (0x46b830) - walks the
     element-0 drawable, and for every prim whose class id == `g[0x811348]` and whose
     `vslot25()==8` calls `SetLayer(3)` (night-light layer)
5. `flags[0x80] |= 0x80`; `SetFadeDist(250.0f)` (0x473aa0)
6. `this->ClassifySize(renderable, &bFits, &this->sizeClass)` = **0x00477e60**:
   * snapshots the allocator stats (0x43c9f0), builds a throw-away clone
     `new StatePropRenderable(renderable)` (0x477b20), snapshots again, releases it;
     `delta` = bytes the clone needed.
   * `delta > g[0x8114f4] (=0x1770, 6000)` -> too big: destroy the drawable, `*bFits = 0`
     (also does two `__RTDynamicCast` probes for `ParticleSystem` / `BillboardObject`)
   * `delta <= g[0x8114f0] (=0x7d0, 2000)` -> `sizeClass = 0`, `*bFits = 1`
   * otherwise                              -> `sizeClass = 1`, `*bFits = 1`
   The two thresholds are set at 0x468578 / 0x4685a3 alongside the memory pools named
   `"smallstateprop"` (0x7d0 x 0x2a) and `"mediumstateprop"` (0x1770 x 0xd) - i.e. state
   props are instanced out of two fixed-size pools and this classifies which one to use.
7. `+0x91 = 1`; `*pObject = this`; flags[0x80] bit5 = bFits.

So a state-prop chunk is only a *template registration*: it binds a name to a
`StatePropData` and precomputes the pool class.  The actual placements come from the
instance-object data (0x0990019x / 0x0802000d) handled by the other agent.

---

## 3. 0x08800004 renderer::ZonePkgLoader -> ZonePkgRenderable (+ 0x08800009)

* loader ctor `0x00471e50`, vtable `0x00738298`
* **LoadObject `0x00472680`**
* renderable vtable `0x00738254`, dtor `0x00471e70`, SetVisible override `0x00471bc0`
* `size = 0x90` (144 bytes)

### Chunk layout                                                    [V]
```
0x08800004 {                        (exactly one per shell/detail file, 152 in z04)
    pstring  name;                  // "devilscay_01_shell", "havana_01_shell", ...
    u32      numNames;
    pstring  worldGeoName[numNames];    // <-- READ AND THROWN AWAY by the loader
    // children:
    0x08800009 entry[...];          // one per world geo actually referenced
}

0x08800009 {                        (420 in z04; tail is always 7 dwords)
    pstring  worldGeoName;
    u32      numFloats;             // always 6 in the shipped data
    float    f[numFloats];
      f[0] = drawDist min           (usually 0)
      f[1] = drawDist max           (10000, 500, 700, ...)
      f[2] = drawDist fade          (5, 15, 10, ...)   [only if numFloats >= 3]
      f[3] = unused                 (always 0)
      f[4] = otherPosition.x        [only if numFloats >= 5]
      f[5] = -otherPosition.z       [only if numFloats >= 6]  (multiplied by -1.0)
}
```

### Class layout                                                    [V]
```
ZonePkgRenderable : Renderable                  (0x90)
  +0x00..0x83  Renderable            typeMask (+0x54) = 0x8000
  +0x84  WorldGeoRenderable **worldGeos_begin
  +0x88  WorldGeoRenderable **worldGeos_end
  +0x8c  (spare / capacity)
```
Filled with `ArrayThing::Init4` (0x468d60): allocates `n*4`, sets `[0]=data`,
`[4]=data+n*4`, fills with a default value (0 here).

### LoadObject                                                      [V]
1. `GetString(name)`; `*pUID = GetHash(name)`
2. `new ZonePkgRenderable` (0x90), `Renderable(0)`, typeMask = 0x8000, `+0x84 = +0x88 = 0`
3. `SetName(name)` (vslot 6 -> 0x438400, a no-op in retail)
4. read `u32 numNames`; `ArrayThing::Init4(&this->+0x84, numNames, &nil)`
5. loop `numNames` times: `GetString(tmp)` - **discarded**; the array is filled from the
   child chunks instead (the name list is vestigial/for tooling)
6. while `ChunksRemaining()`: `BeginChunk()`;
   * if `GetCurrentID() == 0x08800009`:
     - `GetString(wgName)`; `wg = inventory->Find<WorldGeoRenderable>(GetHash(wgName))`
     - if found: `array[i] = wg` (AddRef / Release-old), `i += 4`
     - `ApplyZoneEntry(chunkFile, wg)` = **0x00471c10** (see below)
   * `EndChunk()`
7. `*pObject = this`

### 0x00471c10 `ApplyZoneEntry(ChunkFile*, WorldGeoRenderable*)`     [V]
```
n = ReadU32();  f[i] = ReadF32() for i in 0..n-1;
if (wg == nil) return;
fade = (f[1] - f[0]) * 0.2f;            // const 0x7495ec = 0.2
if (n >= 3) fade = f[2];
ox = 0; oz = 0; haveOther = false;
if (n >= 5) { ox = f[4]; haveOther = true; }
if (n >= 6) { oz = f[5] * -1.0f; }      // const 0x7495e8 = -1.0
if (haveOther && ox == 0.0f && oz == 0.0f) haveOther = false;
wg->SetElementDrawDist(0, f[0], f[1], fade);          // 0x473f00
if (haveOther) {
    wg->flags90 |= 1;                   // useOtherPosition
    wg->otherPosition = { ox, 0.0f, oz };
}
```
So `otherPosition` is a *2-D horizontal reference point* (y always 0) used instead of the
renderable's own matrix position for the LOD distance test; z is stored negated in the file.

The zone package is therefore the per-file streaming record: "these are the world geos
in this package, and this is each one's draw distance + distance reference point".

---

## 4. 0x08800003 renderer::WorldGeoLoader -> WorldGeoRenderable  (cross-check of `renderer/worldgeo.cpp`)

* loader ctor `0x00471d80`, vtable `0x00738230`
* **LoadObject `0x00471ec0`**
* renderable ctor `0x00471490`, vtable `0x007381e4`,
  dtor `0x00471d60`, `SetVisible` `0x00471570`, `Display` `0x00471640`,
  vslot14 `0x00471510`, vslot15 `0x004714e0`

### Chunk layout                                                    [V]
```
0x08800003 { pstring name; pstring compositeName; u32 type; }
type histogram over z04:  0 x331, 1 x16, 3 x3, 5 x47, 6 x12, 7 x9, 8 x2
```

### Class layout                                                    [V]  (0xa0 = 160)
```
WorldGeoRenderable : Renderable
  +0x00..0x83  Renderable            typeMask (+0x54) = 8
  +0x84  float otherPosition.x
  +0x88  float otherPosition.y       (always 0)
  +0x8c  float otherPosition.z
  +0x90  flags byte  (ctor: `and 0xe0` i.e. clears bits 0..4)
          0x01 useOtherPosition   (set by ZonePkg 0x8800009)
          0x02 isDetails          ("details_" or "cbvlitdecals_")
          0x04 isSkyline          ("skyline_")
          0x08 drawFirst          ("shells_" or "underwater_")
          0x10 isLowLOD           ("low_LOD_")
  +0x94  DisplayListPrimitive *primitives   (0x20 each)
  +0x98  i32 *poseIDs
  +0x9c  i32 numPrimitives
```

### Verdict on `renderer/worldgeo.cpp`

**Correct:**
* chunk layout, class field order, the `type in {3,5,6,7,8}` split, all the shader-type ->
  layer numbers in both branches (see the table below - every case matches), the
  `details_/cbvlitdecals_/skyline_/shells_/underwater_/low_LOD_` prefix tests and their
  flag bits, the "skip the primitives[] array when isLowLOD" rule, `SetNumElements(1)` +
  `SetElement(composite,0,false)` at the end, and `if(flag1) flag1=false`.
* The `else { SetLayer(33); flag1=true; }` fallback in the special-type branch really is
  there (0x4723cf `jne 0x4723dc`), it is not an artefact.

**Wrong / missing:**
1. `// TODO: unknown -> 8` in the ctor is `Renderable::typeMask` (+0x54) - see the table
   in section 1.  WorldGeo = 8.
2. `otherPosition` is never filled by the world-geo loader; it comes from the
   **ZonePkg 0x08800009 child chunk** (0x471c10).  Same for the real draw distances -
   `SetElement` leaves {0,100,10} and 0x471c10 overwrites them.
3. Missing after `SetElement`:
   ```
   if (composite->classId == g[0x8113a8])            // 0x4725d6
       g[0x8111d0]->[0x1c]->0x46b5b0(worldgeo);      // register with a manager
   SetFadeDist( g[0x8111d0]->0x41c110() ? 3000.0f : 0.0f );   // 0x473aa0
   ```
4. `poseIDs[i]` is `(u16)` read from `primList[i]+4` (an 8-byte {ptr,u16 id} entry) -
   aap reads `->id`, fine, but note it is 16-bit.
5. The special-type branch iterates `DrawableContainer` elements **directly** and only
   calls `GetShader()` where needed; aap's version calls `GetBlendMode()/GetIsLit()` on a
   shader that may be nil for type 3/8 - guard it.
6. `*pUID` is `GetHash(name)` of the *first* string (0x472652), same value as
   `GetUID()`, so that is equivalent.

### Layer numbers (SetLayer = 0x703300, writes `[prim+0x0c]`)        [V]

Normal branch (type not in {3,5,6,7,8}); `mask = prim->vslot7()`, `sh = prim->vslot9()`,
`sh->[0x14]` = blend mode, `sh->[0x15]` = shader type, `sh->[0x16]` bit1 = isLit,
bit3 = alphaTest; `IsXXXBlendMode` = 0x458ae0 -> blend in {1,2,3,7}.

| condition | layer |
|---|---|
| mask == 2, 4 or 8 | 1 |
| sh type 0x0c / 0x0d (UNTEXTURED / VERTEXFADE) | 33, sets flag1 |
| 0x03 SPECULAR | 5 |
| 0x04 SPECULAR_MCBV | 6 |
| 0x08 FOAM | 29 |
| 0x0b NIGHTLIGHT | 3 |
| 0x0a SHADOWDECAL | 37 |
| 0x07 DECAL, blend 2/3 | 21 |
| 0x07 DECAL, other blend | 22 |
| 0x01 ENV | 26 |
| 0x00 SIMPLE, lit, alphaTest && blend==0 | 13 |
| 0x00 SIMPLE, lit, !blended | 11 |
| 0x00 SIMPLE, lit, blend 2/3 | 14 |
| 0x00 SIMPLE, lit, other blend | 12 |
| 0x00 SIMPLE, unlit, !blended | 7 |
| 0x00 SIMPLE, unlit, blend 2/3 | 10 |
| 0x00 SIMPLE, unlit, other blend | 8 |
| 0x09 CBVLIT, alphaTest && blend==0 | 17 |
| 0x09 CBVLIT, !blended | 15 |
| 0x09 CBVLIT, blend 2/3 | 18 |
| 0x09 CBVLIT, other blend | 16 |
| 0x06 LAYERED, lit | 20 |
| 0x06 LAYERED, unlit | 19 |
| anything else | *no SetLayer call* |

Special branch (type 3 LOW_LOD, 5 UNDERWATER, 6 CBVLITDECALS, 7 INTERIORFLOORS, 8 CARDSNIGHT):

| condition | layer |
|---|---|
| type 5 | 34 |
| type 6, blend 2/3 | 23 |
| type 6, blend 1 | 24 |
| type 6, other | - |
| type 7, blend==1 && !lit | 25 |
| type 3 or 8, sh type 0x0c/0x0d | 33, flag1 |
| type 8, otherwise | 4 |
| type 3, otherwise | 33, flag1 |

### WorldGeoRenderable::Display (0x00471640)                        [V for the dispatch]
```
al = flags90;
if (al & 0x10)                                   // isLowLOD
    g_byte[0x7c0a11] ? Renderable::Display() : Hide();
else if (!(al & 0x0e))                           // plain shells/other
    g_byte[0x7c0a10] ? Renderable::Display() : Hide();
else if (!g_byte[0x7c0a11]) Hide();
else  <custom path>                              // details/skyline/drawFirst
```
`0x7c0a10` / `0x7c0a11` are two global render-enable toggles (world geo / details+LOD).
The custom path re-uses the per-primitive `primitives[]` array (+0x94) and `poseIDs`
(+0x98) so that individual sub-drawables of a composite can be culled/faded separately -
this is what the extra array exists for.  `SetVisible` override (0x471570) simply calls
the base and then `primitives[i].SetVisible(v)` for all `numPrimitives`.

---

## 5. 0x08800007 renderer::SFLightGroupLoader -> LightingRenderable

* loader ctor `0x00470260`, vtable `0x00737ef0`
* **LoadObject `0x00470280`**
* renderable ctor `0x0046ff10`, vtable `0x00737eac`, dtor `0x00470240`
* `size = 0xa4` (164 bytes)

### Chunk layout                                                    [V]
```
0x08800007 {
    pstring name;
    pstring lightGroupName;         // key for pure3d::LightGroup (chunk 0x13000/0x2380)
    u32     kind;                   // 0..4 in z04 (0,1 -> also has controllers)
    u32     numControllers;
    pstring controllerName[numControllers];   // pure3d::LightAnimationController
}
observed (kind,numControllers): (0,4)x2  (1,4)x2  (2,0)x30  (3,0)x116  (4,0)x2
```

### Class layout                                                    [V]
```
LightingRenderable : Renderable                 (0xa4)
  +0x00..0x83  Renderable            typeMask (+0x54) = 0x400
  +0x84  pure3d::LightGroup *group   (AddRef'd)
  +0x88  u32 kind
  +0x8c  i32 numControllers
  +0x90  LightAnimationController *ctrl[5]     (0x90 .. 0xa0, AddRef'd)
```
`Display` (vslot 10) is `nullsub` - a light group draws nothing, it only registers with
the light manager.

### LoadObject / ctor                                               [V]
1. read name, `*pUID = GetHash(name)`
2. read lightGroupName, read `kind`, `Find<pure3d::LightGroup>(GetHash(lightGroupName))`
3. read `numControllers`; loop: read name, `Find<pure3d::LightAnimationController>`, store
   in a local array
4. `new LightingRenderable(group, ctrls, numControllers, kind)` (0x46ff10):
   `Renderable(0)`; typeMask = 0x400; store/AddRef everything;
   then `g_lightManager(0x8111cc)->0x45fe10(group, kind)`;
   if `kind == 0` also `g[0x8113a0] ... 0x460380(...)`.

(The "0x13000 lights" the task mentions are the plain pure3d `Light` chunks referenced
from the LightGroup - registered separately at 0x467d17.)

---

## 6. 0x08800008 renderer::ShadowLoader -> ShadowRenderable

* loader ctor `0x004758e0`, vtable `0x0073841c`
* **LoadObject `0x00475930`**
* renderable ctor `0x00474cf0`, vtable `0x007383d4`, dtor `0x00474ea0`,
  vslot9 `0x00474cd0`, `Display` `0x004751c0`
* `size = 0xbc` (188 bytes)

### Chunk layout                                                    [V]
```
0x08800008 {
    pstring name;                   // "DevilsCay_02_shadow", "NPC_shadow", "bacinari_shadow"
    pstring compositeName;          // CompositeDrawable
    u32     isBuildingShadow;       // -> byte +0x84 (bool); 43x 1, 2x 0 in z04
    u32     b;                      // -> byte +0x86 (bool); always 0
    float   f0;                     // -> +0x88 ; always 0.0
    float   f1;                     // -> +0x8c ; 200.0 (world) / 15.0 (NPC) / 100.0 (car)
}
```

### Class layout                                                    [V]
```
ShadowRenderable : Renderable                   (0xbc)
  +0x00..0x83  Renderable            typeMask (+0x54) = 0x100, flags[0x80] &= ~0x02
  +0x84  bool  isBuildingShadow
  +0x85  bool
  +0x86  bool
  +0x88  float                      (from chunk)
  +0x8c  float                      (from chunk)
  +0x90  0
  +0x94  0
  +0x98  0
  +0x9c  float = 1.0
  +0xa0..+0xac  0
  +0xb0  float = 1.0   (scale x)    overwritten from geometry bounds
  +0xb4  float = 1.0   (scale y)
  +0xb8  bool = 0
```

### LoadObject                                                      [V]
1. name, `new ShadowRenderable` (0xbc), `SetName`
2. compositeName -> `Find<CompositeDrawable>`
3. 4 u32 reads -> +0x84 (bool), +0x86 (bool), +0x88, +0x8c
4. `if (isBuildingShadow) { flags[0x80] &= ~1; SetNumElements(1); } else SetNumElements(2);`
5. walks the composite's `DrawableContainer` elements; `prim->vslot7() == 0x20` selects the
   shadow prims, `SetLayer(2)` at 0x475b88 - **shadows use Display_List layer 2**.
6. For non-building shadows it looks up a `pure3d::Geometry` by a fixed name
   (`0x738450` / `0x738438` - two built-in names) and derives +0xb0/+0xb4 (x/y half-size)
   from its vertex bounds; `+0x85` distinguishes the two cases, `+0xb8 = 1` for the
   bounds-derived one.
7. `SetElement(geom->vslot5(1, !isBuildingShadow), ...)`; `SetElementDrawDist(1, ..., f*c)`
   with `c` at 0x764544.
8. `*pUID = GetHash(name)`

`ShadowRenderable::Display` = 0x004751c0; the helper at 0x474ec0 also calls
`SetLayer(2)` (0x4750ae) when re-binding a drawable.

---

## 7. 0x0880000a occlude::OccluderLoader -> occlude::Occluder

* loader ctor `0x00461840`, vtable `0x007377fc`
* **LoadObject `0x00461650`**
* Occluder vtable `0x007377c8`; **derives from `pure3d::Entity`, NOT from Renderable**
* `size = 0x70` (112 bytes)

### Chunk layout                                                    [V]  (always 44 bytes)
```
0x0880000a {                        // 1022 in z04, 31 files, max 32 per shell file
    float centre[3];                // world position
    float normal[3];                // unit normal, e.g. (0,0,-1) / (-1,0,-0.0126)
    u32   kind;                     // 0,1,2  (1 -> sets both flags, 2 -> only +0x28)
    float unused;                   // never read
    float halfExtent[3];
}
```
The chunk has **no name**; the loader makes one up:
`sprintf(name, "occluderobject%d", ++g[0x8110a0])` and `*pUID = GetHash(name)`.

### Class layout                                                    [V]
```
occlude::Occluder : pure3d::Entity              (0x70)
  +0x00  vtable / +0x04 refcount / +0x08 Entity
  +0x0c  float bbMin.x = centre.x - halfExtent.x*c
  +0x10  float bbMin.y   (immediately overwritten with -1.0f !)
  +0x14  float bbMin.z = centre.z - halfExtent.z*c
  +0x18  float bbMax.x = centre.x + halfExtent.x*c
  +0x1c  float bbMax.y
  +0x20  float bbMax.z
  +0x24  float yaw      = atan2(normal.x, normal.z)   (x87 fpatan)
  +0x28  byte  flagA    (kind == 1 || kind == 2)
  +0x29  byte  flagB    (kind == 1)
  +0x68  byte = 0
  +0x6c  i32  = -1
```
`c` is the constant at `0x007644ec` (scale applied to halfExtent).
`0x00461080` is called at the end (this=occluder) - registers it with the occlusion system.
Occluders are pure CPU-side culling volumes; they never touch Display_List.

---

## 8. 0x08800002 renderer::SkyLoader -> SkyRenderable

* loader ctor `0x00477660`, vtable `0x00738628`
* **LoadObject `0x00477680`**, renderable vtable `0x007385e4`, dtor `0x004774e0`,
  `Display` `0x00477450`, vslot9 `0x00477500`
* `size = 0x98`

### Chunk layout                                                    [V]
```
0x08800002 { pstring name; pstring compositeName; }
Common.p3d has exactly two: ("sky","sky") and ("rainy_skybox","rainy_skybox").
```
### Class layout                                                    [V]
```
SkyRenderable : Renderable                      (0x98)
  typeMask (+0x54) = 1
  +0x84  0
  +0x8c  0
  +0x90  i32 = 0x3e8 (1000)
  +0x94  0
```
### Layers                                                          [V]
For every `DrawablePrimitive` of the composite:
* `dynamic_cast<BillboardQuadGroup>` succeeds -> if `prim->[0x82] != 0` assign
  `prim->[0x90] = runningIndex++`; then **`SetLayer(28)`**
* otherwise -> **`SetLayer(38)`** or **`SetLayer(39)`** depending on a bool derived
  from the chunk name (`[esp+0x13]`).
Afterwards every PrimGroup gets `[pg+0x0d] = 0`.

---

## 9. 0x08800000 renderer::CharacterLoader / 0x08800001 renderer::VehicleLoader

* Character: ctor `0x0046ca80`, vtable `0x00737b88`, **LoadObject `0x0046cad0`**,
  renderable vtable `0x00737b3c`, typeMask 0x20, dtor `0x0046ca60`,
  `Display` `0x0046c620`, vslot9 `0x0046c8f0`, vslot13 `0x0046c5f0`
* Vehicle: ctor `0x00479cf0`, vtable `0x00738810`, **LoadObject `0x0047a5b0`**,
  typeMask 0x4000

### Chunk layout (identical shape)                                  [V]
```
0x08800000 / 0x08800001 {
    pstring name;
    pstring compositeName;
    pstring lodCompositeName;
    pstring shaderName[8];          // fixed count 8; unused slots are stored as the
                                    // 4-byte string {len=3, 00 00 00} i.e. empty
}
```
Verified against `bacinari.p3d` (4 real shader names + 4 blank) and
`tony_only_bacinari.p3d` (1 real + 7 blank) - 3 + 8 = 11 strings, no counts in the file.

### Class layout                                                    [V]
```
VehicleRenderable : Renderable
  +0x84 .. +0xa0  pure3d::Shader *shaders[8]   (AddRef'd; nil where the name was empty)
  +0xa4  i32 numShadersFound
  (then a 0xd8-byte sub-object allocated at 0x47a7fa when a composite was found)

CharacterRenderable : Renderable
  +0x84  (composite / drawable)
  +0x88 .. +0xa4  pure3d::Shader *shaders[8]
```
Loop setup: vehicle `mov dword [esp+0x24], 8` @0x47a73f; character
`mov dword [esp+0x14], 8` @0x46cd0a.  Each name is hashed with `GetHash` and looked up
through `___7__DynamicCaster_VShader_pure3d__...`.

---

## 10. Other 0x088xxxxx chunks that appear in the map packages

### 0x08800112 - LoadPackageIdentifier                              [V]
```
0x08800112 { pstring packageName; }     // "havana_01_shell", "Common", ...
```
Exactly one per .p3d (220 in z04).  Handler = `LoadPackageIdentifierLoader`
(ctor 0x5dd9b0, vtable 0x007570dc), registered at 0x47d724.  It is the package's
self-identification record used by the streaming/package manager.

### 0x08800104 - ScriptFileChunk                                    [V]
```
0x08800104 { pstring csoPath;           // "scriptc/missions/z04/<zone>/zone_setup.cso"
             pstring zoneName;
             ... compiled TorqueScript (.cso) byte code ... }
```
Handler `ScriptFileChunkLoader` (ctor 0x489340, vtable 0x00739ab8) registered at
0x47d6b6 inside `FileHandlerManager::ctor`.  89 in z04, sizes 116 .. 10618 bytes.

### 0x08800102 / 0x08800103 - NIS package                           [V]
```
0x08800102 { pstring nisName; pstring nisName2; pstring "init"; pstring "end";
             pstring nisName3; <floats/ints>; pstring "<nis>/stream"; u32; u32;
             0x08800103 child... }
0x08800103 { pstring "<nis>/static.p3d"; u32; pstring "<nis>/static.p3d"; }
```
Handler `NISPackageLoader` (ctor 0x4401a0, vtable 0x007364ec) registered from
`NISManager::ctor` at 0x43f9b3.  These are cut-scene ("non-interactive sequence")
package descriptors, not renderables.

### 0x08800160 / 0x08800161 - traffic                               [V]
`TrafficConfigLoader` (0x602a30, vtable 0x0075cd9c) and `SubzoneTrafficTriggerLoader`
(0x602ac0, vtable 0x0075cdbc), both registered from `CVManager::ctor` (0x4ecac5 /
0x4ecaf1).  Only in `islands_LOD.p3d` / `miami_lod.p3d`.

### 0x08800200 .. 0x08800203 - water data                           [V]
`WaterDataShallowAreaLoader` (0x8800201, vtable 0x00746c3c),
unnamed loader (0x8800202), `WaterDataDockLoader` (0x8800203, vtable 0x00746c7c),
all registered by `sub_55c1a0` (0x55c273 / 0x55c2ae / 0x55c2e9).
0x08800200 is a *child* of 0x08800201, read inline in `sub_55b710` at 0x55b7cd.
Again only in the two `*_LOD.p3d` files - despite the file names these are not LOD
chunks.

---

## 11. Display_List layer numbers seen so far                       [V]

| layer | used by | where |
|---|---|---|
| 1 | world geo prims with mask 2/4/8 | 0x472293 |
| 2 | shadows | 0x4750ae, 0x475b88 |
| 3 | NIGHTLIGHT shaders; state-prop night lights | 0x472109, 0x46b895 |
| 4 | CARDSNIGHT world geo | 0x4723d1 |
| 5,6 | SPECULAR / SPECULAR_MCBV | 0x4720cd, 0x4720e1 |
| 7,8,10,11,12,13,14 | SIMPLE (lit/unlit x blend) | 0x4721xx |
| 15..18 | CBVLIT | 0x47223x |
| 19,20 | LAYERED | 0x47227b/f |
| 21,22 | DECAL | 0x472144/b |
| 23,24 | CBVLITDECALS world geo | 0x472381, 0x472376 |
| 25 | INTERIORFLOORS | 0x4723ac |
| 26 | ENV | 0x47215f |
| 28 | sky billboard quads | 0x4778da |
| 29 | FOAM | 0x4720f5 |
| 33 | UNTEXTURED / VERTEXFADE | 0x472283, 0x4723dc |
| 34 | UNDERWATER world geo | 0x47234d |
| 36 | MaskRenderable | 0x470518 |
| 37 | SHADOWDECAL | 0x47211d |
| 38, 39 | sky (non-billboard) | 0x4778e4 / 0x4778e8 |

(`NUM_DISPLAY_LISTS` = 84 in `renderer/display_list.h`; the rest are used by
particles, HUD, vehicles, rain, water, etc. from 0x59xxxx-0x6axxxx call sites listed
by `./xr.sh 703300`.)

---

## 12. Handy addresses

### `GetHash` = 0x006dc190 -- the inventory key function                [V]

**This is not what `core::MakeKey`/`MakeKeyCI` in the repo compute.**  Thunks:
`0x006dc230` = `j_GetHash` (called as `GetHash(str, 0)`), `0x006683b0` = `GetHash(str)`
with seed 0.

```c
u32 GetHash(const char *str, u32 seed)
{
    if (str == nil || *str == 0)
        return seed;                       // note: NOT or'ed with 0x80000000
    u32 key = seed & 0x7fffffff;
    for (char c = *str; c; c = *++str) {
        key = (((key << 10) + key) << 6) - key;   // == key * 65599
        key &= 0x7fffffff;                        // <-- masked EVERY iteration
        if (c < 'a') c += 0x20;                   // signed compare; 'A'->'a', but also
                                                  // '0'->'P', '/'->'O', '_'->0x7f ...
        key ^= (u32)(i32)c;
    }
    return key | 0x80000000;
}
```
Differences from `core.cpp`: the `& 0x7fffffff` after each multiply and the final
`| 0x80000000`.  `MakeKeyCI`'s character folding is right; the masking is missing, so
every key aap computes is wrong above bit 30.  All chunk loaders key the
`LoadInventory` with `GetHash(name, 0)` and `*pUID` is that same value.

### Other handy addresses

```
File::GetData(dst,count,size,swap)   sym.File::GetData     ; ChunkFile's File* is at [cf+0x284]
ChunkFile::ChunksRemaining/Begin/End/GetCurrentID      sym.ChunkFile::*
LoadInventory::Find                  vtable slot 4 ([inv_vtbl+0x10])(caster, key)
DrawablePrimitive::SetLayer          0x00703300   ([prim+0x0c] = layer)
DrawablePrimitive::GetSomeMask       vslot 7  ([prim_vtbl+0x1c])
DrawablePrimitive::GetShader         vslot 9  ([prim_vtbl+0x24])
Shader fields                        +0x14 blendMode, +0x15 shaderType, +0x16 bit1 isLit / bit3 alphaTest
Shader::IsXXXBlendMode               0x00458ae0  (blend in {1,2,3,7})
DrawableContainer                    +0x44/+0x48 element array (16-byte stride, element[0] = DrawablePrimitive*)
CompositeDrawable                    +0x44 -> primitive list; list +0x24/+0x28 (8-byte stride {ptr, u16 id})
ArrayThing::Init4(n, &default)       0x00468d60   ([0]=begin, [4]=end)
memory pools (stateprop etc.)        0x00468490  ("smallstateprop" 0x7d0x0x2a, "mediumstateprop" 0x1770x0xd,
                                                  "buildingshadow" 0x32000, "vehicleblock" 0x19c8x9,
                                                  "decalblock" 0x578, "instanceblock" 0x1f4x0x67)
thresholds                           g[0x8114f0] = 0x7d0, g[0x8114f4] = 0x1770
render toggles used by WorldGeo::Display   g_byte[0x7c0a10], g_byte[0x7c0a11]
```

### Helper scripts added by this pass
* `re/vt.py <addr> [n]` - dump n dwords at addr with symbol resolution (vtable dumper)
* `re/chunklen.py <chunkid> <files...>` - payload-length histogram + child ids for one chunk id
