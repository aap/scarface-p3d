# The `renderer::` renderable class family (PC retail)

Companion to `renderables.md` (chunk loaders + WorldGeo/ZonePkg/StateProp/Sky/Shadow/LightGroup
layouts), `renderspine.md` (frame spine, `Renderable::Display`, `Display_List`, culling),
`instances.md` (eco props) and `statepropdata.md`.  **This file does not repeat the chunk formats**
— it answers "what is each of these classes actually *for*, what is in it, and who makes it".

Marking: **[V]** verified from disassembly, **[?]** guess / partially traced.
All addresses are PC EXE VAs (`re/dis.sh`).

Every vtable in this file was located by walking the MSVC RTTI complete-object-locators
(`??_R4<Class>@renderer@@6B@`), so the class names are the **original ones from the PDB**, not
guesses.  One class has no IDA name but does have RTTI: `renderer::DecalRenderable`, vtable
`0x00737cac`, type descriptor `0x007c0738` = `.?AVDecalRenderable@renderer@@`. **[V]**

---

## 0. Family tree — one line per class, in plain words

```
pure3d::Entity
 |
 +- renderer::Renderable                    a world matrix + N (drawable, draw-distance) slots.  Each
 |  |                                       frame: distance -> frustum -> occluder -> fade, then push
 |  |                                       the surviving drawable into the Display_List.  EVERY class
 |  |                                       below is just "which drawable, which layer, what extra state".
 |  |
 |  |--- static map ------------------------------------------------------------------------
 |  +- WorldGeoRenderable      one piece of static map geometry (a shell / details / skyline / low_LOD
 |  |                          block).  The only class that can cull+fade the *sub-primitives* of its
 |  |                          composite individually, and the only one with its own distance
 |  |                          reference point (`otherPosition`) instead of its matrix.
 |  +- ZonePkgRenderable       not geometry: a named bag of WorldGeoRenderables = one streaming package
 |  |                          (= one .p3d).  SetVisible forwards to every member.  Carries their
 |  |                          per-package draw distances.
 |  +- PropRenderable          the degenerate Renderable: one DrawableHierarchy + min/max/fade, no extra
 |  |                          fields at all (size == base size 0x84).  The generic "static model".
 |  +- StatePropRenderable     a *template*: name -> pure3d::prop::StatePropData (benches, doors,
 |  |                          breakables — props with animation states) plus which memory pool a clone
 |  |                          of it fits in.  Real placements are clones of this.
 |  +- InstanceRenderable      one *model* of eco prop (tree/bush/lamp-post) with ALL its placements
 |  |                          batched into one hardware-instanced draw call, plus wind sway.
 |  +- MaskRenderable          takes a bare pure3d::Geometry and forces every PrimGroup of it onto
 |  |                          layer 36 — a mask/first-pass volume (interiors, screen mask).
 |  |
 |  |--- dynamic actors --------------------------------------------------------------------
 |  +- CharacterRenderable     a skinned character: composite + LOD composite + 8 palette shaders,
 |  |                          per-joint damage, per-bone enable.  Draws nothing until it has a drawable.
 |  +- VehicleRenderable       a car: owns a *private copy* of the composite's Pose so every car can be
 |  |                          posed independently; part-by-part damage/paint shader swapping.
 |  +- NISRenderable           a cut-scene ("non-interactive sequence") actor: like a character but its
 |  |                          position comes from the NIS track, and it is never distance-culled.
 |  +- ParticleEffectRenderable one particle-system instance.  `SetMatrix` == "play the effect here".
 |  +- ShadowRenderable        the blob / building shadow projector (layer 2).
 |  |
 |  |--- environment ----------------------------------------------------------------------
 |  +- SkyRenderable           the sky box: billboard quads on layer 28, everything else 38/39.
 |  |                          Ignores SetMatrix; obeys a global sky toggle.
 |  +- OceanRenderable         the water surface.  Its bounding sphere is re-centred on the camera every
 |  |                          frame with radius 100000, so it is never culled.  Layer 27.
 |  +- WakeRenderable          a boat wake: a 40-segment ribbon rebuilt every frame.  Layer 30.
 |  +- RainRenderable          rain streaks + splashes; one big (0x454-byte) singleton-ish object.
 |  +- LightingRenderable      draws NOTHING (Display is a nullsub).  It exists so a pure3d::LightGroup
 |  |                          (+ up to 5 LightAnimationControllers) can be registered with the light
 |  |                          manager as an ordinary scene object with a lifetime.
 |  |
 |  |--- the three RenderManager-owned global pools ---------------------------------------
 |  +- DecalRenderable         THE global decal pool (bullet holes, blood, footprints).
 |  +- SkidmarkRenderable      THE global skid-mark pool (48 tyre ribbons).
 |  +- TraceFireRenderable     THE global tracer-round pool.
 |  |                          None of these three is in a Scene; RenderManager owns them at +0x30 /
 |  |                          +0x34 / +0x38 and Display_List::Render calls their vslot11 at three
 |  |                          fixed points in the layer order (see §4).
 |  |
 |  +- PlugInRenderable        the escape hatch.  Game code registers a custom "renderable model"
 |                             (sky/rain/lighting/tracefire/terrain plug-ins) and this wraps the
 |                             resulting object so it can live in a Scene and be rendered immediately.
 |
 +- (handles, not renderables — a game-side reference to a renderable, 12..0x14 bytes)
    +- RenderableHandle        owns a reference; its destructor Hides the renderable.
    +- SkidMarksHandle         RenderableHandle + one skid-mark writer state block.
    +- DecalsHandle            RenderableHandle + one pure3d::Shader = "a decal *type*".
    +- TraceFireHandle         RenderableHandle + one CompositeDrawable = "a tracer *type*".

pure3d::DrawableContainer                pure3d::DrawablePrimitive
 +- renderer::DecalContainer              +- renderer::DecalPrimitive
 +- renderer::InstanceContainer           +- renderer::InstancePrimitive
 +- renderer::OceanContainer              +- renderer::OceanPrimitive
 +- renderer::SkidmarkContainer           +- renderer::SkidmarkPrimitive
 +- renderer::VehicleContainer            +- renderer::VehiclePrimitive
 +- renderer::WakeContainer               +- renderer::WakePrimitive

renderer::Skidmark                        one skid ribbon (0x684 bytes), 48 of them in a global array.
renderer::DecalPrimitive::CDecalDrawable  / CStaticDecalDrawable / COceanDecalDrawable
                                          the three "how does this decal build its geometry" strategies
                                          (3-slot mini-vtables, no refcount).
```

---

## 1. `renderer::Renderable` — the 16-slot vtable everything shares

vtable `0x0073838c`, ctor `0x00474ba0`, base size **0x84**. **[V]**

| slot | vtbl off | base impl | proposed name | notes |
|---|---|---|---|---|
| 0 | +0x00 | 0x40f390 | `AddRef` | |
| 1 | +0x04 | 0x40f3a0 | `Release` | |
| 2 | +0x08 | 0x673c00 | `GetRef` | |
| 3 | +0x0c | 0x474aa0 | `~Renderable` | scalar-deleting dtor |
| 4 | +0x10 | 0x652590 (`return false`) | `Entity::vslot4` **[?]** | never overridden by any renderable |
| 5 | +0x14 | 0x46d130 (`return this`) | `Entity::Clone` | Decal/Shadow/Skidmark override it to `return 0` = "not clonable" |
| 6 | +0x18 | 0x438400 (`ret 4`) | `SetName` | **no-op in retail**; renderables keep no name |
| 7 | +0x1c | 0x652580 (`return 0`) | *unknown* **[?]** | never overridden by anything |
| 8 | +0x20 | 0x473c20 | `SetVisible(bool)` | sets flags80 bit4 + `prim.SetVisible` on each element |
| 9 | +0x24 | 0x438400 (`ret 4`) | **`Update(TimeInfo*)`** | called from `Scene::Update`/`GamePlayScene::Update` just before Display. The most-overridden slot. |
| 10 | +0x28 | 0x4740f0 | **`Display()`** | the big distance/frustum/fade routine, renderspine §2 |
| 11 | +0x2c | 0x5e1bb0 (nullsub) | **`RenderImmediate()`** | *immediate-mode* draw. `Scene::Render` (0x468b80, the non-gameplay scenes) calls this instead of using the Display_List; `Display_List::Render` calls it on the three global pools. |
| 12 | +0x30 | 0x473b40 | `SetMatrix(Matrix*)` | |
| 13 | +0x34 | 0x473c00 | `GetPosition(Vector*)` | default = matrix row 3 |
| 14 | +0x38 | 0x473c80 | `Hide()` | removes every element's nodes from the Display_List |
| 15 | +0x3c | 0x559000 (`return false`) | `GetDistanceRefPos(Vector*)` | WorldGeo overrides |

`Renderable::Tick` (0x004740c0) is **not virtual** — `GamePlayScene::Update` calls it directly on
every renderable (`+0x78 += dt; if (flags80 & 2) UpdateFade(dt)`).  So "what does Tick do" is the
same for every class in this document. **[V]**

### Corrections/additions to the field table in `renderables.md` §1

```
  +0x7c  i32 sceneId          <-- Scene::AddRenderable (0x468ac0) writes Scene::+0x14 here.
                                  ctor default 5 == "not in any scene".            [V]
  +0x80  bit3 (0x08) hasHandle  <-- RenderableHandle::ctor sets it, the dtor clears it. [V]
```

`Scene` layout correction: `RenderManager+0x04` is a **pointer to** an array of 4 `Scene*`
(not an inline array); `Scene+0x14` is the scene id. **[V]**

### How the four scenes are made — `RenderManager::Init` 0x00468490 **[V]**

```
scenes[0] = new GamePlayScene(0, cfg[0x0c], cfg[0x1c], cfg[0x20])   // 0x468f90, size 0x24
scenes[2] = new Scene(2, cfg[0x04])                                 // 0x468ed0, size 0x1c
scenes[3] = new Scene(3, cfg[0x08])
scenes[1] = new Scene(1, cfg[0x00])
```
All four start with `typeMaskFilter (+0x18) = 0xffffffff`, i.e. the filter is **not** what
partitions the classes — the *creator* picks the scene (the leak always passes
`renderer::GAMEPLAY_SCENE` = 0, `renderer::GUI_SCENE` for HUD).  Only scene 0 is a
`GamePlayScene`, and only a `GamePlayScene` runs `Tick` + `Update` + `Display` into the
`Display_List`; the other three run only `Update`, and render with `RenderImmediate` (vslot 11).
Render order is 0, 2, 3, 1 (renderspine §1.4). **[V]**

`GamePlayScene::AddRenderable` (0x00468cc0) has two special cases: `typeMask == 0x100`
(ShadowRenderable) with `isBuildingShadow` → `0x475760`; `typeMask == 1` (SkyRenderable) →
`0x467950(r, r->+0x88)`. **[V]**

---

## 2. The Container / Primitive pair pattern (shared, described once)

Six renderer classes are `pure3d::DrawableContainer` subclasses and six are
`pure3d::DrawablePrimitive` subclasses, always in matching pairs, and the ctor chain is always
the same shape: **[V]**

```c
XxxRenderable::XxxRenderable(args)                 // size 0x84..0xc0
{
    Renderable::Renderable(1);                     // one DisplayListElement
    vtable = XxxRenderable; typeMask = <bit>;
    this->container /*+0x84*/ = new XxxContainer(args);
    SetElement(container, 0, flag);                // 0x473e70
    <SetElementDrawDist / clear flags80 bits>
}
XxxContainer::XxxContainer(args)                   // DrawableContainer, size 0x3c..0xd8
{
    DrawableContainer::DrawableContainer(1);       // 0x683 area
    vtable = XxxContainer;
    prim = new XxxPrimitive(args);
    DrawableContainer::SetElement(0, prim);        // 0x6836e0
    this->sortKey /*+0x3c*/ = <const>;
}
```

Container `+0x3c` is the **sort key** the Display_List sorter reads (renderspine §3.4).
Observed values: Decal 0.0, Skidmark 0.0, Ocean 0.5, Wake 0.89 (`0x3f63d70a`). **[V]**

### `pure3d::DrawableContainer` vtable (0x00769e14, 30 slots) — the slots the renderer overrides

| slot | base | meaning | who overrides |
|---|---|---|---|
| 3 | 0x683d80 | dtor | all six |
| 4 | _purecall | ? | all six → `0x652590` (`return false`) |
| 5 | _purecall | `Clone` | all six → `Entity::Clone` (`return this`) |
| 8 | 0x6833c0 | ? | all six → `nullsub_12` (0x64f920) |
| **10** | _purecall | **`Display(Display_List*, DisplayListPrimitive*)`** | see below |
| 14 | 0x683790 | `CalcBounds` | VehicleContainer (0x479a90) |
| 17 | 0x438400 | `SetFading(bool)` | VehicleContainer → `+0xcc` |
| 18 | 0x438400 | `SetFadeAmount(float)` | VehicleContainer → `+0xd0` |
| 19 | 0x652590 | `IsFading()` | VehicleContainer → `+0xcc` |
| 20 | 0x41dfd0 | `GetFadeAmount()` | VehicleContainer → `+0xd0` |
| **25** | _purecall | **`GetTypeMask()`** (see §3) | all six |
| 26 | 0x5a4810 | ? | VehicleContainer → `return false` |

Slot 10 splits the six into two camps: **[V]**
* **`j_DrawableContainer::Display` (0x004707f0 → 0x6834c0)** — Instance, Ocean, Wake: the normal
  path, push the element into the Display_List.
* **`0x004773e0`** — Decal, Skidmark: `elements[0]->vslot8()`, i.e. *ignore the display list and
  draw right now*.
* **`0x00478fe0`** — Vehicle: compute a world sort origin from the pose, then call the base.

### `pure3d::DrawablePrimitive` vtable (0x00769f24, 17 slots)

| slot | base | meaning |
|---|---|---|
| 3 | 0x685fc0 | dtor |
| **7** | _purecall | **`GetTypeMask()`** — a single bit identifying the primitive class (§3) |
| **8** | _purecall | **`Display()`** — the actual draw, called by the per-list renderers |
| 9 | _purecall | `GetShader()` |
| 10 | _purecall | ? (1 arg) |
| 11 | _purecall | ? (`return false`) |
| **13** | _purecall | **`CalcBounds()`** — fills the base's `+0x28..+0x34` sphere |
| 14,15 | 0x438400 | fade setters |
| 16 | 0x5a4810 | ? |

`DrawablePrimitive` base fields: `+0x0c = layer`, `+0x10..+0x24` reserved, `+0x28..+0x34`
bounding sphere (all zeroed by the ctor 0x00685f70). **[V]**

### The `layer` field is a *category*, not a list index **[V]**

`0x00703300` is **not** a dedicated `SetLayer` — it is the generic one-argument setter
`{ this->field_0C = arg; }` that MSVC identical-COMDAT-folded across many classes (e.g. 0x6a3460
calls it with a *pointer* argument).  Layers are just as often written with a direct
`mov dword [prim+0x0c], imm`.  The default for an ordinary mesh comes from
**`pure3d::PrimGroup::SetShader` (0x006a4c60)**:

```c
if (shader->shaderType /*+0x15*/ == 5)                 layer = 31;
else if (shader->blendMode /*+0x14*/ in 1..7)          layer = 1;
else                                                   layer = 0;
```

so characters, vehicles, props and NIS actors carry layer 0 / 1 / 31 unless something overrides
them, and `Display_List::AddContainerElement` (0x0045d3b0) maps the category to one of the 84
lists.  Only `WorldGeoLoader`, `ShadowLoader`, `SkyLoader`, `MaskRenderable`, the StatePropManager
night-light pass and the six `renderer::` primitives above assign a layer explicitly.

---

## 3. Two mask tables

### 3.1 `Renderable::typeMask` (+0x54) → class → scene

`renderables.md` §1 already lists the constants; this adds who puts it in which scene.
Scene is whatever the creating façade is handed; the leak passes `GAMEPLAY_SCENE` (0) everywhere
except HUD/frontend.  "RM" = owned directly by the RenderManager, not in a Scene at all.

| typeMask | class | created by | scene |
|---|---|---|---|
| -1 | `Renderable` (base) | — | — |
| 0x00000001 | `SkyRenderable` | chunk 0x08800002 loader; `SkyRenderable_Create` 0x4667c0 | 0 |
| 0x00000002 | `TraceFireRenderable` | `RenderManager::Init` 0x468490 | **RM +0x38** |
| 0x00000004 | `ParticleEffectRenderable` | `ParticleEffectRenderable_Create` 0x465840 | 0 |
| 0x00000008 | `WorldGeoRenderable` | chunk 0x08800003 loader | 0 |
| 0x00000008 | `PropRenderable` | `PropRenderable_CreateInstance` 0x466600 | arg (0..3) |
| 0x00000010 | `StatePropRenderable` | chunk 0x08800005 loader; clones via 0x465990 | 0 |
| 0x00000020 | `CharacterRenderable` | `CharacterRenderable_CreateInstance` 0x4661b0 | 0 |
| 0x00000040 | `OceanRenderable` | `OceanRenderable_CreateInstance` 0x466940 | 0 |
| 0x00000080 | `WakeRenderable` | `WakeRenderable_CreateInstance` 0x466b10 | 0 |
| 0x00000100 | `ShadowRenderable` | chunk 0x08800008 loader; `ShadowRenderable_CreateInstance` 0x467210 | 0 |
| 0x00000200 | `InstanceRenderable` | `InstanceRenderable_CreateInstance` 0x467060 | 0 (asserted 0..3) |
| 0x00000400 | `LightingRenderable` | chunk 0x08800007 loader; `LightingRenderable_CreateInstance` 0x466ce0 | 0 |
| 0x00000800 | `RainRenderable` | `RainRenderable_CreateInstance` 0x466e90 | arg |
| 0x00001000 | `MaskRenderable` | `MaskRenderable_CreateInstance` 0x4673f0 | arg (0..3) |
| 0x00002000 | **`DecalRenderable`** | `RenderManager::Init` 0x468490 | **RM +0x30** |
| 0x00004000 | `VehicleRenderable` | `VehicleRenderable_CreateInstance` 0x465e60 | 0 |
| 0x00008000 | `ZonePkgRenderable` | chunk 0x08800004 loader; `ZonePkgRenderable_CreateInstance` 0x4664a0 | 0 |
| 0x00020000 | `NISRenderable` | `NISRenderable_CreateInstance` 0x466310 | 0 |
| 0x00040000 | `PlugInRenderable` | `Renderable_CreateInstance` 0x464f00 | arg |
| 0x00100000 | `SkidmarkRenderable` | `RenderManager::Init` 0x468490 | **RM +0x34** |

(0x10000, 0x80000, 0x200000.. are unused at the Renderable level.)

### 3.2 Drawable type mask — `DrawablePrimitive` vslot 7 / `DrawableContainer` vslot 25 **[V]**

This is the mask `WorldGeoLoader` tests (`== 2/4/8`) and `ShadowLoader` tests (`== 0x20`).

| value | returned by | addr |
|---|---|---|
| 0x00000001 | (a pure3d primitive kind; layer-44 helper 0x4653c0 tests it) | |
| 0x00000002 / 4 / 8 | world-geo primitive kinds → layer 1 | |
| 0x00000020 | shadow primitives | |
| 0x00020000 | `OceanPrimitive` | 0x4707b0 |
| 0x00040000 | `OceanContainer` | 0x470800 |
| 0x00080000 | `WakePrimitive` | 0x47aaa0 |
| 0x00100000 | `WakeContainer` | 0x47aac0 |
| 0x00200000 | `InstancePrimitive` | 0x46ed10 |
| 0x00400000 | `DecalPrimitive` | 0x46d110 |
| 0x00800000 | `VehiclePrimitive` **and** `InstanceContainer` | 0x478b10 (one function — MSVC ICF) |
| 0x01000000 | `SkidmarkPrimitive` **and** `DecalContainer` | 0x4760d0 (one function — MSVC ICF) |
| 0x02000000 | `VehicleContainer` | 0x478b20 |
| 0x04000000 | `SkidmarkContainer` | 0x4760f0 |

The two shared entries really are one function each: the compiler folded identical bodies.  Since
the value is read through two *different* vtable slots the collision is harmless, but it means the
"one bit per class" scheme was not maintained consistently. **[V]**

---

## 4. Where each class ends up in the frame

* Everything in scene 0 goes through `Renderable::Display` → `DisplayListPrimitive::SetVisible` →
  `DrawableContainer::Display` → `Display_List`, and is drawn by `Display_List::Render`
  (0x0045e680) in the list order of renderspine §3.6.
* The three RenderManager-owned pools are drawn **inside** `Display_List::Render` by explicit
  `vslot11` calls: **[V]**

| where | call | what |
|---|---|---|
| after `RenderList(51)` | `0x45ecc8: renderMgr->+0x34->vslot11()` | **skid marks** |
| after `0x45b240` (list 15, 2nd pass) | `0x45ed8f: renderMgr->+0x24->vslot11()` | optional extra pass object |
| after `0x45c590` (lists 39, 30, 24), before list 65 | `0x45eeff: renderMgr->+0x30->vslot11()` | **decals** |
| after `0x459f70` (list 66), before `RenderList(55)` | `0x45ef36: renderMgr->+0x38->vslot11()` | **tracers** |

  A fourth site (`0x45b06b/0x45b07d/0x45b08b`) calls **vslot 9 (`Update`)** on the same three,
  once per frame.
* Scenes 1..3 (`Scene::Render`, 0x00468b80) render their members with `vslot11` directly, wrapped
  in a renderContext state push/pop (`[ctx+0x15c]` / `[ctx+0x158]`).

---

## 5. Per-class reference

### 5.1 `WorldGeoRenderable` — vtable 0x007381e4, size 0xa0, typeMask 8

Layout, chunk format, the 30-entry shader-type→layer table and `Display` dispatch:
**see `renderables.md` §4**.  Only the additions here:

| slot | addr | what |
|---|---|---|
| 3 | 0x471d60 | dtor |
| 8 | 0x471570 | `SetVisible` — base, then `primitives[i].SetVisible(v)` for all `numPrimitives` |
| 10 | 0x471640 | `Display` — the two global toggles + the per-sub-primitive path |
| 14 | 0x471510 | `Hide` |
| 15 | 0x4714e0 | `GetDistanceRefPos` — returns `otherPosition` when `flags90 & 1` |

It is the only class that overrides slots 8/14/15.  `Update` (slot 9) is *not* overridden.

### 5.2 `ZonePkgRenderable` — vtable 0x00738254, size 0x90, typeMask 0x8000

Layout + chunk: `renderables.md` §3.

| slot | addr | what |
|---|---|---|
| 3 | 0x471e70 | dtor |
| 8 | **0x471bc0** | `SetVisible(v)`: **does not call the base** — loops `worldGeos[]` and calls `wg->vslot8(v)` on each **[V]** |
| 12 | 0x438400 | `SetMatrix` is a no-op — a package has no transform **[V]** |

`Display` is the base one, but the renderable has zero elements, so in practice a ZonePkg draws
nothing itself: it is purely a *group handle* over its world geos.
Created by the 0x08800004 loader and by `renderer::ZonePkgRenderable_CreateInstance`
(0x004664a0; the leak's `loadpackage` / debug-collision code calls it with the package name).

### 5.3 `PropRenderable` — vtable 0x0073819c, **size 0x84 = the base size**, typeMask 8

The simplest renderable in the engine: it adds **no fields at all**. **[V]**

```c
PropRenderable::PropRenderable(DrawableHierarchy *d, float min, float max, float fade)  // 0x004713e0
{
    Renderable::Renderable(1);
    vtable = PropRenderable;  typeMask = 8;
    SetElement(d, 0, false);
    SetElementDrawDist(0, min, max, fade);
}
```
Overrides: slot 3 dtor 0x471620, slot 9 `Update` 0x471460.
Created by `renderer::PropRenderable_CreateInstance` (**0x00466600**): validate scene index 0..3,
`FindInventoryByID`, `inv->Find<pure3d::DrawableHierarchy>(GetHash(name))` (caster vtable
0x7376e0), `new PropRenderable(0x84)`, `SetName`, `scenes[i]->AddRenderable`, wrap in a
`RenderableHandle`.

### 5.4 `StatePropRenderable` — vtable 0x0073864c, size 0x94, typeMask 0x10

Chunk, pool classification, `ClassifySize`: `renderables.md` §2; the data format: `statepropdata.md`.

| slot | addr | what |
|---|---|---|
| 3 | 0x477d10 | dtor |
| 9 | **0x478070** | `Update(TimeInfo*)` — if `+0x88`: take lock 8, re-bind element 0's drawable to the current pose (`drawable->vslot8(pose, 0)`), `Hide()`, repeat for elements 1..n-1; if `+0x90` call 0x473cc0 **[V]** |

`Display` is the base one, but with `flags80 |= 0x80` so `Renderable::Display` builds the bound
sphere from the **pose bounding box** instead of the drawable's static sphere (renderspine §2.2).
Clone ctor 0x00477b20 (used both by the pool-size probe and by
`renderer::StatePropRenderable_CreateInstance`, **0x00465990**).

### 5.5 `InstanceRenderable` / `InstanceContainer` / `InstancePrimitive`

Everything about the LOD bands, the wind, the matrix packets and the pddi instancing extension is
in `renderspine.md` §5 and `instances.md` §5.  Vtable summary:

| class | vtable | ctor | size |
|---|---|---|---|
| `InstanceRenderable` | 0x00737dc4 | 0x0046f8e0 | 0x88 |
| `InstanceContainer` | 0x00737cfc | 0x0046f840 | 0x50 |
| `InstancePrimitive` | 0x00737d7c | 0x0046f2e0 | 0x7c |

`InstanceRenderable` overrides: 3 dtor 0x46f9c0, **9 `Update` 0x46fc40**, 10 `Display`
0x470810 (= a `jmp` thunk to the base 0x4740f0), 12 `SetMatrix` → no-op.
`Update` is one line: `container->prim->+0x54 = timeInfo->+0x08` — it feeds the wall-clock time
into `InstancePrimitive+0x54`, which `ClampWind` (0x650ba0) uses. **[V]**
The ctor clears `flags80 & ~3`, i.e. **no distance test and no fade** by default; the cull radius
is installed afterwards by `renderer::InstanceRenderable_SetCullDistance` (0x00463f80, which
checks `typeMask == 0x200` and forwards to 0x0046fbf0).
Layers 40/41/42 → Display_List lists 72/73/74, the three instancing renderers.

### 5.6 `CharacterRenderable` — vtable 0x00737b3c, size 0xbc, typeMask 0x20

Chunk 0x08800000 + the 8 shader slots: `renderables.md` §9.

| slot | addr | what |
|---|---|---|
| 3 | 0x46ca60 | dtor |
| 9 | 0x46c8f0 | `Update(TimeInfo*)` (218 bytes) |
| 10 | **0x46c620** | `Display()`: `if (this->+0x84) Renderable::Display();` — a character with no drawable draws nothing **[V]** |
| 13 | 0x46c5f0 | `GetPosition(Vector*)` → copies `+0xb0..+0xb8` (the pose root), not the matrix **[V]** |

ctor 0x0046c650: `+0x84` drawable/pose, `+0xa8`, `+0xac`/`+0xad` bytes, `+0xb0..+0xb8` position.
Created by `renderer::CharacterRenderable_CreateInstance` (**0x004661b0**).  The leak's API on top
of it: `CharacterRenderable_SetPalette`, `_SetJointDamage`, `_EnableBone`, `_EnableAllBones`,
`_IsInInterior`.
Character primitives keep the layer `PrimGroup::SetShader` gave them (0/1/31) — the character
loader never calls a layer setter. **[V]**

### 5.7 `VehicleRenderable` / `VehicleContainer` / `VehiclePrimitive`

| class | vtable | ctor | size |
|---|---|---|---|
| `VehicleRenderable` | 0x00738704 | (inside 0x0047a180 / 0x0047a5b0) | 0xc0 |
| `VehicleContainer` | 0x00738794 | 0x00479e00 | 0xd8 |
| `VehiclePrimitive` | 0x0073874c | 0x00479370 | ~0x60 |

`VehicleRenderable` overrides 3 (dtor 0x479780), 9 (`Update` 0x479cc0), **10 `Display`
0x00478b90**:

```c
void VehicleRenderable::Display() {
    this->Hide();                             // vslot14 — drop all my Display_List nodes
    if (g_byte[0x7c0d33])                     // global vehicle-render toggle
        Renderable::Display();
}
```
It hides-then-redisplays **every frame** because the car's pose changes every frame and the
Display_List nodes cache the matrix. **[V]**

`VehicleContainer` is the interesting one and is *not* a clone of the other five containers:
```
+0x3c  sort key                       (written per-frame by Display, see below)
+0x44/+0x48  element array            (one VehiclePrimitive per body part)
+0x50..      part table               (init 0x465050)
+0xc0  pure3d::Pose *pose             a PRIVATE COPY (Pose::Copy) of the composite's pose   [V]
+0xc4  pure3d::CompositeDrawable *src (AddRef'd)
+0xc8  ?
+0xcc  bool  isFading                 (vslot17 set 0x478fb0 / vslot19 get 0x478fc0)
+0xd0  float fadeAmount               (vslot18 set 0x66cc30 / vslot20 get 0x478fd0)
+0xd4  bool
```
| slot | addr | what |
|---|---|---|
| 10 | 0x478fe0 | `Display(dl, prim)` — transform the pose origin into world space, store it as the node's sort position, then `DrawableContainer::Display` |
| 14 | 0x479a90 | `CalcBounds` — union of all parts' boxes, init ±1e11 (`0x5368d4a5` / `0xd368d4a5`) |
| 17..20 | see above | fading state |
| 25 | 0x478b20 | type mask 0x02000000 |

`VehiclePrimitive::ctor(comp, srcPrim, n, shader, bool, bool)` allocates an `n * 0x10` part array
at `+0x38` (`+0x3c = n`), keeps the source shader at `+0x5c` (slot 9 `GetShader` = 0x478b00,
`return this->+0x5c`), a second shader at `+0x58`, flag bits in `+0x50`, its own bounding sphere
at `+0x40..+0x4c`, and sets `layer = <source primitive's layer>` or **32** when `+0x50 & 2`. **[V]**

`VehicleRenderable` itself keeps `shaders[8]` at `+0x84`, `numShaders` at `+0xa4`, and four
pointers at `+0xb0..+0xbc` (composite / lodComposite / container / lodContainer **[?]** on which is
which).  The clone path 0x0047a180 builds **two** `VehicleContainer`s and **two**
`DisplayListElement`s, i.e. a car is a 2-element LOD chain and is the main user of
`Renderable::flags80 bit6` (`useLastElementMaxDist`, renderspine §2.2). **[V]**

Created by `renderer::VehicleRenderable_CreateInstance` (**0x00465e60**); the leak also calls
`VehicleRenderable_SetPalette`, `_ShaderSetup`, `_OverrideStatePropCullingDistances`.
`0x004653c0` (called from the game layer at 0x4aeaaf) walks a handle's element-0 drawable and
forces layer **44** on every primitive whose type mask is 1 and whose shader type is 0 — the
vehicle "wet/reflective" pass. **[?]**

### 5.8 `SkyRenderable` — vtable 0x007385e4, size 0x98, typeMask 1

Chunk + the 28/38/39 layer assignment: `renderables.md` §8.

| slot | addr | what |
|---|---|---|
| 3 | 0x4774e0 | dtor |
| 9 | 0x477500 | `Update(TimeInfo*)` — 344 bytes; a ms-tick accumulator in `+0x90`/`+0x94`, a counter in `+0x8c`, drives the billboard cycling **[?]** |
| 10 | **0x477450** | `Display()`: `if (g_byte[0x7c0c60]) Renderable::Display(); else Hide();` **[V]** |
| 12 | 0x438400 | `SetMatrix` no-op |

`Renderable::Display` has a special case for `typeMask == 1`: when the distance test is off it
still pushes the global fade into `Drawable::SetFadeAmount` (renderspine §2.2) — that is the sky
cross-fade between the clear and rainy sky boxes.
Created by `renderer::SkyRenderable_Create` (**0x004667c0**), which does **not** construct
anything: it looks the already-loaded `SkyRenderable` up in the inventory
(`DynamicCaster<SkyRenderable>`), adds it to the scene and wraps it in a `RenderableHandle`.

### 5.9 `LightingRenderable` — vtable 0x00737eac, size 0xa4, typeMask 0x400

Chunk + fields: `renderables.md` §5.

| slot | addr | what |
|---|---|---|
| 3 | 0x470240 | dtor (unregisters from the light manager) |
| 10 | **nullsub_2** | `Display` draws nothing **[V]** |

It is a pure lifetime object: its ctor (0x0046ff10) registers the `pure3d::LightGroup` with
`g_lightManager` (0x8111cc, `0x45fe10(group, kind)`), and for `kind == 0` also with `g[0x8113a0]`.
Created by the 0x08800007 loader and by `renderer::LightingRenderable_CreateInstance`
(**0x00466ce0**); the game also has `LightingZone_Create/_Destroy` and `LightsVolume` on top.

### 5.10 `ShadowRenderable` — vtable 0x007383d4, size 0xbc, typeMask 0x100

Chunk + fields + the layer-2 assignment: `renderables.md` §6.

| slot | addr | what |
|---|---|---|
| 3 | 0x474ea0 | dtor |
| 5 | 0x652580 | `Clone` → `return 0` |
| 9 | **0x474cd0** | `Update`: `if (isBuildingShadow && !g_byte[0x7c0b46]) 0x473cc0(this);` **[V]** |
| 10 | 0x4751c0 | `Display` (1440 bytes — projects the blob onto the ground) |

The ctor clears `flags80 & ~0x02` (no fade).  Created by the 0x08800008 loader and by
`renderer::ShadowRenderable_CreateInstance` (**0x00467210**), whose last argument is the leak's
`renderer::BUILDING_SHADOW` / `renderer::CAR_SHADOW` enum.
`GamePlayScene::AddRenderable` routes building shadows to a separate registry (0x475760).

### 5.11 The decal family

| class | vtable | ctor | size |
|---|---|---|---|
| `DecalRenderable` | **0x00737cac** (no IDA name; RTTI 0x007c0738) | 0x0046eb00 | 0x88 |
| `DecalContainer` | 0x00737bfc | 0x0046ea70 | 0x50 |
| `DecalPrimitive` | 0x00737bb4 | 0x0046d780 | 0x40 |
| `DecalPrimitive::CDecalDrawable` | 0x00737c78 | 0x0046d3a0 | — |
| `DecalPrimitive::CStaticDecalDrawable` | 0x00737c88 | 0x0046d3c0 / 0x46d450 | — |
| `DecalPrimitive::COceanDecalDrawable` | 0x00737c98 | 0x0046d4e0 | — |
| `DecalsHandle` | 0x007378a0 | 0x00464690 | 0x14 |

```c
DecalRenderable::DecalRenderable(int maxDecals)   // 0x46eb00
{ Renderable::Renderable(0); typeMask = 0x2000; container = new DecalContainer(maxDecals); }

DecalPrimitive::DecalPrimitive(int maxDecals)     // 0x46d780
{
    DrawablePrimitive::ctor();
    this->maxDecals /*+0x38*/ = maxDecals;
    this->decals   /*+0x3c*/ = new Decal[maxDecals];     // 0x24 bytes each, elem ctor 0x46d1d0
    for (i) { decals[i].+0x00 = 0; decals[i].+0x14 = 3; decals[i].+0x20 = 0; }
    this->layer /*+0x0c*/ = 0;                            // never used, see below
}
```
`DecalContainer::Display` is the immediate-mode one (0x4773e0), so the layer is irrelevant: the
whole pool is drawn by one `DecalPrimitive::Display()` (0x0046d860, straight pddi calls) from
`Display_List::Render`.  `DecalRenderable` is created **once** by `RenderManager::Init` and stored
at `RenderManager+0x30`; `maxDecals` comes from the render config struct (`cfg+0x24`).
Renderable vslots: 3 dtor 0x46ebb0, 5 `Clone` → `return 0`, 9 `Update` 0x46ebd0
(`prim->0x46ced0()` = age the decals out), 11 `RenderImmediate` 0x46d150
(`container->vslot10(0,0)`).
The three `C*DecalDrawable` structs have 3-slot vtables `{dtor, Build, ?}` and are the per-decal
geometry strategies: generic, static (baked into world geo), and ocean (follows the water surface).
`DecalsHandle` is *not* a renderable — it is `{vtable, Renderable* /*the singleton*/, uniqueId,
handleId (++g[0x8111f8]), pure3d::Shader*}`, i.e. **"a decal type"**.

Façades: `renderer::DecalRenderable_CreateInstance(shaderName, inventory)` = **0x00465c80**
(find the shader, then `new DecalsHandle(g_renderMgr->decalRenderable, shader)`);
`DecalRenderable_RemoveDecal(int *uid, bool)` = **0x0046ebe0** (calls 0x46d040 and sets `*uid = -1`);
`DecalRenderable_AddDecal` is the sibling at 0x0046ec20 → 0x46d0b0 family. **[V]**

### 5.12 The skid-mark family

| class | vtable | ctor | size |
|---|---|---|---|
| `SkidmarkRenderable` | 0x00738594 | 0x00476ad0 | 0x88 |
| `SkidmarkContainer` | 0x00738514 | 0x00476a30 | 0x50 |
| `SkidmarkPrimitive` | 0x007384cc | (inline in the container ctor) | 0x3c |
| `Skidmark` | 0x007384b8 | 0x00477340 | 0x684 |
| `SkidMarksHandle` | 0x00737824 | 0x00461980 | ≥0x14 |

Same shape as the decals, and the same conclusion: `SkidmarkContainer::Display` is the
immediate-mode 0x4773e0, `SkidmarkPrimitive::layer = 1` is never used, and the whole pool is
drawn from `Display_List::Render` after list 51 via `SkidmarkRenderable::RenderImmediate`
(slot 11 = 0x46d150, the same shared thunk the decals use).  **[V]**

`SkidmarkRenderable::ctor` notably **does not call `SetElement`** — the container is only reachable
through `+0x84`, so the renderable can never be drawn via the Display_List at all.
`SkidmarkPrimitive::Display` (0x00476090) iterates the **global array of 48 `Skidmark*` at
`0x008113f8`** and calls each one's vslot 0.  `Skidmark` vtable: `{0x4764c0 Draw,
0x476100 ?, 0x476480 GetBoundingSphere}`; fields `+0x638`, `+0x63c` bbox, `+0x664`, `+0x668 = 1.0`,
`+0x67c` fade distance (5700.0 when released, 0 when killed), `+0x680` active flag.
0x00476010 kills a skid ribbon by pointer, 0x00476050 releases it (sets `+0x67c = 5700.0f`).
`SkidMarksHandle` = `RenderableHandle` + `handleId (++g[0x8111f4])` + a writer state block at
`+0x10` (ctor 0x476120, dtor 0x476230).  Created at **0x00463ab0** (from `SkidmarkTemplate`).

### 5.13 `TraceFireRenderable` / `TraceFireHandle`

vtable 0x007386b4, ctor 0x00478490, size 0x8c, typeMask 2; handle vtable 0x007378a8,
ctor 0x004647b0. **[V]**

```c
TraceFireRenderable::TraceFireRenderable(int maxTracers)   // 0x478490
{ Renderable::Renderable(0); typeMask = 2;
  this->count /*+0x88*/ = maxTracers;
  this->tracers /*+0x84*/ = malloc(maxTracers * 0x4c); }   // 0x4c bytes per tracer
```
| slot | addr | what |
|---|---|---|
| 3 | 0x478540 | dtor |
| 9 | 0x478430 | `Update(TimeInfo*)` — per tracer: if `t->age` is in range, `t->age += dt * k` (k at 0x73c458) |
| 11 | 0x478560 | `RenderImmediate()` — the actual tracer strips |

Singleton at `RenderManager+0x38`, `maxTracers` from `cfg+0x28`.
`renderer::TraceFireRenderable_CreateInstance(meshName, inventory)` = **0x00465d50**: find a
`pure3d::CompositeDrawable` and wrap `{singleton, composite}` in a `TraceFireHandle`.
`TraceFireRenderable_AddTracer/_RemoveTracer` sit on top of the handle.

### 5.14 `OceanRenderable` / `OceanContainer` / `OceanPrimitive`

| class | vtable | ctor | size |
|---|---|---|---|
| `OceanRenderable` | 0x007380c4 | 0x00470ad0 | 0x88 |
| `OceanContainer` | 0x00737ffc | 0x00470a40 | 0x50 |
| `OceanPrimitive` | 0x0073807c | 0x00470820 | 0x48 |

```c
OceanRenderable::OceanRenderable(a, b, c)  // 0x470ad0 — three pure3d objects (reflection texture etc.)
{ Renderable::Renderable(1); typeMask = 0x40;
  container = new OceanContainer(a,b,c); SetElement(container, 0, false);
  flags80 &= ~3; }                          // NO distance test, NO fade — the ocean is always drawn
OceanContainer: sortKey = 0.5f
OceanPrimitive: layer = 27; +0x38 = pure3d ocean renderer object (ctor 0x689af0);
                +0x3c/+0x40/+0x44 = the three AddRef'd arguments; 0x689790 binds them.
```
| slot | addr | what |
|---|---|---|
| prim 8 | 0x4707a0 | `Display()` → `if (+0x38) jmp 0x689aa0` (the pure3d `win32OceanRenderer`) |
| prim 13 | **0x4707c0** | `CalcBounds()`: `camera->GetPosition(&this->sphere.centre); this->sphere.r = 100000.0f;` — the ocean follows the camera and is never frustum-culled **[V]** |
| renderable 9 | 0x470be0 | `Update(TimeInfo*)` |
| renderable 10 | 0x470810 | `Display` (thunk to the base) |
| renderable 12 | 0x438400 | `SetMatrix` no-op |

Created by `renderer::OceanRenderable_CreateInstance` (**0x00466940**; the string
`"oceanrenderable"` at 0x737904 is used as its inventory name).

### 5.15 `WakeRenderable` / `WakeContainer` / `WakePrimitive`

| class | vtable | ctor | size |
|---|---|---|---|
| `WakeRenderable` | 0x007388fc | 0x0047b4e0 | 0x88 |
| `WakeContainer` | 0x00738834 | 0x0047b450 | 0x50 |
| `WakePrimitive` | 0x007388b4 | 0x0047af60 | **0xe94** |

```c
WakeRenderable::WakeRenderable(pure3d::Shader *sh)   // 0x47b4e0
{ Renderable::Renderable(1); typeMask = 0x80;
  container = new WakeContainer(sh); SetElement(container, 0, true);
  SetElementDrawDist(0, 0.0f, 1000.0f, 100.0f);
  flags80 &= ~1; }                                    // distances stored but the test is off
WakeContainer: sortKey = 0.89f (0x3f63d70a)
WakePrimitive: 40 segments of 0x1c bytes at +0x4c; shader at +0xe64;
               +0xe84 = 2.0f, +0xe88 = 3.0f (width/length?); layer = 30; init 0x47ad10
```
`WakePrimitive::Display` (0x0047ab90) builds a triangle strip directly with pddi from the
per-segment position arrays at `+0x498` / `+0x678` / `+0x8d0` and a per-segment alpha byte array at
`+0x858`.  `WakeRenderable::Update` (0x0047b660) forwards to the wake simulation at 0x0047b060.
Created by `renderer::WakeRenderable_CreateInstance(shaderName, inventory, scene)` = **0x00466b10**.

### 5.16 `RainRenderable` — vtable 0x0073833c, ctor 0x004732c0, size 0x454, typeMask 0x800

The biggest renderable in the engine.  Fields set by the ctor: `+0xdc..+0xe0` five bools,
`+0xe4 = 0.2f`, `+0xe8`, `+0xec` bool, `+0xf0`, `+0x104 = 3`, `+0x108` (a drawable), `+0x150`,
`+0x3ac/+0x3b0`, `+0x3b4 = 1.0f`, `+0x3bc`, `+0x3c4 = -1`, `+0x450` bool. **[V]**

| slot | addr | what |
|---|---|---|
| 3 | 0x4734e0 | dtor |
| 9 | 0x473940 | `Update(TimeInfo*)` (350 bytes — advances the streaks) |
| 11 | 0x472af0 | `RenderImmediate()` (256 bytes) |

Created by `renderer::RainRenderable_CreateInstance(streaksName, splashesName, inventory, scene)`
= **0x00466e90** (it is the one factory besides the PlugIn one that writes `Renderable+0x7c`
itself).  `renderer::Renderer_GetRainRenderable()` returns the live one; the leak's
`RainRenderable_Enable(bool)` toggles it.

### 5.17 `MaskRenderable` — vtable 0x00737f6c, ctor 0x004704c0, size 0x84, typeMask 0x1000

```c
MaskRenderable::MaskRenderable(pure3d::Geometry *mesh, const char *name)   // 0x4704c0
{
    Renderable::Renderable(1);  typeMask = 0x1000;
    for (i in mesh->elements /*+0x44..+0x48, stride 0x10*/)
        mesh->elements[i].prim->layer = 36;          // 0x47051a
    SetElement(mesh, 0, false);
}
```
i.e. "take this mesh and put all of it on layer 36".  Layer 36 is rendered first in the outdoor
block (`0x45ccd0: 36, 42`), which is exactly where a stencil/depth mask volume belongs.
`pure3d::Geometry` is itself a `DrawableContainer`, which is why the loop works. **[V]**
Created by `renderer::MaskRenderable_CreateInstance(name, inventory, scene)` = **0x004673f0**
(`Find<pure3d::Geometry>`, `new MaskRenderable`, `SetName`, `AddRenderable`, `RenderableHandle`).
The leak never names this class, so the façade name is a guess. **[?]**

### 5.18 `ParticleEffectRenderable` — vtable 0x0073810c, ctor 0x00470d70, size 0xa8, typeMask 4

```
+0x84  pure3d::ParticleSystem *      (the effect)
+0x88  instance/emitter handle
+0x8c  bool   (from CompositeDrawable::GetFlag4)
+0x90  float  bias scale
+0x94 +0x98 +0x9c   floats
+0xa0  bool   hasBias
+0xa4  drawable (AddRef'd)
```
| slot | addr | what |
|---|---|---|
| 3 | 0x470ec0 | dtor |
| 9 | 0x470c30 | `Update(TimeInfo*)` |
| 10 | **nullsub_2** | `Display` draws nothing — the particle manager owns the drawing **[V]** |
| 12 | **0x00471250** | `SetMatrix(m)`: copy the matrix, then `particleMgr->0x67c110(system, handle, m)` and, if `+0xa0`, `0x67c340(system, handle, 9, +0x9c * +0x90)` — **`SetMatrix` == "play the effect here"** **[V]** |

This is why the leak's `ParticleEffectRenderable_Play(handle, transform)` is just
`Renderable_SetTransform`.  Created by `renderer::ParticleEffectRenderable_Create(name, inventory)`
= **0x00465840**; `_SetBias(handle, PARTICLE_SIZE, f)` and `_Reset` sit on top.

### 5.19 `NISRenderable` — vtable 0x00737fb4, ctor 0x004705b0, size 0x90, typeMask 0x20000

```c
NISRenderable::NISRenderable(...)          // 0x4705b0
{ Renderable::Renderable(?); typeMask = 0x20000;
  SetNumElements(...); SetElement(...);
  SetElementDrawDist(0, ..., ..., 10000.0f);
  flags80 &= ~1; }                          // no distance test — cut-scene actors never pop
```
| slot | addr | what |
|---|---|---|
| 3 | 0x470630 | dtor |
| 9 | 0x470650 | `Update(TimeInfo*)` |
| 10 | 0x470810 | `Display` (thunk to the base) |
| 13 | **0x470550** | `GetPosition(Vector*)` → `+0x84..+0x8c`, the NIS-driven position **[V]** |

Created by `renderer::NISRenderable_CreateInstance(compositeName, inv, scene, bool)` =
**0x00466310**.  Leak API: `NISRenderable_GetPose`, `NISRenderable_SetJointDamage`.

### 5.20 `PlugInRenderable` — vtable 0x00738154, ctor 0x004712d0, size 0x88, typeMask 0x40000

The only renderable with a **17-slot** vtable — it adds one virtual of its own. **[V]**

```c
PlugInRenderable::PlugInRenderable(void *plugin)   // 0x4712d0
{ Renderable::Renderable(0); this->plugin /*+0x84*/ = plugin; typeMask = 0x40000; }
```
| slot | addr | what |
|---|---|---|
| 3 | 0x4713c0 | dtor |
| 11 | **0x00471310** | `RenderImmediate()`: `if (plugin) plugin->vslot0();` **[V]** |
| 16 | **0x00471330** | `Clone()`: allocate 0x88, copy-construct via 0x4747f0, `plugin = 0`, typeMask = 0x40000 **[V]** |

This is the engine's extension point: game code registers a "renderable model"
(`renderer::SkyRenderableModel_Create`, `RainRenderableModel_Create`,
`LightingRenderableModel_Create`, `TraceFireRenderableModel_Create`,
`TerrainRenderableModel_Create`), looks it up with `renderer::RenderableModel_Find(name)` →
`renderer::ModelHandle*`, and instantiates it with
**`renderer::Renderable_CreateInstance(ModelHandle*, scene)` = 0x00464f00**, which wraps it in a
`PlugInRenderable`, sets `Renderable+0x7c = scene`, adds it to `scenes[scene]` and returns a
`RenderableHandle`. **[V]**

### 5.21 The four handle classes

```
renderer::RenderableHandle      vtable 0x0073781c   ctor 0x00461870   12 bytes      [V]
  +0x00 vtable  (1 slot: scalar-deleting dtor 0x00461920)
  +0x04 Renderable *renderable      AddRef'd; renderable->flags80 |= 0x08
  +0x08 u32 uniqueId                = renderable->+0x64
  dtor 0x004618d0:  renderable->flags80 &= ~0x08;  renderable->Hide() /*vslot14*/;
                    Release();  uniqueId = -1;  --g[0x8110ac];  ++g[0x8110a8]
  counters: g[0x8110a4] total created, g[0x8110ac] live, g[0x8110a8] destroyed

renderer::SkidMarksHandle       vtable 0x00737824   ctor 0x00461980   [V]
  ... RenderableHandle ... + 0x0c u32 handleId (++g[0x8111f4]) + 0x10 skid writer state
  NOTE: does NOT AddRef the renderable (it points at the singleton).
renderer::DecalsHandle          vtable 0x007378a0   ctor 0x00464690   0x14 bytes  [V]
  ... + 0x0c u32 handleId (++g[0x8111f8]) + 0x10 pure3d::Shader *shader (AddRef'd)
renderer::TraceFireHandle       vtable 0x007378a8   ctor 0x004647b0   0x14 bytes  [V]
  ... + 0x0c u32 handleId               + 0x10 pure3d::CompositeDrawable *
```
`renderer::Renderable_Destroy(&handle)` in the leak is just "delete the handle", which is why
destroying a handle also hides the renderable.

---

## 6. `renderer::` façade functions (the API the leaked game code calls)

All take `RenderManager::GetLock(n)` (0x004675b0) first; all `*_CreateInstance` end with
`scenes[i]->AddRenderable(r)` and `new RenderableHandle(r)`.  Verified by matching the class ctor /
`DynamicCaster<T>` each one calls against the argument shape in the leak. **[V]** unless noted.

| addr | proposed name |
|---|---|
| 0x00464f00 | `renderer::Renderable_CreateInstance(ModelHandle*, int scene)` → PlugInRenderable |
| 0x00465840 | `renderer::ParticleEffectRenderable_Create(name, inventory)` |
| 0x00465990 | `renderer::StatePropRenderable_CreateInstance(...)` |
| 0x00465c80 | `renderer::DecalRenderable_CreateInstance(shaderName, inventory)` → DecalsHandle |
| 0x00465d50 | `renderer::TraceFireRenderable_CreateInstance(meshName, inventory)` → TraceFireHandle |
| 0x00465e60 | `renderer::VehicleRenderable_CreateInstance(name, inventoryId, scene)` |
| 0x004661b0 | `renderer::CharacterRenderable_CreateInstance(name, inventory, scene)` |
| 0x00466310 | `renderer::NISRenderable_CreateInstance(compositeName, inv, scene, bool)` |
| 0x004664a0 | `renderer::ZonePkgRenderable_CreateInstance(packageName, inventory, scene)` |
| 0x00466600 | `renderer::PropRenderable_CreateInstance(name, min, max, inventoryId, scene)` **[?]** on the float order |
| 0x004667c0 | `renderer::SkyRenderable_Create(name, inventoryId, scene)` (lookup only, no ctor) |
| 0x00466940 | `renderer::OceanRenderable_CreateInstance(reflectionTexName, ...)` |
| 0x00466b10 | `renderer::WakeRenderable_CreateInstance(shaderName, inventory, scene)` |
| 0x00466ce0 | `renderer::LightingRenderable_CreateInstance(lightGroupName, inventoryId, scene)` |
| 0x00466e90 | `renderer::RainRenderable_CreateInstance(streaks, splashes, inventoryId, scene)` |
| 0x00467060 | `renderer::InstanceRenderable_CreateInstance(shape, lod, lod2, inv, scene, bucket, sway)` |
| 0x00467210 | `renderer::ShadowRenderable_CreateInstance(name, inv, scene, kind)` |
| 0x004673f0 | `renderer::MaskRenderable_CreateInstance(name, inventory, scene)` **[?]** name |
| 0x00463ab0 | `renderer::SkidMarks_CreateInstance(...)` → SkidMarksHandle **[?]** name |
| 0x00463db0 | `renderer::AddInstance(handle, pos, rot, scale, alpha, 1)` |
| 0x00463f80 | `renderer::InstanceRenderable_SetCullDistance(handle, dist, bool)` |
| 0x0046ebe0 | `renderer::DecalRenderable_RemoveDecal(int *uid, bool)` (thiscall on the DecalRenderable) |
| 0x0046ec10 | `renderer::DecalRenderable_?` → `prim->0x46d0b0` **[?]** |
| 0x004675b0 | `renderer::RenderManager::GetLock(int which)` |
| 0x00468490 | `renderer::RenderManager::Init(cfg, ...)` — pools + the 4 scenes + the 3 global pools |

---

## 7. Open questions

0. **One disagreement with `rendercore.md`/`names/rendercore.txt`:** that file has
   `0x00466600 = renderer::PlugInRenderable_CreateInstance ?`.  It is **`PropRenderable`**:
   0x004666f9 calls 0x004713e0, which writes vtable `0x0073819c` = `??_R4PropRenderable@renderer@@`.
   The PlugIn factory is `0x00464f00` (calls 0x004712d0 → vtable 0x00738154). **[V]**
   `names/renderable_classes.txt` otherwise defers to `names/rendercore.txt` for every
   `Renderable`-base / `RenderManager` / `Scene` / handle-plumbing address (those lines are
   commented out here so `mkidanames.py` reports no conflicts).
1. `Renderable` vtable slot 7 (base `return 0`) is never overridden and I did not find a caller.
2. `DrawablePrimitive` slots 10, 11, 16 and `DrawableContainer` slots 4, 8, 26 have no confirmed
   meaning — only the renderer's constant-returning overrides.
3. `VehiclePrimitive+0x50` flag bits: bit1 forces layer 32, bit0 comes from ctor arg 5, bit2 from
   arg 6.  Which is "damaged", which is "reflective"?
4. `0x004653c0` (layer 44 for type-mask-1 / shaderType-0 primitives) — confirmed mechanically, but
   the caller (0x4aeaaf, game layer) was not traced.
5. `0x00473cc0` is shared by `StatePropRenderable::Update` and `ShadowRenderable::Update`; not traced.
6. `RenderManager+0x24` is a fourth object whose `RenderImmediate` runs after list 15's second pass.
7. Whether `MaskRenderable` is the interior mask, the screen-space mask or something else — only
   the mechanism (everything → layer 36) is verified.
