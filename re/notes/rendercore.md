# The core of `renderer::` — what each class is actually for

PC retail, unpacked-image VAs (`re/dis.sh`). Companion to `renderspine.md` (frame order / culling /
display lists) and `renderables.md` (chunk loaders + subclasses). This file is about the **spine
objects**: RenderManager, Scene, Canvas, RenderFlowClient, Renderable, RenderableHandle, the flat
`renderer::` façade the game calls, the cameras, the occluders and the light manager.

**[V]** = read out of the disassembly. **[?]** = inferred / guess. Names ending in ` ?` are guesses.

The class names are not invented: the retail image keeps MSVC RTTI, so
`??_R4Renderable@renderer@@6B@` & co. are in `idb_names.txt` and every vtable is
`<addr of that record> + 4`. The *function* names come from the leaked game source
(`grep -rhoE 'renderer::[A-Za-z_]+' /u/aap/lib/pure3d/scarface_src`), which calls the engine
through a flat C-style façade; matching those 100-odd names against the façade functions in
`0x00461a70..0x004673f0` is what most of this file is.

---

## 0. TL;DR — the mental model

* **`RenderManager`** is *not* a renderer. It is the **owner**: 4 scenes, 2 canvases, 12 memory
  heaps, the environment/time-of-day manager, and the handful of singleton renderables
  (sky, decals, skidmarks, tracers). It has a 2-slot vtable (dtor only). One per game
  (`g_renderMgr = 0x008111d0`).
* **`Scene`** is *not* a scene graph. It is a **fixed-size array of `Renderable*` slots plus a
  typeMask filter and an enable flag**. Adding a renderable = "find the first nil slot". There are
  exactly 4, created at init, and they are rendered in index order 0,1,2,3.
* **`GamePlayScene`** is the one scene that owns a **`Display_List`** and therefore does deferred,
  sorted, batched drawing. The other three draw immediately (vslot 11) — they are the HUD /
  frontend / panel layers.
* **`Canvas`** is *not* a render target. It is a **`pure3d::View` + animated fog** (colour, near,
  far, density, transition time) + the enable flag. `Canvas::RenderScene` = "tick the fog fade,
  then tell the scene to render".
* **`RenderFlowClient`** is the **frame entry point** — one object, one virtual (`slot 2`) that the
  outer game loop calls once a frame. Its ctor *is* `renderer::Init`.
* **`Renderable`** is a **transform + an array of `DisplayListElement`s (drawable + draw-distance
  band + fade state) + a typeMask + visibility/fade state**. It never draws; it decides
  visible/faded and pushes its elements into the display list.
* **`RenderableHandle`** is a **weak reference with a generation check**: `{Renderable*, u32 uid}`
  where `uid` is a snapshot of `Renderable::uniqueId`. Every single façade call starts with
  `if (h && h->r && h->r->uniqueId == h->uid)`. This is the whole ownership story — the game never
  holds a `Renderable*`.
* **`gLightManager` (`0x008111cc`)** owns interiors: rooms, lighting zones, template lights and the
  occluder set. `Renderable::UpdateRoom` asks it which room it is in.

---

## 1. Globals

| addr | type | name | how |
|---|---|---|---|
| `0x008108a0` | `Display_List*` | `g_displayList` | set by `0x4589d0` from `GamePlayScene::ctor` **[V]** |
| `0x00810cdc` | u32 | `g_occStatSpheresTested` **[V]** |
| `0x00810ce0` | u32 | `g_occStatCulled` **[V]** |
| `0x00810ce8` | u32 | `g_occStatOccluderTests` **[V]** |
| `0x00810cf0` | u32 | `g_occStatPlaneTests` **[V]** |
| `0x00810cfc` | u32 | `g_occlusionEnabled` (must be exactly 1) **[V]** |
| `0x00810d04` | u32 | `g_numOccluders` **[V]** |
| `0x00810d10` | `OccluderInstance[]` | `g_occluders`, stride 0x98 **[V]** |
| `0x008110a0` | u32 | occluder auto-name counter (`"OccluderObject_%d"`) **[V]** |
| `0x008110a4` | u32 | `g_handlesCreated` **[V]** |
| `0x008110a8` | u32 | `g_handlesDestroyed` **[V]** |
| `0x008110ac` | u32 | `g_handlesLive` **[V]** |
| `0x008110b0` | bool | colour-effects enabled **[V]** |
| `0x008110b4/b8/bc` | u32/u32/`RenderableHandle**` | the **deferred-destroy queue**: count / capacity / array **[V]** |
| `0x008110c0` | char[] | screenshot filename buffer **[?]** |
| `0x008111c0..c3` | bool×4 | pending video-mode / screenshot request flags **[?]** |
| `0x008111c8` | MemoryAllocator | the rendering allocator (`CORE_MEMORY_ALLOC_RENDERING`) **[V]** |
| `0x008111cc` | `LightManager*` | `renderer::gLightManager` (ctor `0x460600`, 0x70 bytes) **[V]** |
| `0x008111d0` | `RenderManager*` | `g_renderMgr` **[V]** |
| `0x008111d4` | `pure3d::Camera*` | `g_defaultCamera` (0x104 bytes, ctor `0x68b850`, vtable `0x0076a684`) **[V]** |
| `0x008111d8` | `pure3d::Camera*` | **`g_renderCamera`** — what the `pure3d::View` actually draws with **[V]** |
| `0x008111dc` | `pure3d::Camera*` | **`g_cullCamera`** — what `Renderable::Display` culls against **[V]** |
| `0x008111e8` | u32 | frame-time accumulator **[?]** |
| `0x008111ec` | `RenderFlowClient*` | `g_renderFlowClient` (created by `0x4673c0`) **[V]** |
| `0x008111f4/f8/fc` | u32 | id counters for SkidMarksHandle / DecalsHandle / TraceFireHandle **[V]** |
| `0x00811200` | `Heap*` | cached `BuildingShadowHeap` **[V]** |
| `0x00811204` | `Heap*` | cached `ParticleEffectBlock` **[V]** |
| `0x00811208` | `RenderManager*` | second copy, written by the ctor **[V]** |
| `0x008113f4` | u32 | `Renderable::uniqueId` counter (wraps, skips −1) **[V]** |
| `0x008114f0/f4` | u32 | small / medium state-prop size thresholds (0x7d0 / 0x1770) **[V]** |
| `0x00811504` | u32 | vehicle pool item size (0x19c8) **[V]** |

> **Correction to `renderspine.md` §1.1**: `0x8111dc` is the *culling* camera and `0x8111d8` is the
> *rendering* camera; they are two distinct settable slots (`View_SetCullingCamera` /
> `View_SetRenderingCamera` in the leak), both initialised to `g_defaultCamera` at startup.

---

## 2. `renderer::RenderManager` — the owner

vtable `0x007379b8` (2 slots: `0x468470` scalar-deleting dtor, `0x643370`), ctor `0x467a00`,
size **0x104**. **[V]**

```
renderer::RenderManager                                   (0x104)
  +0x00  vtable
  +0x04  Scene**         scenes           // array of 4, allocated 0x10 bytes
  +0x08  Scene**         scenesEnd
  +0x0c  (spare)
  +0x10  Canvas*         canvas           // the main canvas
  +0x14  Canvas*         canvas2          // second canvas / "panel" view
  +0x18  bool            initialised
  +0x19  bool            skyEnabled
  +0x1a  bool            ?                // SetX 0x4676a0
  +0x1c  EnvManager*     env              // 0xc44 bytes, ctor 0x46bd80, Update 0x46c1d0  (§8)
  +0x20  DecalSystem*    decals ?         // 0x58 bytes, ctor 0x457800, Update 0x457fa0
  +0x24  <extra pass>                     // vslot 9 called each frame *only when indoors*
  +0x28  Renderable*     sky
  +0x2c  Renderable*     sky2             // the one EnableSky fades
  +0x30  DecalRenderable*      (ctor 0x46eb00, typeMask 0x2000)
  +0x34  SkidmarkRenderable*   (ctor 0x476ad0, typeMask 0x100000)
  +0x38  TraceFireRenderable*  (ctor 0x478490, typeMask 0x2)
  +0x3c  Renderable*     mainCharacter    // read by IsMainCharacterInInterior
  +0x40  bool            smallRenderBuffer ?
  +0x41  bool            videoModeFlag ?
  +0x44  (unused)
  +0x48  HeapInfo        heaps[12]        // stride 0x10
```

```
RenderManager::HeapInfo                                    (0x10)
  +0x00  MemoryAllocator allocator        // pre-filled for all 12 by 0x4676b0
  +0x04  Heap*           heap             // <-- what GetHeap() returns
  +0x08  u32             numItems
  +0x0c  (spare)
```

`RenderManager::GetHeap(int id)` = **`0x004675b0`**: `return *(Heap**)((u8*)this + 0x4c + id*0x10);`
**[V]**. The ids come straight out of `RenderManager::Init` (`0x468490`) and the heap-name strings
at `0x007379c0..`:

| id | heap | kind | capacity |
|---|---|---|---|
| 0 | `VehicleBlock` | pool | 9 × 0x19c8 |
| 1 | `SmallStatePropBlock` | pool | 0x2a × 0x7d0 |
| 2 | `MediumStatePropBlock` | pool | 0xd × 0x1770 |
| 3 | *(never created)* | | |
| 4 | `InstanceBlock` | pool | 0x67 × 0x1f4 |
| 5 | `ParticleEffectBlock` | linear | 0x41000 |
| 6 | `BuildingShadowHeap` | linear | 0x32000 |
| 7 | `ShadowBlock` | pool | 0x20 × 0x3e8 |
| 8 | `DecalBlock` | pool | cfg × 0x578 |
| 9 | `WakeBlock` | pool | 0xa × 0x1004 |
| 10 | `RenderableHandleBlock` | pool | **0x226 × 0x14** |
| 11 | `EmptyBlock` | pool | 2 × 0xa |

**This is the single most useful thing in the manager.** Every façade entry point opens a scoped
allocator (`0x43c710` push / `0x43c730` pop) on one of these heaps, so *which heap a function
pushes tells you which subsystem it belongs to*. `EmptyBlock` (id 11) is the "no special heap"
default used by all the plain accessors; `ParticleEffectBlock` (5) by the particle façade;
`RenderableHandleBlock` (10) by the handle allocator. 550 handles × 20 bytes is the hard cap on
live renderable handles, and all four handle classes (`RenderableHandle`, `SkidMarksHandle`,
`DecalsHandle`, `TraceFireHandle`) fit in those 20 bytes.

### 2.1 `RenderManager::Init` — `0x00468490(Config *cfg, MemoryAllocator alloc, ...)` **[V]**

```c
void RenderManager::Init(Config *cfg, MemoryAllocator alloc, ...)
{
    if (initialised) return;
    InitHeapTable(&heaps, alloc);                              // 0x4676b0, 12 entries
    heaps[6]  = CreateLinearHeap("BuildingShadowHeap", 0x32000, alloc, 0x40);
    g[0x811200] = heaps[6];
    heaps[0]  = CreatePool("VehicleBlock",         9,     0x19c8, alloc, 0x10);
    heaps[8]  = CreatePool("DecalBlock",           cfg->numDecals, 0x578, alloc, 0x40);
    DecalSystem_SetHeap(heaps[8]);                             // 0x46d140
    heaps[1]  = CreatePool("SmallStatePropBlock",  0x2a,  0x7d0,  alloc, 0x10);
    heaps[2]  = CreatePool("MediumStatePropBlock", 0xd,   0x1770, alloc, 0x10);
    heaps[4]  = CreatePool("InstanceBlock",        0x67,  0x1f4,  alloc, 0x10);
    heaps[5]  = CreateLinearHeap("ParticleEffectBlock", 0x41000, alloc, 0x10);
    g[0x811204] = heaps[5];  ParticleSystem_SetHeap(heaps[5]);
    heaps[7]  = CreatePool("ShadowBlock",          0x20,  0x3e8,  alloc, 0x10);
    heaps[9]  = CreatePool("WakeBlock",            0xa,   0x1004, alloc, 0x10);
    heaps[10] = CreatePool("RenderableHandleBlock",0x226, 0x14,   alloc, 0x10);
    heaps[11] = CreatePool("EmptyBlock",           2,     0xa,    alloc, 0x10);

    scenes = alloc(4 * sizeof(Scene*));
    scenes[0] = new GamePlayScene(0, cfg->maxGameplayRenderables, cfg->displayListNodes);  // 0x468f90
    scenes[2] = new Scene(2, cfg->maxScene2Renderables);                                    // 0x468ed0
    scenes[3] = new Scene(3, cfg->maxScene3Renderables);
    scenes[1] = new Scene(1, cfg->maxScene1Renderables);

    canvas  = new Canvas();  canvas ->SetBackgroundColour(7);   // 0x458210 / 0x4582e0
    canvas2 = new Canvas();  canvas2->SetBackgroundColour(7);

    skidmarks = new SkidmarkRenderable();                       // +0x34, 0x88 bytes, 0x476ad0
    decalsR   = new DecalRenderable(cfg->numDecals);            // +0x30, 0x88 bytes, 0x46eb00
    tracers   = new TraceFireRenderable(cfg->maxTracers);       // +0x38, 0x8c bytes, 0x478490

    renderContext->GetExtension(0x200200);                      // the instancing extension
    ...
    decalSystem = new DecalSystem();                            // +0x20, 0x58 bytes, 0x457800
    env         = new EnvManager();                             // +0x1c, 0xc44 bytes, 0x46bd80
    initialised = true;
}
```

### 2.2 `RenderManager::Update(TimeInfo *t)` — `0x00467810` **[V]**

```c
void RenderManager::Update(TimeInfo *t) {
    View_Begin(canvas->view);                       // 0x67dd40
    View_SetCamera?(GetCullingCamera());            // 0x67ba00
    if (!canvas->enabled) return;
    for (int i = 0; i < 4; i++) {
        if (i == 0) {
            scenes[0]->Update(t);                                  // vslot 4
            if (env)         Env_Update(env, t);                   // 0x46c1d0
            if (decalSystem) DecalSystem_Update(decalSystem, t);   // 0x457fa0
            if (gLightManager) LightManager_Update(t);             // 0x460130
            if (extraPass && env->IsCameraIndoors())               // 0x46a520
                extraPass->vslot9(t);
        } else {
            scenes[i]->Update(t);                                  // vslot 4
        }
    }
}
```

### 2.3 `RenderManager::Render(TimeInfo *t)` — `0x004689a0` **[V]**

```c
void RenderManager::Render(TimeInfo *t) {
    if (!canvas->enabled) return;
    if (GetNumSomething() >= 0 && g[0x859ba8])  g[0x859ba8]->Foo(GetCullingCamera(), 0x4679b0);
    View_Begin(canvas->view);                                      // 0x67ddb0
    for (int i = 0; i < 4; i++)
        canvas->RenderScene(scenes[i], t);                         // 0x458620
    View_End(canvas->view);                                        // 0x67df00
}
```

> **Correction to `renderspine.md` §1.4**: the scene order is **0, 1, 2, 3**, not `{0,2,3,1}`.
> The compiler emitted the `if (i==0) … else if (i==2) … else if (i==3) … else if (i==1)` chain in
> that order, but the loop counter `i` still selects `scenes[i]`, so scene *k* is rendered on
> iteration *k*. Scene 1 is additionally bracketed by `0x5e1bb0(0x11)` / `0x5e1bb0()`, which are
> `nullsub_2` in retail (a stripped profiler/GPU-marker scope) — that is the only thing special
> about it. `RenderManager::Update` has the identical shape, so updates are also 0,1,2,3.

### 2.4 Other `RenderManager` methods **[V]**

| addr | proposed name | what |
|---|---|---|
| `0x004675b0` | `GetHeap(int id)` | `heaps[id].heap` |
| `0x004676b0` | `InitHeapTable(MemoryAllocator)` | 12 × `{alloc, nil, 0}` |
| `0x004676e0` | `DumpHeap(int id) ?` | `0x5a4810(heaps[id])` |
| `0x00467700` | `PurgeHeap(int id) ?` | `0x50e560(heaps[id])` |
| `0x004675c0` | `EnableSky(bool)` | needs `sky` and `sky2`; sets `sky2->flags80 |= 2` then `SetToFadeIn/Out(3000)` on it, caches in `+0x19` |
| `0x00467950` | `SetSky(Renderable*, bool isSecondary)` | ref-counted store into `+0x28` / `+0x2c` |
| `0x00467610` | `EnableSmallRenderBuffer(bool) ?` | `display->vslot[0x6c](b)`, caches `+0x40` |
| `0x00467650` | `ApplyVideoMode(bool) ?` | rebuilds display settings, caches `+0x41` |
| `0x004676a0` | `SetX(bool) ?` | `+0x1a` |
| `0x00468470` | `~RenderManager` (scalar deleting) | |
| `0x00467a00` | `RenderManager::RenderManager()` | zeroes everything, `g[0x811208] = this` |

---

## 3. `renderer::Scene` and `renderer::GamePlayScene`

vtables `0x00737a80` / `0x00737a9c`. **[V]**

```
renderer::Scene                                   (0x1c)   ctor 0x468ed0(int index, int capacity)
  +0x00  vtable
  +0x04  Renderable**  slots          // `capacity` pointers, all nil
  +0x08  Renderable**  slotsEnd
  +0x0c  (spare, from ArrayThing::Init4 0x468d60)
  +0x10  bool          enabled        = true
  +0x14  i32           index          // 0..3; copied into Renderable::sceneIndex (+0x7c)
  +0x18  u32           typeMaskFilter = 0xffffffff

renderer::GamePlayScene : Scene                   (0x24)   ctor 0x468f90(index, capacity, dlNodes)
  +0x1c  float         sortBias ?     = -0.0f (0x80000000)
  +0x20  Display_List* displayList    // 0xcc bytes, ctor 0x45f110(dlNodes);
                                      // also published to g_displayList (0x8108a0) via 0x4589d0
```

| vslot | offset | `Scene` | `GamePlayScene` | meaning |
|---|---|---|---|---|
| 0 | +0x00 | `0x468f70` | `0x469050` | scalar deleting dtor |
| 1 | +0x04 | `0x468ac0` | `0x468cc0` | `AddRenderable(Renderable*)` |
| 2 | +0x08 | `0x468b20` | `0x468d10` | `RemoveRenderable(Renderable*)` |
| 3 | +0x0c | `0x468b80` | `0x468aa0` | `Render()` |
| 4 | +0x10 | `0x468c00` | `0x468c50` | `Update(TimeInfo*)` |
| 5 | +0x14 | `0x438400` | `0x468a90` | `SetName` (stub) |

Non-virtual: `0x00468a70 Scene::EnableTypes(bool on, u32 mask)` → `typeMaskFilter |= / &= ~mask`.

```c
void Scene::AddRenderable(Renderable *r) {              // 0x468ac0 [V]
    r->sceneIndex = this->index;                        // r->+0x7c = this->+0x14
    int n = slotsEnd - slots;
    for (int i = 0; i < n; i++)
        if (slots[i] == nil) { r->AddRef(); Release(slots[i]); slots[i] = r; return; }
    // silently dropped when the scene is full
}

void Scene::RemoveRenderable(Renderable *r) {           // 0x468b20 [V]
    for each slot: if (slots[i] == r) { Release(r); slots[i] = nil; return; }
}

void Scene::Render() {                                  // 0x468b80 [V] — immediate mode
    if (!enabled) return;
    bool old = renderContext->vslot[0x15c]();
    renderContext->vslot[0x158](false);                 // depth off?
    for each slot r:
        if (r && (r->typeMask & typeMaskFilter) && (r->flags80 & 0x10))
            r->vslot11();                               // base impl is nullsub_2
    renderContext->vslot[0x158](old);
}

void Scene::Update(TimeInfo *t) {                       // 0x468c00 [V]
    if (!enabled) return;
    for each slot r:
        if (r && (r->flags80 & 0x10) && (r->typeMask & typeMaskFilter))
            r->vslot9(t);                               // per-class pre-display
}

void GamePlayScene::Render() {                          // 0x468aa0 [V]
    if (enabled) displayList->Render();                 // vslot 9 = 0x45e680
}

void GamePlayScene::Update(TimeInfo *t) {               // 0x468c50 [V]
    if (!enabled) return;
    for each slot r: {
        if (!r) continue;
        Renderable::Tick(r, t);                         // 0x4740c0, NON-virtual, runs even when hidden
        if (!(r->flags80 & 0x10)) continue;             // isVisible
        if (!(r->typeMask & typeMaskFilter)) continue;
        r->vslot9(t);                                   // per-class pre-display
        r->vslot10();                                   // Renderable::Display
    }
    Display_List_SortAndWaterPass(displayList, t);       // 0x45ad10
}

void GamePlayScene::AddRenderable(Renderable *r) {      // 0x468cc0 [V]
    Scene::AddRenderable(r);
    if (r->typeMask == 0x100 /*Shadow*/ && r->+0x84) ShadowRenderable_OnAdd(r);     // 0x475760
    if (r->typeMask == 0x1   /*Sky*/)   g_renderMgr->SetSky(r, r->+0x88);           // 0x467950
}
// GamePlayScene::RemoveRenderable (0x468d10) is the exact mirror (0x475820 / SetSky(nil, ...)).
```

**Scene semantics.** Only scene 0 (`GamePlayScene`) has a display list, so only scene 0 gets sorted,
layered, batched drawing; scenes 1/2/3 call `vslot11` and draw right there. `StatePropManager` and
`renderer::CreateInstanceRenderable` always pass scene index **0**. The leak's enum names are
`GAMEPLAY_SCENE` (= 0 **[V]**) and `GUI_SCENE` (= 1 **[?]**); the other two are unnamed in the leak.

---

## 4. `renderer::Canvas`

vtable `0x007376e8` (1 slot: `0x458640` scalar deleting dtor), ctor `0x458210`, size **0x38**. **[V]**

```
renderer::Canvas                                       (0x38)
  +0x00  vtable
  +0x04  pure3d::View*  view              // 0x150 bytes, ctor 0x67e330
  +0x08  pure3d::Camera* camera           // set together with g_renderCamera
  +0x0c  bool           enabled     = true
  +0x10  u32            backgroundColour  = 0xff191919
  +0x14  float          fogFadeTimeLeft   = 0
  // --- current fog ---
  +0x18  u32            fogColour   = 0xff808080
  +0x1c  float          fogStart    = 100.0f
  +0x20  float          fogEnd      = 1000.0f
  +0x24  i32            fogDensity  = 200
  // --- target fog (what we are interpolating towards) ---
  +0x28  u32            fogColourTo
  +0x2c  float          fogStartTo
  +0x30  float          fogEndTo
  +0x34  i32            fogDensityTo
```

```c
void Canvas::SetFog(bool on, u32 colour, float start, float end, int density, float time)
{                                                                   // 0x458300 [V]
    view->+0xc0 = on;                                               // fog enable
    if (time > 0.0f && on) {                                        // animate
        fogColourTo = colour; fogStartTo = start; fogEndTo = end;
        fogDensityTo = density; fogFadeTimeLeft = time;
    } else {                                                        // snap
        fogColour = colour; fogStart = start; fogEnd = end; fogDensity = density;
        fogFadeTimeLeft = 0;
        view->+0x30 = colour; view->+0x34 = start; view->+0x38 = end; view->+0x3c = density;
    }
}

void Canvas::UpdateFog(TimeInfo *t)                                 // 0x458390 [V]
{
    if (fogFadeTimeLeft <= 0.0f) return;
    float k = t->dt / fogFadeTimeLeft;
    fogFadeTimeLeft -= t->dt;
    if (fogFadeTimeLeft <= 0.0f) {          // done: snap to target
        fogColour = fogColourTo; fogStart = fogStartTo;
        fogEnd = fogEndTo; fogDensity = fogDensityTo; fogFadeTimeLeft = 0;
    } else {                                // lerp each ARGB byte and each float by k
        lerp(fogColour.r/g/b, fogColourTo, k);
        fogStart   += (fogStartTo  - fogStart)  * k;
        fogEnd     += (fogEndTo    - fogEnd)    * k;
        fogDensity += (int)((fogDensityTo - fogDensity) * k);
    }
    view->+0x30 = fogColour; view->+0x34 = fogStart;
    view->+0x38 = fogEnd;    view->+0x3c = fogDensity;
}

void Canvas::RenderScene(Scene *s, TimeInfo *t) {                   // 0x458620 [V]
    if (!enabled) return;
    this->UpdateFog(t);
    s->vslot3();                                                    // Scene::Render
}
```

> **Correction to `renderspine.md` §1.4**: `0x458390` is `Canvas::UpdateFog`, not "set viewport/camera".

Other Canvas members: `0x4582e0 SetBackgroundColour(u32)` (`view->+0x2c`),
`0x4582f0 SetClearColour ?` (`view->+0x20`), `0x4581f0 ?`, `0x458690 ReadDisplaySettings ?`
(reads the `ScreenWidth`/`ScreenHeight`/`MSAA` config vars).

---

## 5. `renderer::RenderFlowClient` — the frame

vtable `0x0073782c`, ctor `0x00465520`, dtor `0x00464c80`, singleton `g[0x8111ec]` created by
`0x004673c0`. **[V]**

```c
RenderFlowClient::RenderFlowClient() {                              // 0x465520 [V]
    vtable = RenderFlowClient_vtbl;
    renderer::Init(8, IsSomething() ? 0x14 : 0x12);                 // 0x465120  <-- the real init
    g_renderMgr->canvas->SetBackgroundColour(6);
}
```

`renderer::Init` (**`0x00465120`**) is the whole bring-up: pddi/allocator setup, the deferred-destroy
array (`0x8110b4/b8/bc`), `new RenderManager` + `RenderManager::Init`, `new Camera` →
`g_defaultCamera` → `g_renderCamera` → `g_cullCamera`, `new LightManager` → `g[0x8111cc]`, and
finally `0x475ee0` and friends. **[V]**

`RenderFlowClient::OnFrame(TimeInfo *t)` = **`0x00465590`** (vslot 2). **[V]**

```c
void RenderFlowClient::OnFrame(TimeInfo *t)
{
    if (!g[0x7bfca0]) { ... }
    ScopedHeap scope(g[0x8111c8]);                    // the rendering allocator
    ... frame-time bookkeeping (g[0x8111e8], capped at 0x5265c00) ...
    if (g[0x8111c3]) {                                 // deferred video-mode/screenshot request
        if (g[0x8111c0]) g_renderMgr->EnableSmallRenderBuffer(...);
        if (g[0x8111c2]) g_renderMgr->ApplyVideoMode(...);
        if (g[0x8111c1]) g_renderMgr->SetX(...);
    }
    0x437770();
    g_renderMgr->Update(t);                            // 0x467810      §2.2
    0x458710();
    renderer::DestroyPendingRenderables();             // 0x464ac0      §6.3
    0x42a180();
    StatePropManager::ProcessPendingRenderableAdds();  // 0x4b23b0
    0x60c410();
    g_renderMgr->Render(t);                            // 0x4689a0      §2.3
    Present(...);                                      // 0x458740
    if (g[0x8111c3]) { ... grab the framebuffer, write <g[0x8110c0]>.bmp ... }   // [?]
}
```

The ordering matters: **all renderable destroys happen between Update and Render**, and all
state-prop renderable *adds* happen right after that — so a renderable created this frame is
already in a scene before `Render`, and a renderable destroyed this frame has already been pulled
out of its scene and out of the display list.

---

## 6. `renderer::Renderable` and `renderer::RenderableHandle`

### 6.1 Layout **[V]**

vtable `0x0073838c` (16 slots), ctor `0x00474ba0(int numElements)`, base size **0x84**.

```
renderer::Renderable : pure3d::Entity                    (0x84)
  +0x00  vtable
  +0x04  refCount
  +0x08  (Entity)
  +0x0c  0
  +0x10  Room*                room          // from gLightManager, ref-counted (UpdateRoom)
  +0x14  Matrix               matrix        // 0x40, identity in ctor; row3 (+0x44) = position
  +0x54  i32                  typeMask      // one bit per class, −1 in the base ctor
  +0x58  DisplayListElement*  elements
  +0x5c  DisplayListElement*  elementsEnd   // stride 0x30
  +0x60  (capacity)
  +0x64  u32                  uniqueId      // from g[0x8113f4]++, never −1
  +0x68  float                fadeRate      // UpdateFade steps by 1000/fadeRate per second
  +0x6c  float                fade          // 0 = fully opaque, 1 = fully gone
  +0x70  float                fade2         // second channel, max()'d with fade
  +0x74  float                fadeTarget    = 1.0f
  +0x78  float                timeSinceDrawn
  +0x7c  i32                  sceneIndex    = 5 in the ctor; overwritten by Scene::AddRenderable
  +0x80  u8                   flags80       // ctor: (old & 0x10) | 0x03
         0x01 doDistanceTest    0x02 doFade        0x04 fadeDirection (1 = fading out)
         0x08 hasHandle         0x10 isVisible     0x20 statePropFitsInPool
         0x40 shareLastElementFarDistance          0x80 useBoxBoundsFromPose
  +0x81  u8                   flags81
         0x01 isInsideRoom (interior)              0x02 matrixDirty
```

```
renderer::DisplayListElement                             (0x30)
  +0x00  float drawDistMin   = 0.0f
  +0x04  float drawDistMax   = 100.0f
  +0x08  float drawDistFade  = 10.0f
  +0x0c  DisplayListPrimitive prim   (0x20)
  +0x2c  bool  isFading      = false
```

### 6.2 vtable — all 16 slots, with what overriding them means **[V]**

| # | off | base impl | signature / semantics |
|---|---|---|---|
| 0 | 0x00 | `0x40f390` | `AddRef()` |
| 1 | 0x04 | `0x40f3a0` | `Release()` |
| 2 | 0x08 | `0x673c00` | `GetRef()` |
| 3 | 0x0c | `0x474aa0` | `~Renderable()` — **every subclass overrides**; frees its own drawables |
| 4 | 0x10 | `0x652590` | `Clone()` stub |
| 5 | 0x14 | `0x46d130` | `Entity::Clone` |
| 6 | 0x18 | `0x438400` | `SetName(const char*)` — `ret 4`, retail keeps no names |
| 7 | 0x1c | `0x652580` | `GetName()` stub |
| 8 | 0x20 | `0x473c20` | **`SetVisible(bool)`** — toggles `flags80 & 0x10` *and* pushes/pops every element's prim |
| 9 | 0x24 | `0x438400` | **`Update(TimeInfo*)` / pre-display** — the per-class hook the scene calls *before* `Display`. Base is a stub. This is where animation, wind, skinning, decal ageing happen. |
| 10 | 0x28 | `0x4740f0` | **`Display()`** — the visibility/fade/cull state machine (§6.5). Overriding it (WorldGeo, Instance) means "I cull my sub-primitives myself". |
| 11 | 0x2c | `0x5e1bb0` | **`DisplayImmediate()`** — used only by the non-gameplay scenes. Base is `nullsub`. |
| 12 | 0x30 | `0x473b40` | `SetMatrix(const Matrix*)` — memcpy 0x40 + set `matrixDirty` |
| 13 | 0x34 | `0x473c00` | `GetPosition(Vector*)` — matrix row 3 |
| 14 | 0x38 | `0x473c80` | **`Hide()`** — `RemoveFromList()` on every element (drops the display-list nodes) |
| 15 | 0x3c | `0x559000` | **`GetDistanceRefPos(Vector*)`** — returns false in the base; WorldGeo (`0x4714e0`) returns its `otherPosition`. This is *the* hook for "measure distance from somewhere other than my origin". |

### 6.3 Non-virtual members `0x473aa0..0x474100` **[V]**

| addr | proposed name | body |
|---|---|---|
| `0x00473aa0` | `SetToFadeIn(float rateMs)` | `flags80 &= ~0x04; fade = 1; fadeTarget = 0; fadeRate = rate` → appears over `rate` ms |
| `0x00473ac0` | `SetToFadeOut(float rateMs)` | `flags80 |= 0x04; fade = 0; fadeTarget = 1; fadeRate = rate` |
| `0x00473ae0` | `FadeInTo(float rate, float target)` | if `target < fade`: clear dir, `fadeTarget = target`, `fadeRate = (fade-target)*rate` |
| `0x00473b10` | `FadeOutTo(float rate, float target)` | if `target > fade`: set dir, `fadeTarget = target`, `fadeRate = (target-fade)*rate` |
| `0x00473b40` | `SetMatrix` (vslot 12) | |
| `0x00473b60` | `~Renderable` body (Entity part) | |
| `0x00473b70` | `Reset()` | clears `isInsideRoom`/`fadeDirection`, `fade = fade2 = timeSinceDrawn = 0`, `fadeTarget = 1`, `SetVisible(true)`, and per element `isFading = false; drawable->SetFading(false); drawable->SetFadeAmount(0)` |
| `0x00473c00` | `GetPosition` (vslot 13) | |
| `0x00473c20` | `SetVisible` (vslot 8) | |
| `0x00473c80` | `Hide` (vslot 14) | |
| `0x00473cc0` | `UpdateRoom()` | `room = gLightManager->FindRoom(position, &inside); flags81.bit0 = inside` (ref-counted) |
| `0x00473d40` | `SetFade2(float)` | clamp to [0,1] into `+0x70` |
| `0x00473d90` | `UpdateFade(float dt) -> float` | see `renderspine.md` §2.3; returns `max(fade, fade2)` |
| `0x00473e70` | `SetElement(Drawable*, int i, int)` | `d->CalcBounds()`, `prim.SetParent(this)`, `prim.SetDrawable(d, i)`, drawDist = {0, 100, 10}, `isFading = false` |
| `0x00473f00` | `SetElementDrawDist(int i, float min, float max, float fade)` | |
| `0x00473f50` | `GetElementDrawDist(int i, float*, float*, float*)` | |
| `0x00473fc0` | `GetElementDrawable(int i)` | |
| `0x00473fe0` | `GetNumElements()` | `(end - begin) / 0x30` |
| `0x00474000` | `CopyElements(src, srcEnd, dst)` | used when resizing/cloning |
| `0x00474ac0` | `SetNumElements(int n)` | realloc + zero drawDist/isFading |
| `0x00474ba0` | `Renderable::Renderable(int n)` | `typeMask = -1`, `uniqueId = g[0x8113f4]++`, identity matrix, `SetVisible(true)`, `SetNumElements(n)` |
| `0x004740c0` | **`Tick(TimeInfo *t)`** | `timeSinceDrawn += t->dt; if (flags80 & 2) UpdateFade(t->fadeDt)` — note it reads **two different** fields of `TimeInfo`: `+0x08` for the age and `+0x0c` for the fade step |
| `0x004740f0` | **`Display()`** | see `renderspine.md` §2.2 (verified, unchanged) |

### 6.4 `RenderableHandle` and friends **[V]**

```
renderer::RenderableHandle                      (0x0c, allocated from RenderableHandleBlock)
  +0x00  vtable                  0x0073781c   (1 slot: 0x461920 scalar deleting dtor)
  +0x04  Renderable*  renderable
  +0x08  u32          uid          // snapshot of renderable->uniqueId; 0xffffffff once released

renderer::SkidMarksHandle : RenderableHandle    (vtable 0x00737824, ctor 0x461980, dtor 0x461a10)
  +0x0c  u32          id           // from g[0x8111f4]++
  +0x10  <sub-object> (0x476120 ctor / 0x476230 dtor)
renderer::DecalsHandle    : RenderableHandle    (vtable 0x007378a0, ctor 0x464690, dtor 0x464740, id g[0x8111f8])
renderer::TraceFireHandle : RenderableHandle    (vtable 0x007378a8, ctor 0x4647b0, dtor 0x464860, id g[0x8111fc])
```

```c
RenderableHandle::RenderableHandle(Renderable *r) {          // 0x461870 [V]
    vtable = ...;  renderable = nil;
    uid = r->uniqueId;
    r->AddRef();  Release(renderable);  renderable = r;
    r->flags80 |= 0x08;                                      // "a handle owns me"
    g_handlesCreated++;  g_handlesLive++;
}

void RenderableHandle::Detach() {                            // 0x4618d0 [V]
    if (renderable) renderable->flags80 &= ~0x08;
    renderable->Hide();                                      // vslot 14
    renderable->Release();  renderable = nil;
    g_handlesLive--;  g_handlesDestroyed++;
    uid = 0xffffffff;
}
```

**The validity check, repeated verbatim in ~90 façade functions:**

```c
static inline Renderable *Deref(RenderableHandle *h, i32 wantMask) {
    if (!h) return nil;
    Renderable *r = h->renderable;
    if (!r) return nil;
    if (r->uniqueId != h->uid) return nil;     // the renderable was destroyed and its slot reused
    if (wantMask && r->typeMask != wantMask) return nil;
    return r;
}
```

That is why `Renderable::uniqueId` exists and why it wraps but skips `−1`: it is a **generation
counter for a weak handle**, nothing else.

`renderer::DestroyPendingRenderables()` — **`0x00464ac0`** **[V]** — drains the queue built by
`Renderable_Destroy`:

```c
void DestroyPendingRenderables() {
    for (i = 0; i < g_destroyCount; i++) {
        RenderableHandle *h = g_destroyQueue[i];
        if (!h) continue;
        Renderable *r = h->renderable;
        switch (r->typeMask) {
        case 0x2000: g_renderMgr->decalsR->RemoveOwner(h); break;        // 0x46ec10
        case 0x8000: ZonePkg_Unload(); h->vslot0(1); break;              // 0x471b60
        case 0x0100: ShadowRenderable_Detach(r, 0);          goto unlink;// 0x474df0
        case 0x20000: if (g_renderMgr->mainCharacter == r) { Release; = nil; }
                      NISRenderable_Detach(r, 0);            goto unlink;// 0x470590
        case 0x0020: if (g_renderMgr->mainCharacter == r) { Release; = nil; }
                      CharacterRenderable_Detach(r, 0);      goto unlink;// 0x46c9d0
        default: ...
        unlink:   g_renderMgr->scenes[r->sceneIndex]->RemoveRenderable(r);   // vslot 2
                  h->vslot0(1);                                              // delete the handle
        }
    }
    g_destroyCount = 0;
}
```

Note it uses `r->sceneIndex` (`+0x7c`) to find the owning scene — the only reason that field exists.

---

## 7. The flat `renderer::` façade

Everything the game calls lives in **`0x00461a70 .. 0x004673f0`** as free functions. The shape is
always the same:

```c
RetType renderer::<Class>Renderable_<Verb>(RenderableHandle *h, args...)
{
    ScopedHeap scope(g_renderMgr->GetHeap(HEAP));      // 0x43c710 / 0x43c730
    Renderable *r = Deref(h, TYPEMASK);
    if (!r) return 0;
    return r-><method>(args...);
}
```

so `(heap id, typeMask, callee)` identifies the function, and the leak's name list supplies the
name. `HEAP` is `EmptyBlock` (11) for almost everything, `ParticleEffectBlock` (5) for particles
and `RenderableHandleBlock` (10) for the handle allocator.

### 7.1 Generic `Renderable_*` (no heap scope, uid-checked) **[V for the bodies]**

| addr | name | body |
|---|---|---|
| `0x00462170` | `Renderable_SetVisible(h, bool)` | `r->vslot8(b)` |
| `0x004621a0` | `Renderable_IsVisible(h) ?` | `(flags80 >> 4) & 1` |
| `0x004621d0` | `Renderable_DisableLOD(h, bool)` | toggles `flags80 & 0x40` (share last element's far distance) |
| `0x00462200` | `Renderable_SetToFadeIn(h, rate, level) ?` | `r->FadeInTo` (0x473ae0) |
| `0x00462230` | `Renderable_SetToFadeOut(h, rate, level) ?` | `r->FadeOutTo` (0x473b10) |
| `0x00462260` | `Renderable_FadeIn(h, timeMs) ?` | `r->SetToFadeIn` (0x473aa0) |
| `0x004622c0` | `Renderable_FadeOut(h, timeMs) ?` | `r->SetToFadeOut` (0x473ac0) |
| `0x00462290` | `Renderable_SetFadeLevel(h, float) ?` | `r->SetFade2` (0x473d40) |
| `0x004622f0` | `Renderable_IsFullyFaded(h) ?` | direction-aware test of `fade`/`fade2` against 1.0 |
| `0x00462340` | **`Renderable_Destroy(RenderableHandle **h)`** | validates then `g_destroyQueue[g_destroyCount++] = *h` — **destruction is deferred to the next frame** |
| `0x00462370` | `Renderable_SetTransform(h, Matrix*)` | `r->vslot12(m)` unless `typeMask == 0x2000` (decals ignore transforms) |
| `0x004623a0` | `Renderable_GetTimeSinceLastDrawn(h) ?` | `timeSinceDrawn * const` |

### 7.2 Per-class façade (heap 11 unless noted) **[V for heap/typeMask/callee, names [?]]**

| addr | typeMask | callee | proposed name |
|---|---|---|---|
| `0x004623d0` | 4 (heap 5) | `0x470f80` | `ParticleEffectRenderable_Play(h, Matrix*)` |
| `0x00462440` | 4 (heap 5) | `0x470cd0` | `ParticleEffectRenderable_Reset(h)` |
| `0x004624b0` | 4 (heap 5) | `0x471110` | `ParticleEffectRenderable_SetBias(h, bias, float)` |
| `0x00462530` | 4 (heap 5) | `0x470cb0` | `ParticleEffectRenderable_IsPlaying(h) ?` |
| `0x004625d0` | 0x10 | `0x473fc0` | `StatePropRenderable_GetDrawable(h) ?` |
| `0x00462680` | 0x10 | `0x477d30` | `StatePropRenderable_?(h, x)` |
| `0x00462720` | 0x10 | `0x477e10` | `StatePropRenderable_?(h)` |
| `0x004627c0` | 0x10 (heap `g[0x8111c8]`) | `0x477aa0` | `StatePropRenderable_CreateUniquePose(h)` |
| `0x00462830` | 0x2000 | `0x46ec30` | `DecalRenderable_AddDecal(h, tri, pos, up, size, bool)` → decal uid |
| `0x004628e0` | 0x2000 | `0x46ebe0` | `DecalRenderable_RemoveDecal(h, uid, bool)` |
| `0x00462960` | 2 | `0x478720` | `TraceFireRenderable_AddTracer(h, start, end)` → tracer id |
| `0x00462a30` | 2 | `0x438400` | `TraceFireRenderable_RemoveTracer(h, id)` — **a no-op stub in retail** |
| `0x00462aa0` | 0x4000 | `0x479920` | `VehicleRenderable_ShaderSetup(h, VehicleShaderSettings /*0x70 by value*/)` |
| `0x00462b30` | 0x4000 | `0x478b80` | `VehicleRenderable_?(h)` |
| `0x00462bd0` | 0x4000 | `0x4797f0` | `VehicleRenderable_?(h, a, b)` |
| `0x00462c50` | 0x4000 | `0x4798a0` | `VehicleRenderable_?(h, a, b)` |
| `0x00462cd0` | 0x4000 | `0x479900` | `VehicleRenderable_?(h, a, b)` |
| `0x00462d50` | 0x4000 | `0x478bb0` | `VehicleRenderable_?(h)` |
| `0x00462df0` | 0x4000 | `0x479a40` | `VehicleRenderable_SetPalette(h, index)` |
| `0x00462e70` | 0x4000 | — | `VehicleRenderable_?(h, bool)` — toggles `flags80 & 1` |
| `0x00462ef0` | 0x4000 + 0x10 | `0x478bd0` | `VehicleRenderable_OverrideStatePropCullingDistances(vehicleH, statePropH)` |
| `0x00462f20` | 0x20 | `0x46c9d0` | `CharacterRenderable_?(h, x)` |
| `0x00462f90` | 0x20 | — | `CharacterRenderable_IsInInterior(h)` → `flags81 & 1` |
| `0x00462fb0` | 0x20 | `0x46c4a0` | `CharacterRenderable_?(h, x)` → bool |
| `0x00463050` | 0x20 | `0x46c4f0` | `CharacterRenderable_EnableBone(h, joint, bool)` |
| `0x004630d0` | 0x20 | `0x46c540` | `CharacterRenderable_EnableAllBones(h)` |
| `0x00463140` | 0x20 | `0x46c570` | `CharacterRenderable_SetJointDamage(h, joint, dmg, bool)` |
| `0x004631c0` | 0x20 | `0x46c5c0` | `CharacterRenderable_?(h)` |
| `0x00463230` | 0x20 | `0x46c420` | `CharacterRenderable_?(h)` |
| `0x004632d0` | 0x20 | `0x46c440` | `CharacterRenderable_SetPalette(h, index)` |
| `0x00463340` | 0x20 | — | `CharacterRenderable_?(h, x)` |
| `0x004633c0` | 0x20000 | `0x470590` | `NISRenderable_?(h, x)` |
| `0x00463440` | 0x20000 | `0x4705a0` | `NISRenderable_GetPose(h)` |
| `0x004634e0` | 0x20000 | `0x470730` | `NISRenderable_SetJointDamage(h, joint, dmg, bool)` |
| `0x00463560` | 0x100 | `0x474e40` | `ShadowRenderable_?(h, a, b)` |
| `0x004635d0` | 0x100 | — | `ShadowRenderable_?(h, x)` |
| `0x00464050` | 0x100 | `0x474df0` | `ShadowRenderable_?(h, x)` |
| `0x00463650` | 0x40 | `0x470bc0` | `OceanRenderable_?(h)` |
| `0x00463720` | 0x80 | `0x47b5e0` | `WakeRenderable_?(h, a, b)` |
| `0x004637a0` | 0x80 | `0x47b620` | `WakeRenderable_?(h, x)` |
| `0x00463820` | 0x80 | `0x47b600` | `WakeRenderable_?(h, x)` |
| `0x004638a0` | 0x80 | `0x47b640` | `WakeRenderable_?(h, x)` |
| `0x00463b70` | 0x100000 | `0x475f30` | `SkidmarkRenderable_?(h)` |
| `0x00463c10` | 0x100000 | `0x4769f0` | `SkidmarkRenderable_?(h, x)` |
| `0x00463c90` | 0x100000 | `0x477190` | `SkidmarkRenderable_AddSkidmark(h, 6 args) ?` |
| `0x00463d30` | 0x100000 | `0x4762f0` | `SkidmarkRenderable_?(h, x)` |
| `0x00463db0` | 0x200 | `0x46fba0` | **`renderer::AddInstance(h, a, b, c, d)`** → instance id |
| `0x00463e60` | 0x200 | `0x46fbc0` | `renderer::RemoveInstance(h, id) ?` (no heap scope) |
| `0x00463e80` | 0x200 | `0x46fc20` | `InstanceRenderable_?(h, a)` |
| `0x00463f00` | 0x200 | `0x46fc60` | `InstanceRenderable_?(h, a)` |
| `0x00463f80` | 0x200 | `0x46fbf0` | `renderer::SetCullDistance(h, visibilityEnd, flag)` |
| `0x00464000` | — | `0x46f7a0` | `InstanceRenderable_CalculateCullDistance(Box /*6 floats by value*/)` |
| `0x00464040` | — | — | `SetInstanceLODScale(float) ?` → `g[0x7c0778]` |

### 7.3 Creation (`*_CreateInstance` / `*_Create`) **[V for the shape, names [?]]**

All have the same body: `GetHash(name)` → `inventory->Find<T>(uid)` → `memory_alloc2(size)` → ctor →
`scenes[sceneArg]->AddRenderable(r)` → `r->vslot10()` → `new RenderableHandle(r)`.

| addr | ctor it calls | proposed name |
|---|---|---|
| `0x00465840` | `0x470d70` (ParticleEffect) | `ParticleEffectRenderable_Create(name, inventory)` |
| `0x00465990` | `0x477b20` (StateProp clone) | `StatePropRenderable_CreateInstance(...)` |
| `0x00465bc0` | — | `RenderableModel_Find(name)` |
| `0x00465c80` | `0x464690` (DecalsHandle) | `DecalRenderable_CreateInstance(shaderName, inventory)` |
| `0x00465d50` | `0x4647b0` (TraceFireHandle) | `TraceFireRenderable_CreateInstance(meshName, inv, scene)` |
| `0x00465e60` | `0x47a180` (Vehicle) | `VehicleRenderable_CreateInstance(modelName, inv, scene)` |
| `0x00466040` | `0x479920` | `VehicleRenderable_ShaderSetup(modelName, settings)` — model-level variant |
| `0x004660f0` | `0x479190` | `VehicleRenderable_?(modelName)` |
| `0x004661b0` | `0x46c650` (Character) | `CharacterRenderable_CreateInstance(name, inv, scene, ?)` |
| `0x00466310` | `0x4705b0` (NIS) | `NISRenderable_CreateInstance(name, inv, scene, bool)` |
| `0x004664a0` | `0x471b10` (ZonePkg) | `ZonePkgRenderable_CreateInstance(name, inv, scene)` |
| `0x00466600` | `0x4713e0` (PlugIn) | `TerrainRenderableModel_Create(...) ?` / `PlugInRenderable_CreateInstance ?` |
| `0x004667c0` | — | `SkyRenderable_Create(modelName, inv, scene)` |
| `0x00466940` | `0x470ad0` (Ocean) | `OceanRenderable_CreateInstance(reflTex, …, 5 args)` |
| `0x00466b10` | `0x47b4e0` (Wake) | `WakeRenderable_CreateInstance(shaderName, inv, scene)` |
| `0x00466ce0` | `0x46fe70` (Lighting) | `LightingRenderable_CreateInstance(lightGroupName, inv, scene)` |
| `0x00466e90` | `0x4732c0` (Rain) | `RainRenderable_CreateInstance(streaks, splashes, inv, scene)` |
| `0x00467060` | `0x46f8e0` (Instance) | `renderer::CreateInstanceRenderable(shape, lod, lod2, inv, scene, bucket, sway)` |
| `0x00467210` | `0x474c70` (Shadow) | `ShadowRenderable_CreateInstance(modelName, inv, scene, shadowType)` |
| `0x004673f0` | `0x4704c0` (Mask) | `MaskRenderable_CreateInstance(...)` |

### 7.4 View / Canvas façade **[V for the bodies]**

| addr | proposed name |
|---|---|
| `0x00461ac0` | `View_GetRenderingCamera()` → `g[0x8111d8]` |
| `0x00461ad0` | `View_GetCullingCamera()` → `g[0x8111dc]` (this is what `Renderable::Display` uses) |
| `0x00464ca0` | `View_SetRenderingCamera(Camera*)` — also stores into `canvas->camera` and the `pure3d::View` |
| `0x00464d00` | `View_SetCullingCamera(Camera*)` |
| `0x00461ed0` | `View_SetFog(FogParameters)` — applies to **both** canvases and caches in `env+0xc28..0xc30` |
| `0x00461f70` | `View_GetFog(FogParameters*)` — 0x18 bytes from `env+0xcc` |
| `0x00461fd0` | `View_EnableDepthOfField(DOFParameters /*0x34*/)` → pddi ext `0x103` |
| `0x00462100` | `View_GetDepthOfField ?` → pddi ext `0x103` |
| `0x00462030` | `View_EnableColourEffects(bool)` → pddi ext `0x103`, caches `g[0x8110b0]` |
| `0x00462050` | `View_IsColorEffectsEnabled()` |
| `0x00462060` | `View_SetColourEffectsParameters(ColourEffectsParameters /*0x1c*/)` — also caches at `0x7bfca8` |
| `0x00461f50` | `View_GetColourEffectsParameters(...)` — 0x20 bytes from `env+0xac` |
| `0x004620a0` | `View_GetEnvParameters ?` — 0x1c bytes from `env+0x118` |
| `0x00462120` | `View_SetEnvParameters ?` (0x30 bytes) |
| `0x00461e00` | `View_SetBackGroundColour(u32)` |
| `0x00461e20` | `View_SetClearColour ?` |
| `0x00461e60` | `View_MapCameraCoordinates(Vector, Vector*) ?` |
| `0x00461ea0` | `ColourEffectsParameters::ctor ?` (0x20) |
| `0x00461fb0` | `DOFParameters::ctor ?` (0x18) |
| `0x00462000` | `FogParameters::ctor ?` (0x1c) |
| `0x004620c0` | `EnvParameters::ctor ?` (0x30) |
| `0x00464e00` | `Scene_Enable(int scene, bool)` → `scenes[scene]->enabled` |
| `0x00464e20` | `Scene_EnableTypes(int scene, bool, u32 mask)` |
| `0x00461c70` | `EnableSmallRenderBuffer(bool) ?` |
| `0x00461d40` | `IsMainCharacterInInterior()` → `g_renderMgr->mainCharacter->flags81 & 1` |
| `0x00461d60` | `IsCameraInInterior() ?` — `gLightManager->FindRoom(renderCamera->GetPosition())` |
| `0x00464660` | `RenderManager::SetMainCharacter(Renderable*)` (`+0x3c`, ref-counted) |
| `0x00464f00` | `Renderable_CreateInstance(...) ?` — the only creator that scopes on `RenderableHandleBlock` |

### 7.5 Time-of-day / environment façade (all route through `g_renderMgr->env`) **[V for the targets]**

| addr | env method | proposed name |
|---|---|---|
| `0x00461ae0` | `0x469ea0` | `TimeOfDay_SetTime(h, m, s)` |
| `0x00461b00` | — | `TimeOfDay_SetSpeed(float)` → `env+0x3c = (int)speed` |
| `0x00461b50` | — | `TimeOfDay_Enable(bool)` → `env+0x40` |
| `0x00461bf0` | — | `TimeOfDay_IsUpdating()` → `env+0x40` |
| `0x00461b60` | `0x46afe0` | `TimeOfDay_SetRainParameters(...) ?` |
| `0x00461b80` | `0x46a110` | `TimeOfDay_SetTimeToTransit(...) ?` |
| `0x00461ba0` | `0x46a140` | `TimeOfDay_SetTimeToHold(...) ?` |
| `0x00461bc0` | `0x46a230` + `RenderManager::EnableSky` | `TimeOfDay_EnableRain(bool) ?` |
| `0x00461c10` | `0x46a2b0` | `TimeOfDay_SetKeyFrameTime(...) ?` |
| `0x00461c30` | `0x469f80` | `TimeOfDay_AttachClearEnvParam(...) ?` |
| `0x00461c50` | `0x469fa0` | `TimeOfDay_AttachRainyEnvParam(...) ?` |
| `0x00464230` | `0x46a190` | `TimeOfDay_GetTime(...) ?` |
| `0x004642a0` | `0x46b8c0` | `TimeOfDay_IsRaining() ?` |

---

## 8. `RenderManager::env` (`+0x1c`) — the environment manager

Ctor `0x46bd80`, **0xc44 bytes**, `Update` `0x46c1d0`. No RTTI record (not polymorphic), so the
class name is not recoverable; the leak reaches it only through the flat `TimeOfDay_*`,
`AmbientEffect_*` and `Renderer_*EnvParam` functions. Proposed name **`renderer::EnvManager ?`**.

Known fields **[V]**:

```
  +0x3c   i32    timeOfDaySpeed
  +0x40   bool   timeOfDayEnabled
  +0x44   bool   cameraIsIndoors          (getter 0x46a520)
  +0xac   ColourEffectsParameters (0x20)
  +0xcc   FogParameters (0x18)
  +0x118  EnvParameters (0x1c)
  +0xc28  fog colour / start / end mirror (written by View_SetFog)
  +0xc34  background colour mirror
  +0x1d4  float  windAngle (degrees)      // read by InstancePrimitive::Display
  +0x1d8  float  windSomething
```

`0x46b830` (`Register`, called by `SFStatePropLoader`) is a **static-ish helper on this object**: it
walks element 0's drawable and rewrites the layer of every primitive whose class id is `g[0x811348]`
and whose `vslot25() == 8` to layer 3 (the night-light layer). It is *not* `StatePropManager`
(that is a separate 0x2da0-byte singleton at `0x4b0000+`); the earlier note in `renderables.md` §2
mis-attributed it.

---

## 9. `occlude::` and `renderer::gLightManager`

### 9.1 `occlude::Occluder` **[V]**

RTTI `??_R4Occluder@occlude@@6B@`, vtable `0x007377c8`, **0x70 bytes**, constructed inline inside
the loader. Loader: `occlude::OccluderLoader`, vtable `0x007377fc`, ctor `0x00461840`
(chunk id `0x0880000a`), `LoadObject` = **`0x00461650`**.

```
occlude::Occluder : pure3d::Entity                     (0x70)
  +0x00  vtable / refcount / (Entity)
  +0x0c  Vector  boundsMin      // init  +1.0e12, then min(centre) - halfExtent
  +0x18  Vector  boundsMax      // init  -1.0e12
  +0x24  float   yaw            // atan2 of the two extent components
  +0x28  bool    isBoxOccluder      // chunk "type" == 2
  +0x29  bool    isSomethingElse    // chunk "type" == 1
  +0x2c  Box     box            // tested with BoxIntersectsSphere (0x6633c0) before the planes
  +0x68  bool
  +0x6c  i32     = -1
```

`LoadObject` reads: 3 floats (centre), 3 floats, 1 u32 type, 1 u32, 3 floats (half extents, each
× `g[0x7644ec]`), builds `boundsMin/boundsMax`, forces `boundsMin.y = -1.0f`, stores
`yaw = atan2(...)`, then calls `0x461080` to build the plane set. The object is auto-named
`"OccluderObject_%d"` from a global counter. **[V]**

### 9.2 The runtime occluder array **[V]**

```
occlude::OccluderInstance          (0x98, array at 0x00810d10, count g[0x00810d04])
  +0x00  Plane  planes[9]          // {nx, ny, nz, d}, 9 × 0x10
  +0x90  u32    numPlanes
  +0x94  Occluder* occluder
```

```c
// 0x00460900 -- test a sphere against one occluder's plane set
int Occluder_TestSphere(const Plane *planes, u32 n, const Sphere *s) {
    for (i = 0; i < n; i++) {
        g_occStatPlaneTests++;
        float d = dot(planes[i].n, s->centre) + planes[i].d;
        if (!(d <  s->r)) return 0;     // entirely in front of this plane -> NOT occluded
        if (!(d < -s->r)) return 1;     // straddles -> partially
    }
    return 2;                            // behind every plane -> fully occluded
}

// 0x00460980 -- test a sphere against every occluder.  2 == cull.
int occlude::TestSphere(Vector centre /*by value*/, float radius) {
    g_occStatSpheresTested++;
    if (g_occlusionEnabled != 1) return 0;
    int result = 1;
    for (i = 0; i < g_numOccluders; i++) {
        OccluderInstance *oi = &g_occluders[i];
        g_occStatOccluderTests++;
        if (oi->occluder->isBoxOccluder && BoxIntersectsSphere(&oi->occluder->box, &sphere))
            continue;                                   // we are inside the occluder box
        int t = Occluder_TestSphere(oi->planes, oi->numPlanes, &sphere);
        if (t == 2) { g_occStatCulled++; return 2; }
        if (t == 0) result = 0;
    }
    return result;
}

// 0x00460a50 -- the convenience wrapper
bool occlude::IsVisible(Vector c, float r) { return occlude::TestSphere(c, r) != 2; }
```

### 9.3 `renderer::gLightManager` = `g[0x008111cc]` **[V for the members, name from the leak]**

Ctor `0x00460600`, 0x70 bytes. It owns three things:

```
renderer::LightManager                                  (0x70)
  +0x28  Room**        rooms         // begin
  +0x2c  Room**        roomsEnd
  +0x38  TemplateLight** lights      // 0x100 slots; each slot: Matrix at +0x00,
                                     //   u32 templateId +0x40, bool +0x44, bool inUse +0x45
  +0x6c  Room*         defaultRoom
```

| addr | proposed name | notes |
|---|---|---|
| `0x0045fa50` | `LightManager::FindRoom(Vector pos, bool *pInside) -> Room*` | walks `rooms`, tests each enabled room's volume (`0x45f3c0`); falls back to `defaultRoom` |
| `0x0045fbd0` | `LightManager::AddTemplateLight(u32 templateId, Matrix xform, bool) -> int` | first free of 0x100 slots |
| `0x0045fc30` | `LightManager::RemoveTemplateLight(int handle)` | |
| `0x0045fc50` | `LightManager::UpdateTemplateLight(int handle, Matrix xform)` | |
| `0x00460380` | `LightManager::AddLightingZone(...)` | façade `0x004640d0` = `renderer::LightingZone_Create(key, volume, groupKey)` |
| `0x0045f9f0` | `LightManager::RemoveLightingZone(...)` | façade `0x00464140` = `renderer::LightingZone_Destroy(key)` |
| `0x00460130` | `LightManager::Update(TimeInfo*)` | called from `RenderManager::Update` |
| `0x0045fe10` | `LightManager::?` | used by `LightingRenderable` |
| `0x004603d0` | `LightManager::RebuildOccluders ?` | |

The façade trio `0x00463920 / 0x004639a0 / 0x00463a10` is the flat
`AddTemplateLight / RemoveTemplateLight / UpdateTemplateLight` the leak calls as
`renderer::gLightManager->…`.

---

## 10. How a renderable lives

1. **Created.** Either
   * a chunk loader (`renderer::WorldGeoLoader`, `SFStatePropLoader`, …) builds it during streaming
     and registers it in the `content::LoadInventory` under `GetHash(name)`, or
   * the game calls a `renderer::<Class>Renderable_CreateInstance(name, inventory, scene, …)`
     façade, which hashes the name, `Find<T>()`s the model out of the inventory, allocates the
     renderable out of the class's heap (`GetHeap`), constructs it, and
   * `StatePropManager` calls `renderer::CreateInstanceRenderable` for eco props.

   The ctor sets `typeMask`, grabs `uniqueId = g[0x8113f4]++`, sets the identity matrix,
   `flags80 = 0x13` (distance test + fade + visible) and allocates `n` `DisplayListElement`s.
   Each element gets a drawable via `SetElement` (which computes bounds and wires the
   `DisplayListPrimitive`'s parent pointer back to the renderable) and a `{min, max, fadeBand}`
   draw-distance band via `SetElementDrawDist`.

2. **Registered.** `scenes[sceneIdx]->AddRenderable(r)` puts it in the first free slot of that
   scene's fixed array, `AddRef`s it and stamps `r->sceneIndex = scene->index`. `GamePlayScene`
   additionally special-cases shadows and sky. Then `r->vslot10()` is called once so it is in the
   display list immediately, and finally a `RenderableHandle` is allocated from
   `RenderableHandleBlock`, which `AddRef`s the renderable again, snapshots `uniqueId` and sets
   `flags80 |= 0x08`. **The handle is what the game gets back** — never the pointer.

3. **Ticked.** Once a frame, `RenderFlowClient::OnFrame` → `RenderManager::Update` →
   `scene->Update(t)`. In `GamePlayScene` every renderable gets `Renderable::Tick` (age +
   `UpdateFade`) **even if hidden**; only the visible ones whose `typeMask` passes the scene's
   filter then get `vslot9` (per-class animation/pre-display) and `vslot10`
   (`Renderable::Display`).

4. **Displayed.** `Renderable::Display` is a *state machine, not a draw call*: distance from the
   cull camera to `GetDistanceRefPos()` (or the matrix origin), then min/max band, then
   `Camera::SphereVisible` on the element-0 drawable's sphere transformed by the matrix, then
   `occlude::TestSphere`. On success it computes the cross-fade alpha, pushes the matrix and calls
   `DisplayListPrimitive::SetVisible(true)`, which — *only if the prim is not already in the
   list* — walks the drawable into `Display_List`. On failure it calls `SetVisible(false)`, which
   unlinks the prim's nodes. Nodes persist across frames; `RemoveFromList()` is called whenever the
   matrix changed or the fade state flipped, because the node caches the world matrix.
   (Full pseudo-C in `renderspine.md` §2.2.)

5. **Hidden.** `Renderable_SetVisible(h, false)` clears `flags80 & 0x10` *and* immediately hides
   every element's prim; the scene then skips it entirely (but still `Tick`s it). `vslot14 Hide()`
   is the cheaper version that only drops the display-list nodes.

6. **Destroyed.** `renderer::Renderable_Destroy(&handle)` does **not** destroy anything — it
   validates the handle and appends it to the global deferred-destroy queue
   (`g[0x8110bc]`, count `g[0x8110b4]`). The next `RenderFlowClient::OnFrame` calls
   `DestroyPendingRenderables()` **between `Update` and `Render`**, which runs the per-typeMask
   teardown, removes the renderable from `scenes[r->sceneIndex]` (dropping that reference) and
   deletes the handle. `~RenderableHandle` clears `flags80 & 0x08`, calls `Hide()` (so the display
   list forgets it), `Release()`s the renderable and sets `uid = 0xffffffff` — after which every
   stale copy of the handle fails the generation check and every façade call on it is a silent
   no-op. That is the entire lifetime contract.

---

## 11. Corrections to the earlier notes

1. **Scene draw/update order is 0, 1, 2, 3**, not `{0, 2, 3, 1}` (`renderspine.md` §1.4). The
   `if/else if` chain is compiler-reordered; the loop index still picks `scenes[i]`.
2. **`0x8111dc` is the culling camera, `0x8111d8` is the rendering camera** — two separate,
   independently settable slots, not "the current camera" and "a second camera"
   (`renderspine.md` §1.1).
3. **`0x458390` is `Canvas::UpdateFog`**, not "set viewport/camera" (`renderspine.md` §1.4).
4. **`RenderManager+0x1c` is the environment / time-of-day manager**, not
   "interiors/stateprop manager"; `StatePropManager` is a separate singleton
   (`renderspine.md` §1.2, `renderables.md` §2).
5. **`RenderManager+0x14` is a second `Canvas`**, not "an optional extra pass object"; the extra
   pass is `+0x24`.
6. **`Renderable+0x7c` is the scene index** (default 5, overwritten on `AddRenderable`), not an
   unexplained `= 5` (`renderables.md` §1).
7. **`Renderable+0x10` is a `Room*`** and **`flags81 bit0` is "inside a room"**, both maintained by
   `Renderable::UpdateRoom` (`0x473cc0`) from `gLightManager`.
8. **`0x473aa0` is `SetToFadeIn`, `0x473ac0` is `SetToFadeOut`** — a fade *time in ms*, confirming
   `renderspine.md` §2.4's rename, and the pair `0x473ae0`/`0x473b10` are the
   "fade to a given level at a given rate" variants the leak calls
   `Renderable_SetToFadeIn/Out`.
9. **`Renderable::Tick` reads two different `TimeInfo` fields**: `+0x08` for `timeSinceDrawn` and
   `+0x0c` for the fade step.
10. **`Scene::AddRenderable` silently drops** the renderable when the scene array is full — there is
    no growth and no error path.
