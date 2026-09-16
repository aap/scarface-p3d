# Instance objects / eco props: from p3d bytes to pixels

Status legend: **[V]** verified from disassembly and/or real file bytes, **[G]** guess / not
fully confirmed, **[?]** open question.

Everything below is the **PC** build. Addresses are in `re/scarface_unpacked.bin`
(flat image, base 0x401000). Use `re/dis.sh`, and for exact `[esp+N]` offsets prefer

    r2 -q -m 0x401000 -b 32 -a x86 -i re/r2flags.r2 \
       -c "e scr.color=0;e asm.comments=false;e asm.var=false; s 0xADDR; pd 200" \
       re/scarface_unpacked.bin

(r2's `pdf` variable names are misleading here — several functions reuse their own
argument slots as scratch.)

See also the sibling notes in this directory: `renderables.md` (the 0x088xxxxx Renderable
loaders, including 0x08800005 StatePropRenderable, and the Display_List layer numbers),
`ps2.md`, `shr_vs_scarface.md`.

---

## TL;DR — minimum to get trees on screen

1. Walk the chunk tree for `0x09900190`/`0x09900191`; whenever `className == "instanceobject"`,
   read the `modelname` property (`0x09900192`, type 0) and then every `0x09900194` child of
   every `instanceposition` property (type 2). `modelname` always comes first. (§1)
2. Decode each 48-byte record: pos = (f0, f1/100, f2) — note the **/100 on Y**;
   rot = (w0, w1, w2) degrees; scale = w6 ? `(w3|w4<<6|w5<<12)/1000` : `(w3,w4,w5)/20`. (§1.4, §3.2)
3. Build `M = Scale · Rot(-rotX, -rotY, +rotZ) · Translate(pos)` with the exact entries in §3.4.
4. Draw the `pure3d::Geometry` named `<modelname>InstanceShape` (case-insensitive — the
   engine's name hash lowercases). 426 of the 455 placed models are present in the extracted
   assets. (§6)

Everything else (StatePropData, CompositeDrawable, the state machine, the pddi instancing
extension, wind/sway, the tint) is optional polish.

---

## 0. The pipeline in one picture

```
   objects.ds script objects inside <zone>_detail.p3d / _shell.p3d
        chunk 0x09900190  ScriptObject definition  (script, name, class="instanceobject")
          chunk 0x09900192  property "modelname"        = "dumpstera"
          chunk 0x09900192  property "instanceposition" (type 2)
            chunk 0x09900194  preprocessed location record (3 float + 9 u32)
                    |
                    |  ScriptObjectDataLoader (0x00488f50..0x00489250)
                    v
   InstanceObject (game object, gameobject/render/instanceobject.cpp)
                    |  InstanceObject::AddPreProcessedLocation  0x005e9410
                    v
   StatePropManager::AddPreProcessedLocation  0x004af620
        stores a 24-byte SLocationData into  StatePropManager+0x2cd0[n]
        and bumps the location count of the "instance render set" it belongs to
                    |
                    |  per frame: cull, pick which locations are close enough
                    v
   StatePropManager 0x004b0f10  -> renderer::AddInstance 0x00463db0
                    v
   renderer::InstanceRenderable (one per prop model, holds "InstanceShape<model>" Geometry)
     -> renderer::InstanceContainer -> renderer::InstancePrimitive
        -> pure3d::pddiExtInstancing::WindyMatrixPacketList  (one matrix packet per instance)
                    v
   InstancePrimitive::Display 0x0046f110: frustum-cull every packet, set wind (sway),
   then one hardware-instanced draw call through the pddi instancing extension.
```

Important consequence for the reimplementation: **the instanced rendering does NOT use the
StatePropData CompositeDrawable.** It uses a single `pure3d::Geometry` whose UID is
`MakeKey32("InstanceShape", modelUID)` (plus optional `LODShape`/`LODShape2` variants).
The CompositeDrawable/state machinery is only used once a prop is "woken up" into a real
`StatePropObject` (damage, physics). **[V]** (see 0x004b0250 below)

---

## 1. Chunk formats

### 1.1 Conventions

Chunk header (as in aap's `chunkfile.cpp`): `u32 id; u32 dataLen; u32 totalLen;` — `dataLen`
includes the 12 header bytes, children follow the data up to `totalLen`.

Pure3D **pstring**: `u8 len; char data[len];` where `1+len` is a multiple of 4 and the string
is NUL terminated inside the padding. So `"dumpstera"` is stored as
`0b 64 75 6d 70 73 74 65 72 61 00 00` (len byte 0x0b = 9 chars + 1 NUL + 2 pad). Reader:
`ChunkFile::GetPString(ChunkFile* cf, char* dst, int maxLen)` at **0x004c1150** **[V]**.

### 1.2 0x09900190 / 0x09900191 — ScriptObject

Both ids carry the same payload. They have **two different registered loaders** **[V]**:

* `0x09900190` → **`ScriptObjectDataLoader`** (ctor 0x00488f50, vtable 0x00739a78,
  registered at 0x0047d64d). A *leaf* object: its body (0x00488db0) only accepts
  0x09900192 property children.
* `0x09900191` → **`GameGroupDataLoader`** (ctor 0x00488f90, vtable 0x00739a98,
  `LoadObject` 0x00489250, registered at 0x0047d679). A *group*: its body (0x00488fb0)
  reads an extra `u32` after the three strings, special-cases `className == "gamegroup"`,
  and recurses into 0x09900190 / 0x09900191 children as well as 0x09900192.

In `z04`: 6350x 0x09900190 (parents: top-level and 0x09900191), 644x 0x09900191 (parents:
top-level and 0x09900191), 84135x 0x09900192, 47631x 0x09900194. **[V]**

```
0x09900190 / 0x09900191:
    pstring  scriptName      // e.g. "objects.ds", "objects_sound.ds"
    pstring  objectName      // e.g. "sp_garbage_dumster"
    pstring  className       // e.g. "instanceobject", "gamegroup", "positionobject"
  children: 0x09900190, 0x09900191, 0x09900192
```

Real bytes (`sbeachn_01_detail.p3d`): **[V]**
```
0b "objects.ds\0"  13 "sp_garbage_dumster\0"  0f "instanceobject\0"
```

Note: `p3dblock.py` currently 4-aligns between objectName and className. That is wrong —
there is no extra alignment, the pstring padding already does it. (The two happen to agree
in most files.)

Class histogram over `assets/packages/z04/*.p3d`: instanceobject 2470, locatablesoundobject
772, worldambiencespawnobject 526, gamegroup 496, positionobject 403, ... **[V]**

### 1.3 0x09900192 — property

```
0x09900192:
    pstring  name           // "modelname", "instanceposition", "priority", ...
    u32      type           // 0 = plain string value; 2 = preprocessed location
    pstring  value          // the value for type 0; EMPTY pstring (byte 0x03 + 3 NULs) for type 2
  children: 0x09900194 (only when type == 2)
```
**[V]** — over all z04 packages only `type` 0 (36504x) and 2 (47631x) occur, and every type-2
property is named `instanceposition` and has exactly one 0x09900194 child.

Properties actually used by `instanceobject` across all of `z04` **[V]**:

| property | count | notes |
|----------|-------|-------|
| `instanceposition` | 47631 | type 2, one 0x09900194 child each |
| `modelname` | 2263 | the eco-prop model |
| `attractname` | 203 | AI attract point (browsehigh, browselow, smoking, peeing, parking, trashbin) |
| `minculldistance` / `maxculldistance` / `fadeoutdist` | 33 each | `0` / `200` / `30` — from a base class, not `InstanceObject` |
| `staticcollision` | 10 | `true` |
| `ident`, `scriptclass`, `wakeupradius`, `nocollision`, `stateproptemplate` | ≤5 each | |

195 of the 2470 `instanceobject`s have **no** `modelname` — they only carry `attractname`
and are invisible AI attract points. `InstanceObject::ApplyChanges` skips template creation
when `mModelName == 0`, so they never render. **[V]**

`si_instanceobject.cpp` in the leak also registers `interior`, `doNotCull`,
`attractEnabled`, `noSway`, `priority`, but none of those appear in the z04 data.

### 1.4 0x09900194 — preprocessed instance location

48 data bytes, always. **[V]**

```
0x09900194:
    float posX            // world units
    float posY            // 1/100 world units (!), always integral in the data — see below
    float posZ            // world units
    u32   rotX            // degrees 0..359
    u32   rotY            // degrees 0..359
    u32   rotZ            // degrees 0..359
    u32   scale0          // 0..63
    u32   scale1          // 0..63
    u32   scale2          // 0..63
    u32   uniformScale    // 0 or 1
    u32   tint            // 0..15
    u32   stateIndex      // 0 = none, else 1-based index into the state-name table (see 3.4)
```

Scale encoding (this is the subtle bit) **[V]** — verified both from the loader/decoder code
and statistically over all 47631 records:

* `uniformScale != 0`: the three 6-bit fields together form one 18-bit number
  `v = scale0 | scale1<<6 | scale2<<12`, and the actual **uniform** scale is `v / 1000.0f`.
  The overwhelmingly common record is `(40, 15, 0)` = `v = 1000` = scale **1.0**.
  Observed values: 1000 (20387x), 1200, 1024, 2000, 1500, 800, 750, ... max 12484 (=12.484).
* `uniformScale == 0`: each 6-bit field is an independent component,
  `scale.x = scale0/20.0f`, `scale.y = scale1/20.0f`, `scale.z = scale2/20.0f`
  (so 20 = 1.0, max 63 = 3.15). Typical vegetation records look like `(20,24,20)`,
  `(24,60,24)`, `(20,40,20)` — equal X/Z, different Y.

`tint` is a 4-bit brightness: `alpha = (tint == 0) ? 1.0f : tint * (1/15.0f)` **[V]**
(constant 0x0073c990 = 0.0666667).

`posY` is converted to a **signed short in 1/100 world units** when stored, so the effective
range is ±327.67 and the precision is 1 cm. The file values are already in those units
(the preprocessed loader does `(short)(float)posY` with **no** scale factor), i.e. a file
value of 368.0 means y = 3.68 world units. The script-string path (`AddLocation`) instead
does `(short)(y * 100.0f)`, so script y is in world units. **[V]**

`stateIndex` → `SLocationData` bits 18..21 of dword +0x10; it is the initial StateProp state
(see §3.3). Observed in the data: 0 = none (47597x), 1 = `idle` (20x), 8 = `open` (13x),
2 = `final` (1x). **[V]**

Beware: when the record is packed into `SLocationData`, **rotX and rotZ lose their low bit**
(they are stored in 2-degree units). Only rotY keeps full 1-degree precision. In the shipped
data rotX/rotZ are 0 for ~60% of all locations anyway.

`re/instloc.py <file.p3d> [modelname-substring]` dumps all placements of a package already
decoded (position, degrees, scale, tint) — handy for eyeballing / for a first import.

---

## 2. The script-object loader

`ScriptObjectDataLoader::LoadObject` (vtable slot 6) = **0x00488f70** → **0x00488db0**
(leaf, 0x09900190). `GameGroupDataLoader::LoadObject` = **0x00489250** → **0x00488fb0**
(group, 0x09900191). The group version is shown below because it is the superset; the leaf
version is the same minus the `extra` u32, the `gamegroup` branch and the recursion.
Both end by calling `group->vtbl[0x74](obj)` to register the finished object. **[V]**

```c
// 0x00488fb0   cdecl, 4 args
void LoadScriptObject(int unused1, ScriptObject** outKey, ChunkFile* cf, ScriptObject* parent)
{
    LoadManager* lm = *(LoadManager**)0x0082a2c0;
    if (lm->field_4 == 0) return;                       // loading disabled

    char nameBuf[0x80], classBuf[0x80];
    ChunkFile::GetPString(cf, nameBuf, 0x80);           // scriptName
    ScriptGroup* group;
    if (parent == NULL) {
        group = (_strcmpi(nameBuf, "objects_sound.ds") == 0) ? lm->field_14 : lm->field_10;
    }
    ChunkFile::GetPString(cf, nameBuf,  0x80);          // objectName (same buffer!)
    ChunkFile::GetPString(cf, classBuf, 0x80);          // className
    u32 extra; File::GetData(&extra, 1, 4, 1);          // u32 after the 3 strings

    if (outKey) *outKey = (ScriptObject*)GetHash(nameBuf, 0);   // 0x006dc230 = j_GetHash

    ScriptObject* obj;
    if (_strcmpi(classBuf, "gamegroup") == 0) {
        obj = (GameGroup*)Alloc(0x40);
        GameGroup::GameGroup(obj, nameBuf, 9, extra, 2);        // 0x00441a40
    } else {
        obj = ScriptObject_CreateByClassName(classBuf);          // 0x004956e0
    }
    ScriptObject::SetProperty(obj, "name", 0, nameBuf);          // 0x004453d0

    while (ChunkFile::ChunksRemaining(cf)) {
        ChunkFile::BeginChunk(cf);
        switch (ChunkFile::GetCurrentID(cf)) {
        case 0x09900190: Load0x190Handler(cf, obj);       break;   // 0x00488db0
        case 0x09900191: LoadScriptObject(0,0, cf, obj);  break;   // recursion
        case 0x09900192: {
            char key[0x80], val[0x80]; u32 type;
            ChunkFile::GetPString(cf, key, 0x80);
            File::GetData(&type, 1, 4, 1);
            ChunkFile::GetPString(cf, val, 0x80);
            if (type == 0) {
                ScriptObject::SetProperty(obj, key, 0, val);
            } else if (ChunkFile::ChunksRemaining(cf)) {
                ChunkFile::BeginChunk(cf);
                LoadPreProcessed(cf, obj, type);           // 0x00488b60
                ChunkFile::EndChunk(cf);
            }
            break;
        }
        }
        ChunkFile::EndChunk(cf);
    }
    obj->vtbl[0x1c]();            // ApplyChanges(true) / PostLoad
    group->vtbl[0x74](obj);       // register the object with the group
}
```

`LoadPreProcessed` = **0x00488b60** — only handles `type == 2` and `id == 0x09900194`:

```c
// 0x00488b60   cdecl(ChunkFile* cf, InstanceObject* obj, int propType)
void LoadPreProcessed(ChunkFile* cf, InstanceObject* obj, int propType)
{
    if (propType != 2) return;
    if (ChunkFile::GetCurrentID(cf) != 0x09900194) return;

    SLocationData loc;  SLocationData_ctor(&loc);      // 0x004af050
    loc.flagBit25 = 1;                                 // byte[0x0b] |= 2

    u32 t;
    File::GetData(&t,1,4,1);  loc.posX  = t;                       // raw float bits
    File::GetData(&t,1,4,1);  loc.posY16 = (short)(float)t;        // fld/ftol, NO *100
    File::GetData(&t,1,4,1);  loc.posZ  = t;
    File::GetData(&t,1,4,1);  loc.rotX2 = t >> 1;      // bits  9..16 of +0x08
    File::GetData(&t,1,4,1);  loc.rotY  = t;           // bits  0.. 8 of +0x08 (9 bits)
    File::GetData(&t,1,4,1);  loc.rotZ2 = t >> 1;      // bits 17..24 of +0x08
    File::GetData(&t,1,4,1);  loc.s0    = t;           // bits  0.. 5 of +0x10
    File::GetData(&t,1,4,1);  loc.s1    = t;           // bits  6..11 of +0x10
    File::GetData(&t,1,4,1);  loc.s2    = t;           // bits 12..17 of +0x10
    File::GetData(&t,1,4,1);  loc.uniformScale = (t != 0);         // bit 29 of +0x08
    File::GetData(&t,1,4,1);  loc.tint  = t;           // bits 22..25 of +0x10
    File::GetData(&t,1,4,1);  loc.unk4  = t;           // bits 18..21 of +0x10

    obj->AddPreProcessedLocation(loc);                 // 0x005e9410
}
```

(the `>>1` is not written literally — the compiler emits `(v<<8) & 0x1FE00` which is exactly
`((v>>1)&0xFF) << 9`. Same for rotZ.) **[V]**

---

## 3. `StatePropManager::SLocationData` — the 24-byte instance record

This is the struct that both the file loader and the script path produce, and it is *also*
the persistent per-instance record: `AddPreProcessedLocation` memcpy's all 24 bytes straight
into the manager's array. **[V]**

```c
struct SLocationData            // 24 (0x18) bytes
{
    /*0x00*/ float  posX;                  // world units
    /*0x04*/ float  posZ;                  // world units   (note: Z, not Y!)
    /*0x08*/ u32    A;
    /*0x0c*/ u32    B;                     // ctor initialises to 0x0003FFFF
    /*0x10*/ u32    C;
    /*0x14*/ s16    posY100;               // Y position in 1/100 world units
    /*0x16*/ u16    lightSlot;             // dynamic-light index, valid when C bit 28
};
```

**A (+0x08)** **[V]**

| bits | meaning |
|------|---------|
| 0..8 | rotY, degrees 0..359 |
| 9..16 | rotX / 2 (2-degree units) |
| 17..24 | rotZ / 2 (2-degree units) |
| 25 | **enabled / placed** — both loaders set it; cleared by `SetLocationEnabled` (0x004aff50), and `InstanceUpdate` then removes the render instance |
| 26 | has a collision object (slot in B bits 25..31) |
| 27 | currently has an instance inside a `renderer::InstanceRenderable` |
| 28 | a real `StatePropObject` has been instantiated for this location |
| 29 | **uniform-scale flag** |

**B (+0x0c)** **[V]** — four packed sub-indices; init `0x3FFFF` = slot 0x3ff + handle 0xff

| bits | meaning |
|------|---------|
| 0..9 | `SInstanceSlot` index (into StatePropManager+0x10); 0x3ff = none |
| 10..17 | `InstanceLocationHandle` — the render-set index; 255 = none |
| 18..24 | active-renderable slot (into StatePropManager+0x714) |
| 25..31 | collision-object slot (into StatePropManager+0x2ce0) |

**C (+0x10)** **[V]**

| bits | meaning |
|------|---------|
| 0..5 / 6..11 / 12..17 | scale x/y/z, or (0..17 together) the uniform scale |
| 18..21 | **initial state index** (§3.3) |
| 22..25 | tint, 0..15 |
| 26 | attract point registered |
| 27 | stateprop/lighting registered **[G]** |
| 28 | has a dynamic light; its index is in +0x16 |
| 30 | **dead** — the render set has no InstanceObject; the location is skipped forever |
| 31 | **use the template's "final"/destroyed mesh** → selects `lod[1]` |

### 3.1 Encoders (what the exporter/scripts do) — for cross-checking

* `SLocationData::ctor` **0x004af050**: zeroes pos/A, `B = 0x3FFFF`, and fills all three
  6-bit scale fields with the global `*(int*)0x007c66ac` (= 20, i.e. scale 1.0 in the
  non-uniform encoding), zeroes bits 18..25 and the two u16s. **[V]**
* `SLocationData::Set(Vector* pos, Vector* rot, Vector* scale, bool flag)` **0x004afb50**:
  * `posX = pos->x; posZ = pos->z; posY100 = (short)(pos->y * 100.0f)`  (0x0072fa40 = 100.0)
  * `SetRotation(rot)` (see below)
  * if `|s.x-s.y| < 1e-5 && |s.y-s.z| < 1e-5` → set bit29, `v = (int)(clamp(s.x, 0.001f, 65.0f) * 1000.0f)`,
    store `v & 0x3FFFF` across the three 6-bit fields.
    (constants: 0x0073cb48 = 0.001, 0x007644e8 = 65.0, 0x0073ba90 = 1000.0)
  * else the three fields get `(int)((s.i + 0.05f*0.5f) * 20.0f)` each, i.e. `round(s.i*20)`.
    (0x007c6698 = 0.05, 0x0073ba80 = 20.0)
* `SLocationData::SetRotation(Vector* rot)` **0x004af100**:
  `r = fmod(r, 360); if (r < 0) r += 360;` then
  `bits0..8 = (int)rotY`, `bits9..16 = (u8)(rotX*0.5f)`, `bits17..24 = (u8)(rotZ*0.5f)`,
  preserving bits 25..31. **[V]**
* tint is set by `StatePropManager::AddLocation` **0x004b0130**:
  `t = (int)(tintFloat * 14.999999f + 0.5f); if (t > 15) t = 15;` → bits 22..25. **[V]**

### 3.2 Decoders (what you must implement)

```c
// 0x004aef10 : SLocationData::GetScale(const SLocationData* loc, Vector* out)   [V]
void GetScale(const SLocationData* loc, Vector* out)
{
    if (loc->A & (1u<<29)) {                       // uniform
        int v = loc->C & 0x3FFFF;
        out->x = out->y = out->z = v * 0.001f;     // 0x007c66a8
    } else {
        out->x = ( loc->C        & 0x3f) * 0.05f;  // 0x007c6698
        out->y = ((loc->C >>  6) & 0x3f) * 0.05f;
        out->z = ((loc->C >> 12) & 0x3f) * 0.05f;
    }
}

// 0x004af0b0 : SLocationData::GetPosition(Vector* out)                          [V]
void GetPosition(const SLocationData* loc, Vector* out)
{
    out->x = loc->posX;
    out->y = (float)loc->posY100 * 0.01f;          // 0x007c66a0
    out->z = loc->posZ;
}

// rotation, in degrees
rotX = ((loc->A >>  9) & 0xff) * 2;
rotY =  (loc->A        & 0x1ff);
rotZ = ((loc->A >> 17) & 0xff) * 2;

// tint -> alpha  (0x004b0f10)
tint  = (loc->C >> 22) & 0xf;
alpha = (tint == 0) ? 1.0f : tint * (1.0f/15.0f);
```

### 3.3 The state-name table (bits 18..21) **[V]**

`StatePropManager::SetLocationName(SLocationData* loc, u32 nameKey)` = **0x004af5e0** is what
turns the trailing `%s` of an `instanceposition` string into those 4 bits:

```c
void SetLocationName(SLocationData* loc, u32 nameKey)
{
    u32* p = (u32*)0x0081d238;  int idx = 1;           // table of 11 state-name hashes
    while (*p != nameKey) { ++p; ++idx; if (p >= (u32*)0x0081d264) return; }
    loc->C = (loc->C & ~0x3c0000) | ((idx << 18) & 0x3c0000);
}
```

and readers do `idx = (C >> 18) & 0xf; if (idx) key = *(u32*)(0x0081d234 + idx*4);`
(0x004b313e in `sub_4b3080`, 0x004b352e in `sub_4b31a0`).

The table is filled at 0x0071e340 with `MakeKey32` of, in order:

| idx | address | state name |
|-----|---------|------------|
| 1 | 0x0081d238 | `idle` |
| 2 | 0x0081d23c | `final` |
| 3 | 0x0081d240 | `damage_1` |
| 4 | 0x0081d244 | `damage_2` |
| 5 | 0x0081d248 | `damage_3` |
| 6 | 0x0081d24c | `damage_4` |
| 7 | 0x0081d250 | `explosion` |
| 8 | 0x0081d254 | `open` |
| 9 | 0x0081d258 | `opening` |
| 10 | 0x0081d25c | `closed` |
| 11 | 0x0081d260 | `closing` |

(0x0081d264, immediately after the table, is an unrelated counter used by
`StatePropManager::EraseLocations` 0x004af300 — it doubles as the loop's end address.)

These names are exactly the `0x0802000e` StateData names, so the field says which state the
instance starts in. A separate table at 0x0081d22c..0x0081d234 holds
`attract_point_a/b/c` keys.

### 3.4 The matrix

`BuildInstanceMatrix(SLocationData* loc, Matrix* m, bool applyScale)` = **0x004b0000** **[V]**

```c
void BuildInstanceMatrix(const SLocationData* loc, Matrix* m, bool applyScale)
{
    Matrix::Identity(m);
    float rx = ((loc->A >>  9) & 0xff) * 2.0f;
    float ry =  (loc->A        & 0x1ff);
    float rz = ((loc->A >> 17) & 0xff) * 2.0f;
    Matrix::SetRotation(m, -rx*DEG2RAD, -ry*DEG2RAD, +rz*DEG2RAD);   // 0x00660a90
    //  note the signs: X and Y are NEGATED, Z is not.
    //  DEG2RAD = 0.017453292 (0x0072f3c8 = +, 0x00737e08 = -)

    if (applyScale) {
        Vector s; GetScale(loc, &s);
        // in aap's math::Matrix (float e[16], GetX/GetY/GetZ/GetPosition at 0/4/8/12):
        *m->GetX() *= s.x;          // e[0..2]
        *m->GetY() *= s.y;          // e[4..6]
        *m->GetZ() *= s.z;          // e[8..10]
    }
    m->e[12] = loc->posX;
    m->e[13] = loc->posY100 * 0.01f;
    m->e[14] = loc->posZ;
    // e[15] stays 1.0 from Identity
}
```

The layout matches `math::Matrix` in `gmath.h` exactly (row-major, rows are the basis
vectors, row 3 is the translation), so the offsets can be used verbatim.

Caveat: the *instanced* path never calls this function — `StatePropManager` hands raw
position / Euler-degrees / scale / tint to the renderer, which composes the matrix itself in
`InstancePrimitive::AddInstance` (§5.3) using the identical convention. `BuildInstanceMatrix`
is used for attract points and for `CreateStatePropFromInstance` when the location has no
render instance yet. Either way the formula above is what you want to implement. **[V]**

`Matrix::SetRotation(m, a, b, c)` = **0x00660a90**; with
`c1=cos a, s1=sin a, c2=cos b, s2=sin b, c3=cos c, s3=sin c` it fills (row-major,
row-vector convention like the rest of Pure3D): **[V]**

```
e[0] = c3*c2                e[1] = s3*c2                e[2]  = -s2
e[4] = c3*s1*s2 - s3*c1     e[5] = s2*s3*s1 + c3*c1     e[6]  = c2*s1
e[8] = c3*c1*s2 + s3*s1     e[9] = s3*c1*s2 - c3*s1     e[10] = c2*c1
```
Translation is untouched by `SetRotation`. All nine entries verified. **[V]**

The exact same rotation convention (`-x, -y, +z`) is used by
`renderer::InstancePrimitive::AddInstance` at 0x0046f4b0, so it is definitely the intended
one. **[V]**

---

## 4. `StatePropManager`

Singleton: `the_StatePropManager()` = **0x0042a180**, pointer in global **0x00806b44**,
object size **0x2da0**, ctor **0x004b1220**. **[V]**

### 4.1 Members

| offset | type | meaning |
|--------|------|---------|
| +0x0000 / +0x0004 | ptr / ptr | begin/end of the sorted `SActiveRenderable[]` (16-byte elements, `bsearch` with `PtFuncCompare` 0x0046a550) |
| +0x0010 | `SInstanceSlot*` | 895 × 12 bytes (alloc at 0x004b151e, element ctor 0x004aee20) |
| +0x0014 | u16[0x37f] | free list of instance-slot indices |
| +0x0712 | u16 | free-list cursor for instance slots |
| +0x0714 | `RenderableHandle*[0x67]` | 103 live instance renderables |
| +0x08b0 | u8[0x67] | free list of renderable slots |
| +0x0918 | u16 | free-list cursor for renderable slots |
| +0x091c / +0x0924 | ptr / int | a *different* 0x14-stride array (template streaming requests) and its count |
| +0x092c | int | number of registered instance objects |
| +0x0930 | `SInstanceRenderSet[255]` | **the render sets** |
| +0x1d1c…+0x1d24 | ptr×3 | woken-up-StateProp array, 12-byte elements, cap 100 (cmp 0x004afab0) |
| +0x1d2c | u8[2000][2] | **mPendingAdds** — pairs `{u8 renderSetHandle, u8 locationSubIndex}` |
| +0x2ccc | int | mPendingAdds count (max 0x7d0 = 2000) |
| +0x2cd0 | `SLocationData*` | mLocations |
| +0x2cd4 | int | mNumberLocations (max 0x1055 = 4181) |
| +0x2cd8 | int | time-slice cursor into mLocations |
| +0x2cdc / +0x2d44 | float | 40.0f — extra collision radius |
| +0x2ce0 | ptr | 80 × 8-byte collision records `{ptr, s16 setHandle, s16 locIndex}` |
| +0x2d4c | int | live render instances (hard cap 0x37e = 894, warn above 850 = [0x007c668c]) |
| +0x2d50 | int | live renderables (warn above 97 = [0x007c6690]) |
| +0x2d54 | int | renderables created |
| +0x2d58 / +0x2d5c | int | instances dropped this pass / previous pass |
| +0x2d68…+0x2d70 | Vector | last camera position |
| +0x2d74 | u16 | last allocated render-set index |
| +0x2d7c / +0x2d80 | ptr | StatePropPreSimSet / StatePropPostSimSet |
| +0x2d84 | bool | `mRequiresCullUpdate` (`RequiresCullUpdateSet` is inlined) |
| +0x2d88 | float | global prop draw-distance scale, init 1.0f, pushed to the renderer via 0x00464040 |
| +0x2d8c | u8 | "over budget" warning flag |
| +0x2d90 | int | locations to process this frame, reset to 0x15e = **350** |
| +0x2d94 / +0x2d98 | int | pending-erase range used by `EraseLocations` |

`c_InvalidRenderSetIndex` = the global **0x0073baa8 = 255**. `InstanceObject::s_ActiveStatepropTemplates`
= 0x0082ae00 (begin) / 0x0082ae04 (end), 20-byte elements
`{Key32 key, InventoryId, StatePropTemplate*, StatePropData*, float mCullRadius}`. **[V]**

```c
struct SInstanceRenderSet            // 0x14 bytes, 255 of them at +0x930
{
    /*0x00*/ InstanceObject*    mpInstanceObject;   // NULL = free
    /*0x04*/ float              mCullDistanceSq;
    /*0x08*/ StatePropTemplate* mpTemplate;
    /*0x0c*/ u16                mFirstLocation;     // index into mLocations; 0xffff = free
    /*0x0e*/ u16                mNumberLocations;
    /*0x10*/ u16                mPendingAddStart;   // index into mPendingAdds; 0xffff = none
};

struct SActiveRenderable             // 16 bytes, sorted by pTemplate
{
    /*0x00*/ StatePropTemplate* pTemplate;
    /*0x04*/ struct { s16 renderableSlot;  // index into +0x714; 0xffff = none
                      s16 numInstances;
                      u16 slotListHead;    // 0x3ff = empty
                    } lod[2];              // [0] normal mesh, [1] the "final"/destroyed mesh
};

struct SInstanceSlot                 // 12 bytes, at +0x10
{
    /*0x00*/ void* pInstance;        // the MatrixPacket returned by AddInstance
    /*0x04*/ u16   prevSlot;         // doubly linked within one SActiveRenderable::lod
    /*0x06*/ u16   nextSlot;         // 0x3ff = none
    /*0x08*/ u16   renderSetHandle;
    /*0x0a*/ u16   locationSubIndex;
};
```

The render sets are initialised **inline** in the ctor at 0x004b1290 to
`{0, 0, 0, 0xffff, 0, 0xffff}`. (0x004afd80 — which I first took for their ctor — is the
element ctor of the unrelated +0x91c array.) **[V]**

### 4.2 Entry points

```c
// 0x004af280  s16 RegisterInstanceObject(InstanceObject* o)
//   finds the first set with mFirstLocation == 0xffff (max 255), then
//   e->mpInstanceObject = o; e->mFirstLocation = mNumberLocations; e->mCullDistanceSq = 0;
//   e->mpTemplate = NULL; e->mNumberLocations = 0;   returns the index or -1
// 0x004af620  bool AddPreProcessedLocation(InstanceObject* o, SLocationData& l)
//   if (mNumberLocations >= 0x1055) return false;
//   mLocations[mNumberLocations] = l;  its B bits10..17 = o->mArrayAllocationHandle;
//   mNumberLocations++;  renderSet[handle].mNumberLocations++;  return true
// 0x004b0130  bool AddLocation(InstanceObject*, Vector* pos, Vector* rot, Vector* scale,
//                              float tint, const char* stateName)
// 0x004af5e0  SetLocationNameKey  (see 3.3)
// 0x004af300  EraseLocations
// 0x004b1eb0  UnregisterInstanceObject(InstanceObject*, s16 handle)
// 0x004b1890  RemoveActiveRenderablesByTemplate(StatePropTemplate*)
// 0x004b2c60  InstanceSetStateByIndex(InstanceObject*, s16 h, int idx, Key32 state, int, bool)
// 0x004b2910  CreateStatePropFromInstance(s16 h, int idx, bool, int, int)
```

`StatePropManager::InstanceObjectInitialise` = **0x004b3080** (called from
`InstanceObject::Register` 0x005e99d0, which matches the leaked source exactly): **[V]**

```c
void InstanceObjectInitialise(s16 handle, float fMaxCullDistance,
                              StatePropTemplate* t, int maxLocations)
{
    if (handle == 0xff) return;
    SInstanceRenderSet* e = &mRenderSets[handle];
    InstanceObject* pObj = e->mpInstanceObject;
    e->mpTemplate      = t;
    e->mCullDistanceSq = fMaxCullDistance * fMaxCullDistance;

    bool bHasStateData = false;
    if (t) {
        float r = t->mVisibilityEnd;              // t+0x40
        e->mCullDistanceSq = r * r;               // the template wins
        bHasStateData = (t->byte_0x22 != 0);
    }
    bool bAutoInstance = pObj->mIsAutoCreated ? bHasStateData : false;

    SLocationData* loc = &mLocations[e->mFirstLocation];
    for (int i = 0; i < maxLocations; i++) {
        loc[i].B = (loc[i].B & 0xFFFC03FF) | (handle << 10);
        int stateIdx = (loc[i].C >> 18) & 0xF;
        if (stateIdx && bHasStateData)
            InstanceSetStateByIndex(pObj, handle, i, g_StateKeys[stateIdx], -1, false);
        else if (bAutoInstance)
            InstanceSetStateByIndex(pObj, handle, i, 0, 0, false);
    }
}
```

`StatePropManager::SetMaxCullingDistances(Key32 key, float cullRadiusSq)` = **0x004afa00**:
for every set whose `mCullDistanceSq < 150.0f` (0x00746128, i.e. still at a default) and whose
InstanceObject's name key matches, set `mCullDistanceSq = cullRadiusSq`. **[V]**

### 4.3 The per-frame pass

```
0x004b3e30  StatePropManager tick callback (table at 0x0073bb90)
   phase 1 -> PreSimSet objects (vtable +0x20)
   phase 2 -> StatePropManager::Update  0x004b3a90   then PostSimSet (vtable +0x24)
```

**`StatePropManager::Update(Timer*)` = 0x004b3a90** **[V]**
1. tick the timers of the +0x91c array;
2. process the camera-collision fade list (0x0081cdd8, count 0x0081d0e0);
3. `ProcessFadeCollidableList` 0x004b0530;
4. get the camera, `GetPosition` → camPos;
5. `bFullScan = |camPos − lastCamPos|² > 400.0f` (0x0073ba88 → 20 m);
6. `InstanceUpdate(&camPos, bFullScan)` 0x004b31a0;
7. push `mDrawDistanceScale` to the renderer (0x00464040).

**`StatePropManager::InstanceUpdate(const Vector* camPos, bool bFullScan)` = 0x004b31a0** **[V]**

Time-sliced: a full scan resets the cursor, otherwise only **350 locations per frame**
(`+0x2d90`, cursor `+0x2cd8`).

```c
for (i = start; i < end; i++) {
    SLocationData* loc = &mLocations[i];
    if (loc->C & 0x40000000) continue;                  // dead
    h = (loc->B >> 10) & 0xff;
    if (h != cachedHandle) {                            // per-set data, cached across the loop
        e = &mRenderSets[h];
        cullLimitSq = e->mCullDistanceSq + 900.0f;      // 0x0073ba74
        if (!e->mpInstanceObject) { loc->C |= 0x40000000; continue; }
        t = e->mpTemplate ? e->mpTemplate : (e->mpTemplate = FindTemplate());   // 0x005ea980
        if (t) {
            bHasStateData = t->byte_0x22;
            float r = t->mVisibilityEnd * mDrawDistanceScale;
            cullLimitSq = r*r + 900.0f;                 // <-- the real cull radius
            wakeSq      = t->mWakeUpRadius ^ 2;
            ...
        }
    }
    Vector pos = { loc->posX, loc->posY100 * 0.01f, loc->posZ };
    float distSq = (pos.x-cam.x)^2 + (pos.z-cam.z)^2;   // *** 2D, XZ only, squared ***

    if (!(loc->A & 0x02000000) || distSq >= cullLimitSq || !bHasStateData) {
        if (loc->A & 0x08000000) RemoveLocationFromRenderable(loc, &cached);   // 0x004b1ac0
        continue;
    }
    int stateIdx = (loc->C >> 18) & 0xF;
    if (stateIdx)                          InstanceSetStateByIndex(..., g_StateKeys[stateIdx], -1, false);
    else if (!(loc->A & 0x08000000))       QueueLocationForRenderableAdd(e, loc, i);  // 0x004afee0
    else if (!(loc->A & 0x10000000) && distSq < wakeSq)
                                           InstanceSetStateByIndex(..., 0, 0, true);  // wake up
    // then attract points and collision objects
}
```

**There is no alpha ramp, LOD switch or fade-out inside StatePropManager.** It only *adds* and
*removes* instances; the renderer owns per-instance frustum culling, LOD and fading. What the
manager hands over is `mVisibilityEnd` at creation time (0x00463f80) and a global scale
(0x00464040). **[V]**

`StatePropManager::ProcessPendingRenderableAdds()` = **0x004b23b0**, called from the renderer
at 0x004656ba once per frame after `InstanceUpdate`: **[V]**

```c
for (u8* p = mPendingAdds; p < mPendingAdds + 2*mPendingCount; p += 2) {
    u8 h = p[0];  if (h == 0xff) continue;
    e = &mRenderSets[h];  t = e->mpTemplate;
    if (!t) { t = e->mpTemplate = FindTemplate(); if (t) e->mCullDistanceSq = t->mVisibilityEnd^2; continue; }
    if (!t->byte_0x22) continue;
    int lod = mLocations[e->mFirstLocation].C >> 31;           // 1 = the destroyed mesh
    SActiveRenderable* entry = bsearch(mActiveRenderables, t);
    if (!entry) { if (CreateInstanceRenderable(entry, lod)) entry = InsertSorted(...); mRenderableCount++; }
    e->mPendingAddStart = 0xffff;
    while (p[0] == h) { AddLocationToRenderable(&mLocations[e->mFirstLocation + p[1]], entry, p[1]); p += 2; }
}
mPendingCount = 0;
```

### 4.4 Creating the renderable for a prop model — 0x004b0250 **[V]**

```c
bool StatePropManager::CreateInstanceRenderable(SActiveRenderable* e, int lod)
{
    StatePropTemplate* t = e->pTemplate;
    if (!t || !t->byte_0x22) return false;

    Key32 shape = 0, lodShape = 0, lod2Shape = 0;
    if (lod == 1 && t->mFinalMeshNameUID /*+0x164*/) {
        shape = t->mFinalMeshNameUID;                // the destroyed-state mesh
    } else {
        shape = MakeKey32("InstanceShape", t->mModelUid /*+0x0c*/);
        if (t->mHasLOD  /*+0x170 bit19*/) lodShape  = MakeKey32("LODShape",  t->mModelUid);
        if (t->mHasLOD2 /*+0x170 bit20*/) lod2Shape = MakeKey32("LODShape2", t->mModelUid);
    }
    float sway = g_SwayTable[(t->mFlags170 >> 21) & 7];        // table at 0x0073bab0

    RenderableHandle* r = renderer_CreateInstanceRenderable(   // 0x00467060
            shape, lodShape, lod2Shape, t->mInventory /*+0x4c*/, 0, t->int_0x114, sway);
    if (!r) return false;

    u8 slot = mRenderableFreeList[mRenderableCursor++];
    mActiveRenderables[slot] = r;
    e->lod[lod].renderableSlot = slot;
    e->lod[lod].slotListHead   = 0x3ff;
    mRenderablesCreated++;
    renderer_SetCullDistance(r, t->mVisibilityEnd, (t->byte_0x50 >> 2) & 1);   // 0x00463f80
    return true;
}
```

Sway table at **0x0073bab0**: `{0.0, 0.5, 1.0, 4.0, 50.0, 200.0}` — exactly the six
`mSwayAmount` levels from the leaked source (none / Small / Normal / Extreme / Massive /
Gigantic). It is a **per-template constant handed to the renderer**; there is no sway maths in
StatePropManager. **[V]**

Note `lod[1]` is the *destroyed-state* mesh, not a distance LOD — distance LODs live inside a
single renderable. **[V]**

### 4.5 Submitting one instance — 0x004b0f10 **[V]**

```c
bool StatePropManager::AddLocationToRenderable(SLocationData* loc, SActiveRenderable* e, int sub)
{
    if (mLiveInstances > 850)      mOverBudget = 1;
    if (mLiveInstances >= 0x37e) { mDropped++; return false; }      // 894
    if (!e) return false;

    int lod = loc->C >> 31;
    if (e->lod[lod].renderableSlot == 0xffff) {
        if (e->pTemplate && e->pTemplate->byte_0x22) CreateInstanceRenderable(e, lod);
        if (e->lod[lod].renderableSlot == 0xffff) return false;
    }
    RenderableHandle* r = mActiveRenderables[e->lod[lod].renderableSlot];

    Vector scale; GetScale(loc, &scale);                           // 0x004aef10
    Vector rot = { ((loc->A >>  9) & 0xff) * 2.0f,                 // DEGREES, not radians
                    (loc->A        & 0x1ff),
                   ((loc->A >> 17) & 0xff) * 2.0f };
    Vector pos;   GetPosition(loc, &pos);                          // 0x004af0b0
    int   t4   = (loc->C >> 22) & 0xf;
    float tint = t4 ? t4 * (1.0f/15.0f) : 1.0f;

    MatrixPacket* inst = renderer_AddInstance(r, &pos, &rot, &scale, tint, 1);   // 0x00463db0
    if (!inst) return false;

    // take an SInstanceSlot from the free list, link it into e->lod[lod]
    u16 s = mInstanceFreeList[mInstanceCursor++];
    mInstanceSlots[s] = { inst, e->lod[lod].slotListHead, 0x3ff, (loc->B>>10)&0xff, sub };
    if (e->lod[lod].slotListHead != 0x3ff) mInstanceSlots[old].nextSlot = s;
    e->lod[lod].slotListHead = s;

    loc->B = (loc->B & ~0x3ff)      | s;
    loc->B = (loc->B & ~0x01FC0000) | (e->lod[lod].renderableSlot << 18);
    loc->A |= 0x08000000;
    renderer_SetInstanceVisible(r, inst, true);                    // 0x00463f00
    e->lod[lod].numInstances++;  mLiveInstances++;
    return true;
}
```

So for instanced rendering **the manager never builds a matrix** — it hands over position,
Euler angles *in degrees*, scale and tint, and the renderer composes the matrix (§5.3).
`BuildInstanceMatrix` (0x004b0000, §3.4) is used for the non-instanced paths: attract points
and `CreateStatePropFromInstance` when there is no render instance yet. (When there *is* one,
`CreateStatePropFromInstance` reads the matrix back from the renderer via 0x00463e80, which is
therefore the authoritative convention.) **[V]**

### 4.6 `InstanceObject::mPriority`

Used in exactly one place: `CreateStatePropFromInstance` (0x004b2910, at 0x004b2b49) does
`pStatePropObject->float_0xe0 = 1.0f / (float)mPriority;` — probably an update-rate scale.
It plays **no part** in instance rendering or culling. **[V]**

### 4.7 Cull / budget constants **[V]**

| addr | value | use |
|------|-------|-----|
| 0x0073ba74 | 900.0 | added to the squared cull distance (30 m hysteresis in squared space) |
| 0x0073ba88 | 400.0 | "camera moved a lot" threshold, squared (20 m) → full rescan |
| 0x0073ba78 | 1600.0 | attract-point distance² |
| 0x0073ba7c | 62500.0 | "Parking" attract distance² (250 m) |
| 0x00746128 | 150.0 | `SetMaxCullingDistances` "still unset" guard |
| 0x007c668c / 0x007c6690 | 850 / 97 | soft warning limits (instances / renderables) |
| — | 0x37e = 894 | hard instance cap |
| — | 0x67 = 103 | renderable cap |
| — | 0x7d0 = 2000 | pending-add cap |
| — | 0x1055 = 4181 | location cap |
| — | 0x15e = 350 | locations scanned per frame |

### 4.8 `StatePropTemplate` offsets **[V]**

| off | member |
|-----|--------|
| +0x0c | model-name UID (`core::Key32`) |
| +0x22 | bool "render/state data loaded" — gates every renderable operation |
| +0x40 | `mAttributes.mRenderableAttributes.mVisibilityEnd` |
| +0x4c | model `content::LoadInventory*` |
| +0x50 | byte flags; bit 2 = `mAllowDistanceClamping` **[G]** |
| +0x114 | int 0..3 passed to the renderable factory **[G: render bucket?]** |
| +0x150 | `mAttractPoint` (char*) |
| +0x158 | `mWakeUpRadius` |
| +0x15c | `mCollisionObjectRadiusSquared` |
| +0x164 | `mFinalMeshNameUID` (the mesh used for `lod[1]`) |
| +0x16c | `mScriptContextSetID` |
| +0x170 | bitfield: `mMovementType:8, mSurfaceType:4, mDynamicLightNameIndex:3, mDestroysTires:1, mIsAttractPoint:1(16), mCollisionFrustrumOnly:1(17), mHasCollision:1(18), mHasLOD:1(19), mHasLOD2:1(20), mSwayAmount:3(21..23), mDestroyOnCollision:1(24), mIsDoor:1(25), mHasRenderable:1(26), mTimeOfDayEnabled:1(27), mNeverWakes:1(28), mNeverCull:1(29), mIsEffectOnly:1(30)` |

## 5. The renderer side

### 5.1 Classes

| class | vtable | ctor | size | base |
|-------|--------|------|------|------|
| `renderer::RenderableHandle` | 0x0073781c | 0x00461870 | 12 | — (`Renderable*` at +4) |
| `renderer::InstanceRenderable` | 0x00737dc4 | 0x0046f8e0 | 0x88 | `renderer::Renderable` (base ctor 0x00474ba0) |
| `renderer::InstanceContainer` | 0x00737cfc | 0x0046f840 | 0x50 | `pure3d::DrawableContainer` |
| `renderer::InstancePrimitive` | 0x00737d7c | 0x0046f2e0 | 0x7c | `pure3d::DrawablePrimitive` |
| `pure3d::pddiExtInstancing::WindyMatrixPacketList` | 0x00767968 | 0x0064d960 | 0x2c | `MatrixPacketList` (vtable 0x007676cc) |

Ownership: `InstanceRenderable` +0x84 → `InstanceContainer`; the container holds one
`InstancePrimitive`; `InstancePrimitive` +0x78 → `WindyMatrixPacketList`. **[V]**

`Renderable+0x54` is a type tag; `InstanceRenderable` sets it to **0x200** and
`renderer::AddInstance` checks for exactly that. **[V]**

`InstancePrimitive` fields seen in the ctor **[V]**:

`InstancePrimitive::ctor(Drawable* a1, Drawable* a2, Drawable* a3, int a4, float sway)`
(`ret 0x14`); `InstanceContainer::ctor` and `InstanceRenderable::ctor` have the same 5-arg
signature and just forward. From `CreateInstanceRenderable` a1/a2/a3 are the
`InstanceShape` / `LODShape` / `LODShape2` geometries, a4 is a slot index (0..3) and
`sway` comes from the sway table.

```
+0x0c  u32 Display_List layer: 0x28 (40) if a4 == 1 or either drawable has flag 1 or 2 set;
       else 0x29 (41), or 0x2a (42) when the shader is one of the four in
       0x0081137c..0x00811388.  (See renderables.md §11 for the other layers; 40..42 are
       the eco-prop instancing layers.)
+0x38  pure3d::Geometry*  = ctor arg1 (the InstanceShape)     AddRef'd
+0x3c  pure3d::Geometry*  = ctor arg2 (the LOD shape)         AddRef'd
+0x40  pure3d::Geometry*  third slot; read for the bbox union but never assigned here  [?]
+0x44  float 10.0f
+0x48  0
+0x4c  float sway amount (from the sway table)
+0x54  0
+0x58  Vector bboxMin   (init +1e11)   union of the drawables' boxes
+0x64  Vector bboxMax   (init -1e11)
+0x70  ctor arg4
+0x74  flag byte, |= 6 at construction
+0x78  WindyMatrixPacketList*
```
`InstancePrimitive::GetSomeMask()` (vtable slot 7, 0x0046ed10) returns **0x00200000**. **[V]**

### 5.2 `renderer::AddInstance` — 0x00463db0 **[V]**

```c
// cdecl, 6 args
MatrixPacket* renderer_AddInstance(RenderableHandle* h, const Vector* pos, const Vector* rot,
                                   const Vector* scale, float alpha, int one)
{
    ScopedLock lock( RenderManager::GetLock(0xb) );          // 0x004675b0(0xb), 0x0043c710
    if (!h) return NULL;
    InstanceRenderable* r = h->renderable;                   // h+4
    if (r->typeTag /*+0x54*/ != 0x200) return NULL;
    return InstanceContainer_AddInstance(r, pos, rot, scale, alpha);   // 0x0046fba0
}

// 0x0046fba0
MatrixPacket* InstanceContainer_AddInstance(InstanceRenderable* r, ...)
{
    DisplayListElement* el = r->GetElement(0);               // 0x00473fc0
    InstancePrimitive* p   = *(InstancePrimitive**)el->drawable /*+0x44*/;
    return InstancePrimitive::AddInstance(p, pos, rot, scale, alpha);  // 0x0046f4b0
}
```

### 5.3 `renderer::InstancePrimitive::AddInstance` — 0x0046f4b0 **[V]**

```c
MatrixPacket* InstancePrimitive::AddInstance(const Vector* pos, const Vector* rot,
                                             const Vector* scale, float alpha)
{
    Matrix mt, mr;
    Matrix::Identity(&mt);  Matrix::Identity(&mr);
    Matrix::SetPosition(&mt, pos);
    Matrix::SetRotation(&mr, -rot->x*DEG2RAD, -rot->y*DEG2RAD, +rot->z*DEG2RAD);
    mr.row0 *= scale->x;  mr.row1 *= scale->y;  mr.row2 *= scale->z;
    Matrix final; Matrix::Multiply(&final, &mr, &mt);          // 0x00660... (sym.Matrix::Multiply)

    alpha = clamp(alpha, 0.0f, 1.0f);                          // >1 -> 1, <0 -> 0

    // grow this primitive's bounding box by the transformed local box
    // (0x006606d0 / 0x00427080 / 0x004202d0 — box transform + union)

    MatrixPacket* p = matrixPacketList->Add(&final, alpha, &boundingSphere, 1.0f);  // 0x00649c10
    ... optional per-instance shadow / backface work when flags at +0x74 are set ...
    return p;
}
```

**Important gotcha:** `MatrixPacketList::Add` (0x00649c10) takes the `alpha` argument but
**never reads it** in this build. Per-instance tint therefore appears to be dropped on PC.
**[V that the code does not read it, ? as to whether the tint is applied anywhere else]**

### 5.4 The matrix packet — 0x00649c10 **[V]**

```c
struct MatrixPacket {           // allocated from a global free list at 0x008309a8
    /*0x00*/ MatrixPacket* next;
    /*0x08*/ Matrix  matrix;          // 64 bytes, copied with rep movsd (0x10 dwords)
    /*0x48*/ u8      enabled;         // set to 1 on Add
    /*0x4c*/ float   sphere[4];       // centre xyz + radius, copied from Add's 3rd arg
    /*0x5c*/ float   fade;            // Add's 4th arg (always 1.0 from AddInstance)
    /*0x60*/ u8      visible;         // written by the per-frame frustum cull
};
```
`MatrixPacketList`: +0x04 list head, +0x10 cached iterate index, +0x14 cached iterate node,
+0x18 wind angle, +0x1c wind strength, +0x20 wind ?, +0x24 sin(windAngle), +0x28 cos(windAngle).
Iterator `GetPacket(i)` = **0x00649b20**, `SetWindAngle(float rad)` = **0x00649c80**,
`SetWindStrength(float)` = **0x00649cc0** (`+0x1c = 0.01f * s`, const 0x007ebff8). **[V]**

### 5.5 `renderer::InstancePrimitive::Display` — 0x0046f110 (vtable slot 8) **[V]**

```c
void InstancePrimitive::Display()
{
    int n = matrixPacketList->count;            // +0x0c
    if (n <= 0) return;
    Culler* cull = GetCurrentCuller();          // 0x00461ad0
    cull->vtbl[0x60](&frustumScratch);
    for (int i = 0; i < n; i++) {
        MatrixPacket* p = matrixPacketList->GetPacket(i);          // 0x00649b20
        if (!p) continue;
        Sphere s = p->sphere;                                      // p+0x4c
        if (!cull->vtbl[0x58](&s, s.r))  { p->visible = 1; continue; }   // trivially in
        p->visible = (cull_test(...) == 2);                        // 0x00460980
    }
    if (sway /*+0x4c*/ > 0.0f) {
        matrixPacketList->SetWindAngle( g_render->wind.angleDeg /*+0x1d4*/ * DEG2RAD );
        matrixPacketList->SetWindStrength( sway );
        matrixPacketList->field_20 = g_render->wind.field_1d8;
    }
    ClampWind(matrixPacketList, this->field_54);                   // 0x00650ba0
    pddiExtInstancing* ext = *(pddiExtInstancing**)0x00811368;      // created in the ctor via
                                                                    // renderContext->vtbl[0x190](0x200200)
    ext->vtbl[0x10](matrixPacketList);                              // the instanced draw call
}
```

So the whole batch for one prop model is drawn in one call; visibility is per packet.

---

## 6. `modelname` → renderable data

### 6.1 The name hash — `core::MakeKey32` / `pure3d::Entity::MakeUID`

`GetHash` **0x006dc190** (`MakeKey32(s)` = `GetHash(s, 0)`, thunk 0x006683b0 → 0x006dc230): **[V]**

```c
u32 GetHash(const char* s, u32 seed) {
    if (!s || !*s) return seed;
    u32 h = seed & 0x7fffffff;
    for (; *s; s++) {
        int c = (signed char)*s;
        if (c < 'a') c += 0x20;           // blind "tolower" — also hits digits/punctuation
        h = (h * 65599) & 0x7fffffff;
        h ^= c;
    }
    return h | 0x80000000;
}
```

Because the seed is masked, this is a **rolling** hash:
`GetHash("InstanceShape", MakeKey32("barrierA")) == MakeKey32("barrierAInstanceShape")`.
And because of the blind lowercasing, the `modelname` property value in `objects.ds`
(`"dumpstera"`, `"benchbusstop"`, `"signbustop"`) hashes identically to the real asset names
(`"dumpsterA"`, `"benchBusStop"`, `"signBusStop"`). **[V]**

Every `pure3d::Entity` has `u32 uid` at +0x08 = `MakeKey32(name)`; `Entity::SetName` =
0x0040fec0. Lookup in a package is `content::LoadInventory::Find(DynamicCaster*, u32 uid)`
= **0x006e9850** (vtable slot +0x10), which walks a uid hash table and `__RTDynamicCast`s
each hit; it recurses into the parent inventory if not found.

### 6.2 Chunk layouts of the StatePropData family

All verified from both the loader disassembly and the real bytes of all 220 z04 packages
(1046 root chunks, all `version == 4`). **[V]**

```
0x0802000d  pure3d::prop::StatePropData      (root; loader ctor 0x0068efd0, LoadObject 0x0068f290)
    u32     version                  // 4 in all shipped data
    pstring name                     // == modelname
    pstring compositeDrawableName    // == modelname too, in practice
    u32     numStates
    u32     numGlobalUserData        // only if version >= 3
  children: 0x08020008 x numGlobalUserData, 0x0802000e x numStates

0x0802000e  StateData
    pstring name                     // "idle", "final", "open", ...
    u32     autoTransition           // stored as bool
    u32     outState
    u32     numVisibilityData        // 0x08020002 children
    u32     numFrameControllerData   // 0x08020003
    u32     numEventData             // 0x08020004  (0 in z04)
    u32     numCallbackData          // 0x08020005  (0 in z04)
    f32     outFrame
    u32     unused                   // only if version >= 2, read and DISCARDED
    u32     numUserData              // only if version >= 3 -> 0x08020008
    u32     numEffectData            // only if version >= 4 -> 0x0802000b
  children in any order

0x08020002  VisibilityData
    pstring drawableName             // e.g. "dumpsterAInstanceShape", "dumpsterAShape"
    u32     visible                  // stored as bool

0x08020003  FrameControllerData
    pstring name
    u32 cyclic; u32 numberOfCycles; u32 holdFrame
    f32 minFrame; f32 maxFrame; f32 relativeSpeed

0x08020004  EventData     (unused in z04)   pstring name; u32 state; u32 eventEnum
0x08020005  CallbackData  (unused in z04)   pstring name; u32 eventEnum; f32 onFrame

0x08020008  UserData
    pstring fieldName                // "name","type","view_distance","sway","mass",...
    pstring fieldValue

0x0802000b  EffectData  (Scarface addition)
    pstring typeName                 // always "Effect"
    pstring effectName               // "epDebris_Wood", "epDebris_Metal", ...
    u32     unk                      // always 0
    pstring jointName
    f32[3]  position
    f32[3]  rotation                 // always 0 in the data
    u32     unk                      // always 0
    pstring drawableName             // == jointName in the data
    f32     scale                    // [G]
    f32     unk                      // [G]
```

The corresponding in-memory classes (`pure3d::prop::StatePropData` 0x2c bytes, vtable
0x0076aa04; `StateData` 0x44 bytes, vtable 0x0076a9c4; `UserData` 8 bytes) and every
per-chunk handler address are in section 7.

`StatePropDataLoader::LoadObject` resolves `MakeKey32(compositeDrawableName)` against the
inventory to a `pure3d::CompositeDrawable*` (stored AddRef'd at +0x0c) and the **same uid**
against `ravenphysics::CollisionObject` (at +0x10). If the CompositeDrawable is not found
the StatePropData is not created at all. **[V]**

### 6.3 The chain, end to end

```
objects.ds 0x09900192 "modelname" = "dumpstera"
   -> InstanceObject::CreateTemplate(MakeKey32("dumpstera"))                (leaked source)
   -> InstanceObject::s_ActiveStatepropTemplates lookup, filled by
      InstanceObject::StatePropOnLoadCallback when the package loads
   -> pure3d::prop::StatePropData "dumpsterA"          (chunk 0x0802000d)
   -> pure3d::CompositeDrawable "dumpsterA"            (chunk 0x00123000, children 0x00123001)
   -> meshes (chunk 0x00010000), shaders (0x00011000), textures (0x00019000)
   -> ravenphysics collision (0x07010000)
   -> renderer::StatePropRenderable "dumpsterA"        (chunk 0x08800005, loader 0x00478180)
```

But for the *instanced* drawing the renderer does not touch any of that: it looks up the
single mesh

```
MakeKey32("InstanceShape",  modelUid)   ==  MakeKey32("<modelname>InstanceShape")
MakeKey32("LODShape",       modelUid)   (only if template flag 0x08)
MakeKey32("LODShape2",      modelUid)   (only if template flag 0x10)
```

as a `pure3d::Geometry` in the template's `LoadInventory`. **[V]** — e.g.
`sbeachn_01_detail.p3d` contains a `0x00010000` mesh named `barrierAInstanceShape`, and that
name is also the `0x08020002` VisibilityData entry of the `idle` state of stateprop
`barrierA`.

That mesh is an ordinary Pure3D mesh — `0x00010000` with the usual `0x00010020` prim group
(`0x00010005` positions, `0x00010006` normals, `0x00010007` uvs, `0x0001000a` indices, ...)
and an ordinary `LocalShader_...` shader name; nothing instancing-specific in the asset.
It also carries an `0x00122000` chunk (2 floats, first always 0, second 0..1, usually 0.5),
but that chunk is on *every* drawable in the game (8275x, one per `0x00123001`), so it is not
instance-related. **[V]**

So the shortest path to "trees on screen" in the reimplementation is: for each
`0x09900194` record, build the matrix per §3.4 and draw the mesh named
`<modelname>InstanceShape` — no StatePropData, no CompositeDrawable, no instancing
extension needed.

### 6.4 Which package holds which model

`0x0802000d` occurs 1009x across 87 of the 220 `assets/packages/z04/*.p3d`, plus `Common.p3d`
(33 stateprops: identBalls, identCop, ...). 637 distinct stateprop names. Each package that
declares a model also contains its `0x00123000` CompositeDrawable, `0x00023000` Skeleton,
`0x00010000` meshes, `0x07010000` collision and `0x08800005` StatePropRenderable — so a model
package is self-contained. **[V]**

Examples: `dumpsterA` is in `bsand_01_detail.p3d`, `lobst_region_D.p3d`, `miami_lod_D.p3d`,
`tutorial_01_shell.p3d`; `benchBusStop`, `signBusStop`, `treeA`, `streetLightA` are only in
`miami_lod_D.p3d`. So the `*_detail.p3d` that *places* an instance is often not the package
that *contains* its model — the template lookup goes through whatever inventory is currently
loaded. **[V]**

Naming pattern confirmed in the assets (e.g. `miami_lod_D.p3d`, `islands_LOD_D.p3d`): for a
model `bushSetF` there is a `bushSetFInstance` (drawable) + `bushSetFInstanceShape` (mesh),
`bushSetFLOD` + `bushSetFLODShape`, `bushSetFLOD2` + `bushSetFLODShape2`. The renderer looks
up the **`...Shape`** (Geometry) names, exactly as the leaked source does with
`content::Find<pure3d::Geometry>(inv, MakeKey32("LODShape", modelUID))`. **[V]**

Coverage check over everything extracted under `assets/`: of the 455 distinct `modelname`s
that have placements in `z04`, **426 have a matching `<model>InstanceShape` mesh** somewhere
in the extracted data — 45698 of 46427 placements (98.4%). The 29 that don't (`jungle`,
`bushs`, `treetops`, `cloud`, `ax`, `fencea`, `lantern`, `shovel`, …) have no StatePropData
either, so their packages are simply not in the extraction. **[V]**

`art\ecoprops\*_ecoprops.p3d` inside `cement.rcf` is **not** needed (all 38 are already
extracted to `assets/art/ecoprops/` anyway): those files are the
per-level sources that were merged into the `packages\z04\*.p3d` (verified by extracting
`art\ecoprops\bsand_01_detail_ecoprops.p3d` and comparing — identical stateprop set). The one
model that exists only there is `missingProp` (the `c_MissingPropName` fallback in
`instanceobject.cpp`). **[V]**

---

## 7. Address table (proposed IDA names)

| address | proposed name |
|---------|---------------|
| 0x00488f50 | `ScriptObjectDataLoader::ctor` (chunk 0x09900190) |
| 0x00488f70 | `ScriptObjectDataLoader::LoadObject` (vtable slot 6) |
| 0x00488db0 | `ScriptObjectDataLoader::LoadLeafScriptObject` |
| 0x00488f90 | `GameGroupDataLoader::ctor` (chunk 0x09900191) |
| 0x00489250 | `GameGroupDataLoader::LoadObject` (vtable slot 6) |
| 0x00488fb0 | `GameGroupDataLoader::LoadScriptObjectTree` |
| 0x00488b60 | `ScriptObjectDataLoader::LoadPreProcessedLocation` (0x09900194) |
| 0x00739a78 / 0x00739a98 | `??_7ScriptObjectDataLoader@@6B@` / `??_7GameGroupDataLoader@@6B@` |
| 0x0047d64d / 0x0047d679 | the two `LoadManager::AddHandler` registrations |
| 0x004c1150 | `ChunkFile::GetPString` |
| 0x004453d0 | `ScriptObject::SetProperty` |
| 0x004956e0 | `ScriptObject_CreateByClassName` |
| 0x00441a40 | `GameGroup::ctor` |
| 0x006dc230 | `j_GetHash` / `core::MakeKey32` |
| 0x0042a180 | `the_StatePropManager` |
| 0x004af050 | `StatePropManager::SLocationData::ctor` |
| 0x004afb50 | `StatePropManager::SLocationData::Set` |
| 0x004af100 | `StatePropManager::SLocationData::SetRotation` |
| 0x004aef10 | `StatePropManager::SLocationData::GetScale` |
| 0x004af0b0 | `StatePropManager::SLocationData::GetPosition` |
| 0x004af280 | `StatePropManager::RegisterInstanceObject` |
| 0x004af620 | `StatePropManager::AddPreProcessedLocation` |
| 0x004b0130 | `StatePropManager::AddLocation` |
| 0x004af5e0 | `StatePropManager::SetLocationName` (state name key → 4-bit index) |
| 0x004af300 | `StatePropManager::EraseLocations` |
| 0x0071e340 | `InitStateNameKeys` (fills the table at 0x0081d238) |
| 0x0071e2e0 | `InitAttractPointKeys` (fills 0x0081d22c..0x0081d234) |
| 0x004afd80 | element ctor of the +0x91c array — **NOT** the render-set ctor (they are built inline at 0x004b1290) |
| 0x004b0000 | `StatePropManager::BuildInstanceMatrix` |
| 0x004b0250 | `StatePropManager::CreateInstanceRenderable` |
| 0x004b0f10 | `StatePropManager::AddLocationToRenderable` |
| 0x00660a90 | `Matrix::SetRotation` |
| 0x005e9410 | `InstanceObject::AddPreProcessedLocation` |
| 0x005e9490 | `InstanceObject::AddLocation` |
| 0x00463db0 | `renderer::AddInstance` |
| 0x00467060 | `renderer::CreateInstanceRenderable` |
| 0x00461870 | `renderer::RenderableHandle::ctor` |
| 0x0046f8e0 | `renderer::InstanceRenderable::ctor` |
| 0x0046f250 | `renderer::InstanceRenderable::dtor` |
| 0x0046f840 | `renderer::InstanceContainer::ctor` |
| 0x0046f2e0 | `renderer::InstancePrimitive::ctor` |
| 0x0046f110 | `renderer::InstancePrimitive::Display` |
| 0x0046ed10 | `renderer::InstancePrimitive::GetSomeMask` (returns 0x200000) |
| 0x0046f4b0 | `renderer::InstancePrimitive::AddInstance` |
| 0x0046fba0 | `renderer::InstanceRenderable::AddInstance` |
| 0x00473fc0 | `renderer::Renderable::GetElement` |
| 0x0064d960 | `pure3d::pddiExtInstancing::WindyMatrixPacketList::ctor` |
| 0x00649c10 | `pure3d::pddiExtInstancing::MatrixPacketList::Add` |
| 0x00649b20 | `pure3d::pddiExtInstancing::MatrixPacketList::GetPacket` |
| 0x00649c80 | `...::WindyMatrixPacketList::SetWindAngle` |
| 0x00649cc0 | `...::WindyMatrixPacketList::SetWindStrength` |
| 0x004b1220 | `StatePropManager::ctor` |
| 0x004b3080 | `StatePropManager::InstanceObjectInitialise` |
| 0x004b31a0 | `StatePropManager::InstanceUpdate(const Vector* camPos, bool fullScan)` |
| 0x004b3a90 | `StatePropManager::Update(Timer*)` |
| 0x004b3e30 | `StatePropManager_TickCallback` (table at 0x0073bb90) |
| 0x004b23b0 | `StatePropManager::ProcessPendingRenderableAdds` (called from the renderer at 0x004656ba) |
| 0x004afee0 | `StatePropManager::QueueLocationForRenderableAdd` |
| 0x004aff50 | `StatePropManager::SetLocationEnabled` |
| 0x004b1ac0 | `StatePropManager::RemoveLocationFromRenderable` |
| 0x004b1eb0 | `StatePropManager::UnregisterInstanceObject` |
| 0x004b1890 | `StatePropManager::RemoveActiveRenderablesByTemplate` |
| 0x004b2c60 | `StatePropManager::InstanceSetStateByIndex` |
| 0x004b2910 | `StatePropManager::CreateStatePropFromInstance` |
| 0x004afa00 | `StatePropManager::SetMaxCullingDistances` |
| 0x004b0530 | `StatePropManager::ProcessFadeCollidableList` |
| 0x004b2280 | `StatePropManager::InsertActiveRenderableSorted` |
| 0x004aee20 | `SInstanceSlot::ctor` |
| 0x0046a550 | `PtFuncCompare` (bsearch predicate for SActiveRenderable) |
| 0x005e9150 | `InstanceObject::ctor` |
| 0x005e9250 | `InstanceObject::RegisterWithStatePropManager` |
| 0x005e9270 | `InstanceObject::SetStateByIndex` |
| 0x005e99d0 | `InstanceObject::Register(int)` |
| 0x005e9880 | `InstanceObject::NotifyUnloadPackage` |
| 0x005e9b80 | `InstanceObject::dtor` |
| 0x005ea980 | `InstanceObject::FindOrCreateTemplateForRenderSet` (name is a guess) |
| 0x00463e60 | `renderer::InstanceRenderable_RemoveInstance` |
| 0x00463e80 | `renderer::InstanceRenderable_GetInstanceMatrix` |
| 0x00463f00 | `renderer::InstanceRenderable_SetInstanceVisible` (writes MatrixPacket+0x48) |
| 0x00463f80 | `renderer::InstanceRenderable_SetCullDistance(r, visEnd, clamp)` |
| 0x00464040 | `renderer::Instance_SetGlobalDrawDistanceScale(float)` |
| 0x0068efd0 | `pure3d::prop::StatePropDataLoader::ctor` (chunk 0x0802000d; a 2nd instance is registered for 0x0802000a at 0x00468163) |
| 0x0068f290 | `StatePropDataLoader::LoadObject(Entity**, u32* uid, ChunkFile*, LoadInventory*)` |
| 0x0068f433 / 0x0068f4d6 | its 0x08020008 (global) / 0x0802000e handlers |
| 0x0068f6d2 / 0x0068f757 / 0x0068f888 / 0x0068f921 / 0x0068f9ba / 0x0068fc55 | per-state handlers for 0x08020002 / 03 / 04 / 05 / 0b / 08 |
| 0x0068fd94, 0x0068fda0, 0x0068fdb0 | the loader's jump tables |
| 0x0068f1f0 | `StatePropData::ctor(CompositeDrawable*, CollisionObject*, int nStates, int nGlobalUD)` |
| 0x0068f060 / 0x0068f170 | `StatePropData::dtor` / scalar deleting dtor |
| 0x0068ee50 | `StatePropData::ReleaseCollisionObject` |
| 0x0068ecf0 / 0x0068eeb0 / 0x0068f040 | `StateData::ctor(nEv,nCb,nVis,nFC,nUD,nFx)` / dtor / scalar deleting dtor |
| 0x0068ec90 | `prop::UserData::SetValue(const char*)` |
| 0x0076aa04 / 0x0076a9c4 / 0x0076a9e4 | vtables: StatePropData / StateData / StatePropDataLoader |
| 0x006dc190 | `GetHash(const char*, u32 seed)` (= `core::MakeKey32`) |
| 0x006683b0 / 0x006683c0 / 0x0040fec0 | `Entity::MakeUID` / `UID::Set(const char*)` / `Entity::SetName` |
| 0x006e9850 | `content::LoadInventory::Find(DynamicCaster*, u32 uid)` |
| 0x00464540 / 0x00464500 / 0x00422150 | `DynamicCaster<CompositeDrawable / StatePropData / CollisionObject>::Cast` |
| 0x00478180 | `renderer::SFStatePropLoader::LoadObject` (chunk 0x08800005) |
| 0x00694230 / 0x00694a70 | `pure3d::CompositeDrawableLoader::ctor` (0x00123000) / `::LoadObject` |
| 0x00668900 | `pure3d::SkeletonLoader::ctor` (0x00023000) |

### Data constants

| address | value | meaning |
|---------|-------|---------|
| 0x0072f3c8 | +0.017453292 | deg→rad |
| 0x00737e08 | −0.017453292 | −deg→rad |
| 0x0072fa40 | 100.0 | Y position encode |
| 0x007c66a0 | 0.01 | Y position decode |
| 0x0073ba90 | 1000.0 | uniform scale encode |
| 0x007c66a8 | 0.001 | uniform scale decode |
| 0x0073ba80 | 20.0 | per-axis scale encode |
| 0x007c6698 | 0.05 | per-axis scale decode |
| 0x007c66ac | 20 (int) | default scale fields in the ctor |
| 0x0073cb48 / 0x007644e8 | 0.001 / 65.0 | uniform scale clamp |
| 0x0073bad0 | 1e−5 | "scale is uniform" epsilon |
| 0x0073bb54 / 0x007644ec | 14.999999 / 0.5 | tint encode |
| 0x0073c990 | 0.0666667 | tint decode (1/15) |
| 0x0073baa8 | 255 | `c_InvalidRenderSetIndex` |
| 0x0073bab0 | {0,0.5,1,4,50,200} | sway amount table |
| 0x0081d238 | 11 u32 | state-name key table (idle, final, damage_1..4, explosion, open, opening, closed, closing) |
| 0x007c668c / 0x007c6690 | 850 / 97 | active-instance soft budgets |

---

## 8. Open questions

1. `SLocationData.C` bit 27 — set right after a `StatePropObject` is created (0x004b2b85),
   cleared in `RemoveLocationFromRenderable` (0x004b1b72). Exact meaning unconfirmed. **[?]**
2. Per-instance tint/alpha is passed all the way down to `MatrixPacketList::Add` (0x00649c10)
   but that function never reads its 2nd argument. Is the tint applied through some other
   channel (vertex colour in the InstanceShape geometry, a per-batch shader constant) or simply
   dead on PC? **[?]**
3. `InstancePrimitive::ctor` keeps only two of the three drawables it is given (+0x38, +0x3c);
   +0x40 is read for the bounding-box union but never assigned. Where does `LODShape2` end
   up? **[?]**
4. `StatePropTemplate+0x22` (the bool that gates every renderable operation) and `+0x114`
   (an int asserted 0..3 by the renderable factory, alongside a hard-coded 0) need names. **[?]**
5. Who consumes the `0x0802000b` EffectData array (`StateData+0x38/+0x3c`)? Not located; the
   literal `"Effect"` does not exist in the exe, so the type key is probably never compared.
   Its `position` / `rotation` / `scale` / trailing float are inferred from value
   distributions. **[G]**
6. `StatePropData+0x1c` and `+0x28` are never written by the ctor or dtor — probably
   Array-template capacity fields. **[?]**
7. 29 of the 455 placed models (`jungle`, `bushs`, `treetops`, `cloud`, …, 1.6% of placements)
   have neither an `InstanceShape` mesh nor a `StatePropData` anywhere under `assets/` — which
   package are they in? **[?]**
8. Where `StatePropManager+0x2d88` (the global draw-distance scale) is written — a console
   variable / quality setting — was not traced. **[?]**
9. The camera-collision fade list's *expiry* path clears `A` bit 25 and calls
   `SetInstanceVisible(false)`, which reads backwards (expiry should restore visibility).
   Peripheral to eco-props but worth a second look. **[?]**
