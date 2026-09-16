# pure3d::prop::StatePropData chunk family (0x0802000d etc.) — PC

Report from the StatePropData sub-investigation (2026-09-15). Legend: **[V-dis]** verified from
disassembly, **[V-bytes]** verified from real p3d bytes (all 220 z04 packages + Common.p3d,
1046 StatePropData chunks), **[G]** guess. Complements instances.md §6.2.

## 0. Big picture (verified)

* `0x0802000d` = **StatePropData** (root, one per eco-prop model). Loader `pure3d::prop::StatePropDataLoader`, ctor `0x0068efd0`, `LoadObject` = `0x0068f290` (vtable `0x0076a9e4` slot 6). Registered with loadManager at `0x0046812c` for id `0x0802000d` and a second instance at `0x00468163` for id `0x0802000a` (same class; `0x0802000a` never occurs in z04 — presumably an older-version alias). [V-dis]
* `0x0802000e` = **StateData** (one per state; "idle", "final", "open", ...). Child of 0x0802000d. [V-dis][V-bytes]
* `0x08020002` = **VisibilityData** (drawable name + visible flag). Child of 0x0802000e.
* `0x08020003` = **FrameControllerData** (animation controller params). Child of 0x0802000e.
* `0x08020004` = EventData, `0x08020005` = CallbackData — handled by the loader (child of 0x0802000e) but 0 occurrences in z04.
* `0x08020008` = **UserData** {fieldName, fieldValue} — child of 0x0802000d (global) and of 0x0802000e (per-state).
* `0x0802000b` = per-state **effect attachment** record (type "Effect", particle effect name, joint/drawable name, offset, scale...). Child of 0x0802000e. Layout verified; semantics of some fields are [G].
* These are Scarface's extended versions of the SHR-era ids in `constants/chunkids.hpp` (`STATEPROP 0x08020000, STATEPROPSTATEDATA 0x08020001, VISIBILITYDATA 0x08020002, FRAMECONTROLLERDATA 0x08020003, EVENTDATA 0x08020004, CALLBACKDATA 0x08020005`), with schema in `toollib/Schema16/tlStatePropChunk.sc`. 0x08020002..05 keep the SHR layout exactly; 0x0802000d/0e are versioned supersets of 0x08020000/01.

All 1046 root chunks have **version = 4** and **name == compositeDrawableName** [V-bytes].

## A. Byte layouts

pstring = `u8 len; u8 data[len]` where `(1+len) % 4 == 0` in every one of the ~31,000 strings checked [V-bytes]; NUL-padded inside but not always NUL-terminated (e.g. `07 "iBenchA"`). The loader reads `len` bytes into a stack buffer and writes `buf[len]=0` itself [V-dis].

### 0x0802000d StatePropData [V-dis][V-bytes]
```
u32     version                 // always 4 in data; loader handles >=1
pstring name                    // -> Entity::SetName -> uid = MakeKey32(name)
pstring compositeDrawableName   // -> MakeKey32 -> LoadInventory::Find(CompositeDrawable)
u32     numStates               // ctor arg: sizes the StateData* array
u32     numGlobalUserData       // only if version >= 3
children: 0x08020008 (numGlobalUserData times), 0x0802000e (numStates times)
```
Example (Common.p3d @0x153dbb): `04000000 | 0b "identBalls\0" | 0b "identBalls\0" | 02000000 | 01000000`.
Any other child id is skipped (`EndChunk`).

### 0x0802000e StateData [V-dis][V-bytes]
```
pstring name                    // "idle","final",...
u32     autoTransition          // stored as u8 bool
u32     outState                // state index 0..5
u32     numVisibilityData       // count of 0x08020002 children
u32     numFrameControllerData  // count of 0x08020003 children
u32     numEventData            // count of 0x08020004 children
u32     numCallbackData         // count of 0x08020005 children
f32     outFrame                // 0 or floats like 30.0, 60.0
u32     unused_v2               // only if version>=2: read and discarded; always 0
u32     numUserData             // only if version>=3: count of 0x08020008 children
u32     numEffectData           // only if version>=4: count of 0x0802000b children
children: 0x08020002 / 03 / 04 / 05 / 08 / 0b in any order
```
Data stats: autoTransition {0:1564, 1:109}; outState 0..5; numUserData 0..2; numEffectData 0..5.
Counts are trusted blindly — no bounds check.

### 0x08020002 VisibilityData
```
pstring drawableName   // e.g. "identBallsInstanceShape", "dumpsterAShape"
u32     visible        // u8 bool; data: 1 (1975x) or 0 (1146x)
```

### 0x08020003 FrameControllerData (names from SHR schema)
```
pstring name             // e.g. "BQG_identCopInstanceShape"
u32     cyclic
u32     numberOfCycles
u32     holdFrame
f32     minFrame
f32     maxFrame
f32     relativeSpeed
```

### 0x08020004 EventData (not in z04) — `pstring name; u32 state; u32 eventEnum` (loader stores first u32 at struct+8, second at +4).
### 0x08020005 CallbackData (not in z04) — `pstring name; u32 eventEnum; f32 onFrame`.

### 0x08020008 UserData
```
pstring fieldName    // "name","percent_damaged","visibility","surface_type","hit_points","movement","mass","damaged_by","wakeup_radius","collision_on","collision_test","sway","type","view_distance","class","priority","explosion","time_of_day","life_cycle","plugin_templates",...
pstring fieldValue
```
Under 0x0802000d the first global entry is `name = <modelname>` (1046/1046).

### 0x0802000b "EffectData" [layout V; semantics partly G]
```
pstring typeName        // always "Effect"
pstring effectName      // "epDebris_Dust","epDebris_Metal","epDebris_Wood","epDebris_Glass","epDebris_Foliage","epExplosion_Electric",...
u32     unk_2c          // always 0
pstring jointName       // == drawableName
f32[3]  position        // (0, y, 0), y in {0,0.2,0.3,0.5,0.7,1,1.5,2}  [G: local offset]
f32[3]  rotation?       // always 0
u32     unk_28          // always 0
pstring drawableName
f32     scale?          // 1.0 (777x), 2.0, 1.5, 0.5, 0.7, 3.0 ...
f32     unk_34          // 0 (1477x) or 3.0,4.0,10.0,20.0 ... [G: lifetime/delay]
```

### Related chunks
* `0x08800005` renderer::StatePropRenderable (1009x, one per 0x0802000d). Loader renderer::SFStatePropLoader, LoadObject `0x00478180`. Payload `pstring name; pstring statePropDataName` (both == model name). Finds StatePropData by uid, stores at +0x84, calls `StatePropData::ReleaseCollisionObject` (`0x0068ee50`).
* `0x00123000` pure3d::CompositeDrawable: `u32 version(0); pstring name; pstring skeletonName; u32 numPrimitives`; children `0x00123001`. LoadObject `0x00694a70`, vtable `0x0076ad2c`, size 0x54.
* `0x00023000` pure3d::Skeleton (SkeletonLoader ctor `0x00668900`), child `0x00023001`.
* `0x07010000` ravenphysics collision object, `0x07011000` physics object.

## B. In-memory class layouts [V-dis]

```
struct Entity { void** vptr; int refCount; u32 uid; };   // slots: 0 AddRef 1 Release 2 GetRef 3 ~dtor 4 ?(returns false) 5 Clone 6 SetName
// Entity::SetName (0x0040fec0) = UID::Set(this+8, str) (0x006683c0); MakeKey32 = 0x006683b0 -> 0x006dc230 -> GetHash 0x006dc190

struct UserData { u32 fieldName; char* fieldValue; };                       // 8; SetValue 0x0068ec90 (strdup)
struct VisibilityData { u32 name; u8 isVisible; };                          // 8
struct FrameControllerData { u32 name; u8 cyclic; u32 numberOfCycles; u8 holdFrame; f32 minFrame; f32 maxFrame; f32 relativeSpeed; }; // 0x1c
struct EventData    { u32 name; u32 eventEnum; u32 state; };                // 0xc
struct CallbackData { u32 name; u32 eventEnum; f32 onFrame; };              // 0xc
struct EffectData { f32 pos[3]; f32 rot[3]; u32 effectName; u32 drawableName; u32 jointName; u32 typeName; u32 unk28; u32 unk2c; f32 scale; f32 unk34; }; // 0x38

struct StateData : Entity {            // 0x44, vtable 0x0076a9c4
  +0x0c  struct { u8 autoTransition; u8 pad[3]; u32 outState; f32 outFrame; }* hdr;  // block base (12 bytes)
  +0x10 int numVisibility;        +0x14 VisibilityData* visibility;
  +0x18 int numFrameControllers;  +0x1c FrameControllerData* frameControllers;
  +0x20 int numEvents;            +0x24 EventData* events;
  +0x28 int numCallbacks;         +0x2c CallbackData* callbacks;
  +0x30 int numUserData;          +0x34 UserData* userData;
  +0x38 int numEffects;           +0x3c EffectData* effects;
  +0x40 void* block;              // one allocation: hdr | visibility | frameCtrl | events | callbacks | effects | userData
};
// ctor 0x0068ecf0 StateData(nEv, nCb, nVis, nFC, nUD, nFx); dtor 0x0068eeb0; scalar deleting dtor 0x0068f040

struct StatePropData : Entity {        // 0x2c, vtable 0x0076aa04
  +0x0c CompositeDrawable* drawable;          // AddRef'd
  +0x10 ravenphysics::CollisionObject* collision;  // may be NULL; ReleaseCollisionObject 0x0068ee50
  +0x14 StateData** statesBegin; +0x18 StateData** statesEnd;   // each AddRef'd
  +0x1c u32 unused/capacity [G]
  +0x20 UserData* globalUserDataBegin; +0x24 UserData* globalUserDataEnd;
  +0x28 u32 unused/capacity [G]
};
// ctor 0x0068f1f0 StatePropData(CompositeDrawable*, CollisionObject*, nStates, nGlobalUD); dtor body 0x0068f060; scalar dtor 0x0068f170
```
Accessors (GetNumberOfStates, GetStateData, GetGlobalUserData, GetCompositeDrawable, GetVisibilityData...) are inlined everywhere.

Loader: `StatePropDataLoader : content::SimpleChunkHandler` (0x14 bytes, vtable 0x0076a9e4: 0 AddRef 1 Release 2 GetRef 3 RefDtor 4 SimpleChunkHandler::Load(0x6e9e70) 5 GetChunkId(0x4979a0) 6 LoadObject(0x68f290)).
`LoadObject(Entity** ppObject, u32* pUid, ChunkFile*, LoadInventory*)`: on success `*ppObject = data; *pUid = data->uid;` and, if global hook `[0x859bc8]` is set, calls it with `(inv->+0xc, data)` (dtor calls `[0x859bcc]`). If the composite drawable is not found it returns without creating anything.

### Pseudo-C of LoadObject 0x0068f290
```c
u32 version = rd_u32();
char name[0x100]; rd_pstring(name);
char buf[0x100];  rd_pstring(buf);                    // composite drawable name
CompositeDrawable* dr = inv->Find(&DynamicCaster<CompositeDrawable>, MakeKey32(buf));   // Inventory::Find 0x006e9850
if (!dr) return;
CollisionObject* col = inv->Find(&DynamicCaster<ravenphysics::CollisionObject>, dr->uid);
u32 numStates = rd_u32();
u32 numGlobalUD = 0; if (version >= 3) numGlobalUD = rd_u32();
StatePropData* spd = new StatePropData(dr, col, numStates, numGlobalUD);   // 0x0068f1f0
spd->SetName(name);
int stateIdx = 0, gudOff = 0;
while (cf->ChunksRemaining()) {
  switch (cf->BeginChunk()) {                          // jump tables 0x68fd94 / 0x68fda0
  case 0x08020008: { UserData* ud = spd->globalUserData + gudOff/8; rd_pstring -> ud->fieldName; rd_pstring -> ud->SetValue; gudOff += 8; break; }
  case 0x0802000e: {
      rd_pstring(buf);
      u32 autoTrans = rd_u32(), outState = rd_u32();
      u32 nVis = rd_u32(), nFC = rd_u32(), nEv = rd_u32(), nCb = rd_u32(); f32 outFrame = rd_f32();
      if (version >= 2) (void)rd_u32();
      u32 nUD = 0; if (version >= 3) nUD = rd_u32();
      u32 nFx = 0; if (version >= 4) nFx = rd_u32();
      StateData* st = new StateData(nEv, nCb, nVis, nFC, nUD, nFx);   // 0x0068ecf0
      st->SetName(buf); st->hdr->autoTransition = autoTrans != 0; st->hdr->outFrame = outFrame; st->hdr->outState = outState;
      while (cf->ChunksRemaining()) {
        switch (cf->BeginChunk()) {                    // jump table 0x68fdb0
        case 0x08020002: visibility[i++] = { uid(name), rd_u32()!=0 };                           // 0x0068f6d2
        case 0x08020003: frameCtrl[i++] = { uid(name), rd!=0, rd, rd!=0, rdf, rdf, rdf };        // 0x0068f757
        case 0x08020004: events[i++]    = { uid(name), state=rd, eventEnum=rd };                 // 0x0068f888
        case 0x08020005: callbacks[i++] = { uid(name), eventEnum=rd, onFrame=rdf };              // 0x0068f921
        case 0x0802000b: effects[i++]   = { typeName, effectName, unk2c, jointName, pos[3], rot[3], unk28, drawableName, scale, unk34 }; // 0x0068f9ba
        case 0x08020008: userData[i++]  = { uid(name), strdup(value) };                          // 0x0068fc55
        }
        cf->EndChunk();
      }
      st->AddRef(); spd->statesBegin[stateIdx++] = st;   // 0x0068fd13
      break; }
  }
  cf->EndChunk();
}
*ppObject = spd; *pUid = spd->uid;
```

## C. How the model name reaches a CompositeDrawable [V-dis]
1. The second pstring of 0x0802000d is hashed with MakeKey32.
2. `content::LoadInventory::Find(DynamicCaster*, u32 uid)` = `0x006e9850` iterates the inventory hash table (`this+8`) over entities stored under that uid and calls `caster->vtable[0](entity)` (`__RTDynamicCast`); first non-NULL wins; recurses into the parent inventory (`this+0xc`) via `0x006e98b0`. The CompositeDrawable must already be in the inventory — in z04 the `0x00123000` chunk precedes the `0x0802000d` chunk in the same file.
3. The collision object is looked up with the drawable's uid (the `0x07010000` chunk named like the model).
4. The CompositeDrawable looks up its `0x00023000` Skeleton by `MakeKey32(skeletonName)` the same way.
5. `instance location (0x09900194) → ScriptObject template → StatePropRenderable → StatePropData → CompositeDrawable → 0x00123001 primitives → 0x00010000 meshes`.

## D. Where the eco-prop data lives [V-bytes]
* `dumpsterA` → bsand_01_detail.p3d, lobst_region_D.p3d, miami_lod_D.p3d, tutorial_01_shell.p3d
* `benchBusStop`, `signBusStop`, `treeA`, `streetLightA` → miami_lod_D.p3d only
* 637 distinct stateprop names across z04; Common.p3d holds 33 (identBalls, identCop, identDrugsG, ...).
* `art\ecoprops\*_ecoprops.p3d` are the 38 per-level sources merged into `packages\z04\<level>.p3d`; not needed. Exception: `havana_02_shell_ecoprops.p3d` has `missingProp` (`c_MissingPropName` in instanceobject.cpp).

## E. Address table
| addr | name |
|---|---|
| 0x0068efd0 | pure3d::prop::StatePropDataLoader ctor (chunk 0x0802000d) |
| 0x0076a9e4 | StatePropDataLoader vtable (RTTI 0x0079d3d8) |
| 0x0068f290 | StatePropDataLoader::LoadObject |
| 0x0068f433 / 0x0068f4d6 | global 0x08020008 / 0x0802000e handlers |
| 0x0068f6d2 / 0x0068f757 / 0x0068f888 / 0x0068f921 / 0x0068f9ba / 0x0068fc55 | per-state handlers 02 / 03 / 04 / 05 / 0b / 08 |
| 0x0068fd94, 0x0068fda0, 0x0068fdb0 | jump tables |
| 0x0068f1f0 | StatePropData ctor; 0x0076aa04 vtable; 0x0068f060 dtor body; 0x0068f170 scalar dtor |
| 0x0068ee50 | StatePropData::ReleaseCollisionObject |
| 0x0068f190 / 0x0068f110 / 0x0068eff0 / 0x0068ee70 | Array<UserData>::Init / alloc / construct / destroy |
| 0x00468d60 | ArrayThing::Init4 = Array<StateData*>::Init(n, default) |
| 0x0068ecf0 | StateData ctor; 0x0076a9c4 vtable; 0x0068eeb0 dtor; 0x0068f040 scalar dtor |
| 0x0068ec90 | UserData::SetValue (strdup) |
| 0x006683c0 | UID::Set; 0x0040fec0 Entity::SetName; 0x00409cf0 UID::operator=(u32); 0x005e1bb0 UID::~UID (nop) |
| 0x006683b0 → 0x006dc230 → 0x006dc190 | MakeKey32(s) → GetHash(s, 0) |
| 0x006683f0 | Entity ctor; 0x00668410 Entity copy ctor |
| 0x006e9850 | content::LoadInventory::Find(DynamicCaster*, uid); vtable 0x007723ec; parent recursion 0x006e98b0 |
| 0x00464540 / 0x00464500 / 0x00422150 | DynamicCaster<CompositeDrawable / StatePropData / CollisionObject>::Cast; vtables 0x00737858 / 0x00737848 / 0x00732b84 |
| 0x006e9e70 | SimpleChunkHandler::Load; 0x004979a0 GetChunkId; 0x006ea060 LoadManager::AddHandler; 0x00468104..0x0046816d registration |
| 0x00478180 | renderer::SFStatePropLoader::LoadObject; vtable 0x00738690; StatePropRenderable vtable 0x0073864c, size 0x94, +0x84 = StatePropData* |
| 0x00694230 / 0x00694a70 | CompositeDrawableLoader ctor / LoadObject; CompositeDrawable vtable 0x0076ad2c |
| 0x00668900 | SkeletonLoader ctor |
| 0x00465bc0 / 0x0067cf60 | other DynamicCaster<StatePropData> users: lookup-by-name helper; creation of a 0x130-byte DrawableHierarchy-derived object via 0x0069dd30(StatePropData*, stateIdx, 1) [G: runtime StateProp instance] |
| 0x00859bc8 / 0x00859bcc | optional global hooks after StatePropData create / before destroy |

## Open questions
1. Consumer of the 0x0802000b EffectData array not located; field semantics guessed from value distributions.
2. StatePropData +0x1c and +0x28 never touched by ctor/dtor; probably capacity fields.
3. StateData vtable slot 4 (0x00652590, returns false) purpose unknown.
4. 0x0802000a registered as an alias of the same loader; never seen in data.
5. GetBoundingBox() not found (assumed to forward to CompositeDrawable::CalcBounds).
