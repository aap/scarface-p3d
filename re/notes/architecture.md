# Pure3D architecture: 2003 (Simpsons Hit & Run) vs 2006 (Scarface)

An architecture study, written to answer one question: **where is the spine, and what should a
reimplementation keep?**

Sources:

* `/u/aap/fun/ps2engines/extracted/shr` — complete SHR source tree (May–Jul 2003).
  `libs/pure3d/{p3d,pddi,constants,docs}` = the engine; `libs/radcontent/src/radload` = the generic
  loader underneath it; `code/render/{RenderFlow,RenderManager,Culling,DSG,Loaders}` = how a real
  game used it. ~59 kLOC p3d, ~61 kLOC pddi, ~42 kLOC game render layer.
* `/u/aap/src/scarface-p3d` — aap's reimplementation, and `re/notes/{shr_vs_scarface,renderables,
  instances,statepropdata,ps2}.md` — the RE of the 2006 engine (PC retail + PS2 ELF).
* `/u/aap/lib/pure3d/scarface_src` — the leaked Scarface **game** layer (the engine layer is not in
  the leak; its API is visible only through call sites and through the RTTI/vtable dump).

Legend: **[V]** verified from source or disassembly; **[M]** measured from data; **[J]** my judgement.

---

## 0. Summary of the argument

aap's working impression was: *"if we compare pure3d from SH&R to pure3d from Scarface the class
hierarchy is rather more complicated at the 3D layer, and it's not clear that was a good idea. The
pddi layer looks pretty reasonable, and the general chunk loader system is not too bad either, but it
seems to be lacking a good spine while being opinionated enough to make it hard to build your own."*

After reading both, I would sharpen it to five claims:

1. **2003 Pure3D genuinely has no spine, and that is by design.** It is a *toolkit*: a chunk loader,
   an inventory, a device layer, and a bag of objects that know how to draw themselves against
   ambient global state (`p3d::stack`, `p3d::pddi`, `p3d::context`). There is no frame, no world, no
   visibility. The game supplies all three — 42 kLOC of `code/render` with its own entity hierarchy
   (`IEntityDSG`), its own spatial tree (`SpatialTree`/`WorldScene`), its own layer system
   (`RenderManager`, 5 layers) and its own sort. `tDrawable::Display()` takes no arguments and
   returns nothing; everything it needs is a global.

2. **2006 Pure3D *did* grow a spine, and it is a good one.** The frame became: *per-object distance
   test → membership delta on a retained, material-bucketed draw list → walk the buckets in a fixed
   order → draw*. Membership is a delta, not a rebuild; sorting happens only inside the few buckets
   that need depth order, and only when one of them was touched. That is the correct answer for a
   streamed open world, and it is roughly what a modern renderer does.

3. **But the spine lives in `renderer::`, above the `pure3d::` line, and it is not parameterised.**
   `pure3d::` was reshaped *to serve it* (the `tDrawable` three-way split, `DisplayList` becoming an
   abstract interface) without ever owning it. So from inside `pure3d::` the engine still looks
   spineless, while being unable to work any other way. That combination is precisely what makes it
   feel opinionated-but-directionless.

4. **The real opinionation is not in the C++ — it is in the file format.** By 2006 the world model
   (zones, packages, world-geo LOD distances, occluders, state props, instance placements) had moved
   *into engine chunk ids* `0x088000xx` / `0x0990019x`. Load the files and the visible world exists;
   no renderer-side game code runs. `renderer::WorldGeoRenderable` is not a design choice you can
   revisit — it is the in-memory shape of chunk `0x08800003` plus its `0x08800009` annotations.
   **Pure3D 2006 is not an
   engine with a data format; it is a data format with an engine attached.**

5. **Streaming is the force behind every 2006 change, and it is invisible from the engine binary.**
   The leaked game layer shows a `StreamManager` / `StreamPackage` / `StreamSlot` system where an
   inventory's lifetime *is* a package's residency in a named memory slot, only one package may be
   in load-completion at a time, and object construction is pumped at 5–20 ms per frame with the
   game acting as backpressure on the streamer (§3.4). Once you see that, the abstract `DisplayList`,
   `RenderableHandle`, the retained draw list and the inventory parent chain all stop looking like
   taste and start looking like consequences.

The practical consequence for the reimplementation, spelled out in §4: keep the data-shaped classes
verbatim, refuse to let them be the spine, and write your own spine above them.

---

## 1. Layer map

### 1.1 The 2003 stack

```
  game     code/render/RenderFlow      RenderFlow::OnTimerDone   (the frame)
           code/render/RenderManager   RenderManager, RenderLayer x5, WorldRenderLayer
           code/render/Culling         SpatialTree, SpatialNode, WorldScene, SpatialTreeIter
           code/render/DSG             IEntityDSG : tDrawable  + ~15 subclasses
           code/render/Loaders         IWrappedLoader : tSimpleChunkHandler  + ~20 loaders
  ---------------------------------------------------------------------------------------
  p3d      libs/pure3d/p3d             tEntity, tInventory, tLoadManager, tChunkFile,
                                       tDrawable / tGeometry / tPrimGroup / tCompositeDrawable,
                                       tSkeleton / tPose / tFrameController / tMultiController,
                                       tContext / tView / tCamera / tMatrixStack, DisplayList
  radload  libs/radcontent/src/radload radLoadObject, radLoadHashedStore, radLoadFileLoader,
                                       radLoadDataLoader, radLoadStream, RefHashTable, the queue
  radcore  libs/radcore                radKey, radFile (RCF/cement), radMemory, radThread
  ---------------------------------------------------------------------------------------
  pddi     libs/pure3d/pddi            pddiDevice/Display/RenderContext/Texture/Shader/PrimBuffer
                                       + base/ (2.4 kLOC) + gl dx8 xbox GameCube ps2
```

**What each layer owns, and what it assumes.**

*pddi* owns GPU resources and GPU state, nothing else. `pddiRenderContext` is a ~120-method
fixed-function god-object: frame (`BeginFrame`/`EndFrame`/`Clear`), a 5-deep *matrix stack inside the
context*, camera (`SetCamera(near, far, fov, aspect)` — Pure3D never hands it a projection matrix),
8 fixed-function lights (4 on PS2/GC), raster state, and two draw paths: retained
(`NewPrimBuffer` → `DrawPrimBuffer(shader, buffer)`) and immediate
(`BeginPrims(shader, type, format, count, pass)` → per-vertex → `EndPrims`). A "shader" is a named
C++ class (`NewShader("simple")`, ≤16 per backend) with FOURCC-keyed parameters
(`PDDI_SP_BASETEX`, `PDDI_SP_BLENDMODE`, …) dispatched through hand-rolled member-function-pointer
tables in `pddi/base/baseshader.cpp`. No compilation, no reflection. `pddiExtension` with
ID-namespaced extension ids is the escape hatch for platform reality
(`PDDI_EXT_PS2_CONTROL` → `LoadMicrocode`, `AddVU1Program`, `SyncScratchPad`; `PDDI_EXT_VERTEX_PROGRAM`
on Xbox/DX8). It assumes nothing about the layers above.

*p3d* owns **assets** (entity, inventory, loader, chunk file, geometry, shader, texture, skeleton,
animation) and a thin frame veneer (`tContext`, `tView`, `tCamera`, `tMatrixStack`). It assumes:
`p3d::pddi`/`p3d::device`/`p3d::display`/`p3d::stack`/`p3d::context`/`p3d::inventory` are live
globals (`libs/pure3d/p3d/utility.hpp:37-43`); that somebody has called `tContext::BeginFrame()` and
`tView::BeginRender()`; and that the object→world matrix is already on
`PDDI_MATRIX_MODELVIEW` when `Display()` is called. `tMatrixStack` stores nothing — it is an inline
forwarder to `pddiRenderContext::{Push,Pop,Mult,Load}Matrix`. **All transform flow goes through
pddi's single modelview stack; there is no engine-side matrix.**

*game render layer* owns the world, visibility, ordering and the frame. `IEntityDSG : public
tDrawable, public tDrawable::ShaderCallback` is the world object; `StaticEntityDSG` holds only
`{Vector mPosn; int mIsGeo; tDrawable* mpDrawstuff;}` — **no matrix at all**, because static world
geometry is baked into world space by the exporter. `InstStatEntityDSG` adds an
`rmt::Matrix*` and does `PushMultMatrix / Display / PopMatrix`.

**Where the frame flows (2003) [V]:**

```
RenderFlow::OnTimerDone(elapsed)                      code/render/RenderFlow/renderflow.cpp
 └ RenderManager::ContextUpdate(elapsed)
    ├ p3d::context->SwapBuffers()                     ← presents the PREVIOUS frame
    ├ RenderManager::MunchDelList(2000)               timeboxed deferred entity release
    ├ LensFlareDSG::ReadFrameBufferIntensities()
    ├ p3d::context->BeginFrame() → pddi->BeginFrame(), stack->LoadIdentity()
    ├ for (i = numLayers-1 .. 0) mpRenderLayers[i]->Render()      // 5 layers, back to front
    │   └ WorldRenderLayer::Render()                             // LevelSlot
    │      ├ mpView[view]->BeginRender()   → tCamera::SetState() → pddi->SetCamera(...)
    │      │                                → LoadViewMatrix, lights, fog, Clear
    │      ├ worldSpheres[i]->Display()                          // sky, z-write off
    │      ├ WorldScene::Render(view)
    │      │   ├ SpatialTreeIter::AndTree(msClear)
    │      │   ├ WorldScene::MarkCameraVisible(cam, msVisible0)  // sphere+cone, NOT a frustum
    │      │   └ WorldScene::RenderScene(msVisible0, cam)        // walk marked nodes, fill:
    │      │        mpZSorts[]           opaque,      key = IEntityDSG::GetShaderUID()
    │      │        mpZSortsPass2[]      translucent, key = IEntityDSG::mRank (view depth)
    │      │        mShadowCastersPass1[]
    │      ├ WorldScene::RenderOpaque()      → IEntityDSG::Display() x N
    │      ├ WorldScene::RenderSimpleShadows()
    │      ├ WorldScene::RenderTranslucent()
    │      ├ BillboardQuadManager::DisplayAll()
    │      ├ for (i = mpGuts.size-1 .. 0) mpGuts[i]->Display()   // characters, vehicles
    │      └ mpView[view]->EndRender()
    └ p3d::context->EndFrame(false)                   ← EndFrame WITHOUT swap
```

and at the bottom, one entity:

```
InstStatEntityDSG::Display()
  → pddi->PushMultMatrix(PDDI_MATRIX_MODELVIEW, mpMatrix)
  → tGeometry::Display()  →  for each tPrimGroup: Display()
       tPrimGroupOptimised     → pddi->DrawPrimBuffer(shader->GetShader(), mBuffer)
       tPrimGroupStreamed      → pddi->BeginPrims(...) / per-vertex / EndPrims(...)
       tPrimGroupSkinnedOptim. → pddiExtHardwareSkinning::SetMatrix x N, DrawSkin(shader, buffer)
  → pddi->PopMatrix()
```

Animation does **not** run in the frame: `AnimEntityDSGManager::Update` and the physics/entity
`Update()`s are ticked from the gameplay context
(`code/contexts/gameplay/gameplaycontext.cpp:500`), so `RenderFlow` reads an already-settled world.
`tMultiController::Advance` → `tPoseAnimationController::Update` writes joint object matrices and
clears `poseReady`; `tPose::Evaluate()` re-concatenates lazily at
`tCompositeDrawable::Display(tPose*)` time.

`p3d/displaylist.cpp` (4 KB) is *not* part of this path. It is a "Mini Display List… used by the
SceneGraph and the tCompositeDrawable for rendering translucent object last": a fixed-capacity
per-frame list of `{sortPosition, zPosition, objectToView, tDrawable*}`, `qsort`ed by
(sortOrder desc, z far→near), drained and purged every `Display()`. Its only owners are
`tCompositeDrawable::translucentObjects` and the Pure3D `Scenegraph`. This matters for §2: **the
2003 `DisplayList` is a leaf-level convenience, not the renderer.**

### 1.2 The 2006 stack

Three tiers, not two. The leak makes the split visible: `gameobject/` (leaked) sits on `engine/`
(`renderer::`, `content::`, `StatePropManager`, `ScriptObject`, `StreamManager`, `ResourceManager`
— *not* leaked) which sits on `pure3d/`.

```
  game     gameobject layer (leaked)   InstanceObject, StatePropObject, LoadPackage, vehicles,
                                       characters, HUD.  Derives from the engine in exactly four
                                       places tree-wide; everything else goes through a flat
                                       C-style façade.
  ---------------------------------------------------------------------------------------
  engine   the renderer::* façade      ~110 free functions over an opaque RenderableHandle*
           StatePropManager            the instance/eco-prop budget + cull manager (0x2da0 bytes)
           StreamManager / StreamPackage / StreamSlot / ResourceManager   (the streaming spine)
           ScriptObject, GameObject, Template, FlowManager / FlowClient
  ---------------------------------------------------------------------------------------
  renderer renderer::  (internals)     Renderable (+~20 subclasses), RenderableHandle,
                                       DisplayListPrimitive, Display_List (84 lists),
                                       RenderManager, Scene / GamePlayScene, Canvas,
                                       RenderFlowClient, and the 0x088000xx chunk loaders
           occlude::                   Occluder, OccluderLoader   (chunk 0x0880000a)
  ---------------------------------------------------------------------------------------
  pure3d   pure3d::                    Entity, DrawableHierarchy / DrawableContainer /
                                       DrawablePrimitive, Geometry, PrimGroup*, CompositeDrawable
                                       (+ActivePrimitiveList/ActiveControllerList), Skeleton
                                       (+Limb, +Partition), CharacterPose, Animation/Channel/
                                       FrameController/MultiController, Shader, Texture,
                                       DisplayList (now abstract), prop::StatePropData
  content  content::                   LoadManager, LoadRequest, LoadInventory (+DynamicCaster<T>),
                                       ChunkFile, LoadStream, P3DFileHandler, SimpleChunkHandler
  core     core::                      RefCount, Object, Key32/GetHash, File, Drive,
                                       CementLibrary, Memory*, Platform
  om       om::                        Entity, MetaType, MetaAttrib, Stream — a new reflection layer
  ---------------------------------------------------------------------------------------
  pddi     pure3d::pddi*               essentially unchanged from 2003; + d3dExtInstancing,
                                       d3dExtHardwareSkinning, d3dExtFramebufferEffects, …
```

Class counts from the RTTI dump (2153 vtables total) [M]: `pure3d::` 163, `om::` 62, `core::` 59,
`renderer::` 58, `content::` 57. So the *engine* is about 400 classes; the 3D layer proper is ~160.
For comparison SHR's `libs/pure3d/p3d` declares ~310 classes in its headers. The comparison is rough
— the Scarface figure counts only classes that have a vtable *and* RTTI, the SHR figure counts every
declared class — but it is off by a factor of two in the direction opposite to the impression:
**the 3D layer did not get bigger, it got re-shaped.** What grew is the renderer namespace, which in
2003 lived in `code/render` as 42 kLOC of *game* code and did not count as "the engine" at all.

**What each new layer owns.**

*`renderer::Renderable`* (base size 0x84 [V]) is the engine's entire world-object model:

```
Renderable : pure3d::Entity
  +0x14  Matrix matrix
  +0x54  i32 typeMask            // 8 WorldGeo, 0x10 StateProp, 0x200 Instance, 0x8000 ZonePkg, ...
  +0x58  DisplayListElement* elements_begin / end / capacity
  +0x64  u32 uniqueId
  +0x80  flags: isVisible, doDistFade, ...
DisplayListElement (0x30)
  float drawDist[3]  // min, max, fade
  DisplayListPrimitive prim (0x20)
  bool  isFading
DisplayListPrimitive (0x20)
  Renderable* parent;  DrawableHierarchy* drawable;  LinkedList children;
  flags: isVisible, isInList, flag4
```

i.e. *"a thing at a matrix, with N elements, each of which has a distance band and may or may not
currently be in the draw list."* That is a good, small world model, and it is the spine object.

*`renderer::Display_List : pure3d::DisplayList`* owns ordering: `NUM_DISPLAY_LISTS = 84` intrusive
linked lists, a free list, and a preallocated store of `DisplayListDrawable` records
`{matrix, PrimEntry*, DrawableContainer*, Shader*, list back-pointer, parent back-pointer}`.

*`pure3d::DrawableHierarchy` vs `DrawablePrimitive`* is the key 2006 split (§2.1): the hierarchy side
*submits*, the primitive side *draws*.

*`StatePropManager`* (a 0x2da0-byte singleton) owns instance placement, budget and culling for the
47 631 eco-prop placements: `SLocationData mLocations[4181]`, `SInstanceRenderSet mRenderSets[255]`,
`SInstanceSlot[895]`, a hard cap of 894 live instances and 103 live instance renderables, and a
time-sliced scan of **350 locations per frame** [V].

*`occlude::Occluder`* — 1022 in z04 [M], derived from `pure3d::Entity` not from `Renderable`; pure
CPU-side culling volumes that never touch `Display_List`.

Notably absent: **there is no spatial acceleration structure in Scarface at all.** Nothing in the
2153-vtable dump resembles `SpatialTree` / `OctTreeNode` / `WorldScene`, and the frame trace confirms
it: `GamePlayScene::Update` (`0x00468c50`) walks a flat `Renderable*` array in full, every frame,
filtered only by `typeMask & scene->typeMaskFilter` [V]. The 2003 octree was replaced by *zone/package
granularity for what exists + per-object frustum + 256 software occluders + a draw-distance band*.

**The game does not see any of this.** The single most surprising thing in the leaked game layer is
how little of the engine it touches. Across 1903 files the game derives from an engine class in
exactly four places (`content::SimpleChunkHandler` ×5, `pure3d::Entity` ×3, `pure3d::RefCounted` ×1)
[V]. Everything else goes through a flat C-style façade in `namespace renderer` — about 110 free
functions over an opaque `renderer::RenderableHandle*`:

```cpp
// model (archetype) / instance split, at the façade
renderer::ModelHandle*      RenderableModel_Find(const char* name);
renderer::ModelHandle*      TerrainRenderableModel_Create(name, data, inventory, visStart, visEnd);
renderer::RenderableHandle* Renderable_CreateInstance(ModelHandle*, renderer::GAMEPLAY_SCENE);
// typed one-shots
renderer::RenderableHandle* ZonePkgRenderable_CreateInstance(name, InventoryId, scene);
renderer::RenderableHandle* ShadowRenderable_CreateInstance(model, inv, scene, CAR_SHADOW);
// generic ops
void Renderable_Destroy(RenderableHandle**);   void Renderable_SetTransform(h, math::Matrix);
void Renderable_SetVisible(h, bool);           void Renderable_SetRoom(h, roomIndex);
void Renderable_DisableLOD(h, bool);           void Renderable_SetToFadeIn(h, rate, 0.0f);
// view
void View_SetRenderingCamera(pure3d::Camera*); void View_SetCullingCamera(pure3d::Camera*);
void Scene_Enable(scene, bool);                // exactly two scenes: GAMEPLAY_SCENE, GUI_SCENE
```

There is **no `RenderManager`, no `Display_List`, no scene graph and no `Render()` exposed to the
game at all** — `Display_List` appears in the entire leaked tree once, inside a comment [V]. So the
20-deep `Renderable` hierarchy is a private implementation detail behind a handle. That changes the
weight of aap's complaint in an interesting way (§2.3): the hierarchy is not the API, the façade is —
and the façade is short, flat and rather good. What is opinionated is *what the façade lets you say*,
and that is fixed by the chunk format (§2.9).

Two details from the façade worth stealing outright [J]: `View_SetRenderingCamera` and
`View_SetCullingCamera` are **separate** (and this is real, not cosmetic — the engine keeps three
`pure3d::Camera` globals at `0x008111d4/d8/dc`, with `GetCurrentCuller` `0x00461ad0` returning
`g[0x008111dc]` [V]), and `renderer::` has a real model/instance split (`ModelHandle` = archetype,
`RenderableHandle` = placement) that the internal class hierarchy never quite commits to.

### 1.3 Where the frame flows in 2006 — the spine

Traced from the retail PC image; addresses are VAs in `re/scarface_unpacked.bin` (base 0x401000).
**[V]** unless marked.

```
RenderFlowClient::OnRender  0x00465590   (vtable slot 2 of renderer::RenderFlowClient, g[0x008111ec])
 ├ ScopedMemoryAllocator(g[0x8111c8])              0x43c710 / 0x43c730   — a HEAP switch, not a lock
 ├ timeOfDay g[0x8111e8] += dt * rate, wrap 86400000 ms
 │
 ├── CULL / BUILD PASS ───  RenderManager::CullAndBuild  0x00467810
 │    ├ canvas->view->SetupViewMatrix 0x0067dd40  (camera world matrix → InvertOrthonormal)
 │    ├ SetCurrentCamera(GetCurrentCuller())  0x0067ba00      (culler = g[0x008111dc])
 │    └ for i in 0..3:   scene[i]->vslot4 = Update(params)
 │         scene[0] is the GamePlayScene (vt 0x00737a9c, owns the Display_List at +0x20);
 │         the other three are plain renderer::Scene (vt 0x00737a80) drawn immediate-mode.
 │
 │       GamePlayScene::Update  0x00468c50
 │         for each Renderable* r in scene->renderables[]      ← A FLAT ARRAY, WALKED IN FULL
 │             0x004740c0(r, params)                            (tick r's timer / fade)
 │             if (!(r->flags80 & 0x10)) continue;              // isVisible
 │             if (!(r->typeMask54 & scene->typeMaskFilter)) continue;
 │             r->vslot9(params)                                // per-class update
 │             r->vslot10()                                     // Renderable::Display 0x004740f0
 │                 transform element sphere by the view matrix
 │                 culler->vt[0x60]  build world frustum
 │                 culler->vt[0x58]  sphere-vs-frustum quick test
 │                 0x00460980        occlusion test vs the ACTIVE OCCLUDER SET (2 = occluded)
 │                 min / max / fade distance test
 │                 → DisplayListPrimitive::SetVisible 0x00458f00  or  ::Hide 0x00458ea0
 │                     SetVisible: if (visible && !isInList) {
 │                                    drawable->vslot10(g_displayList /*0x008108a0*/, prim);
 │                                    isInList = 1; }
 │                     Hide:       Display_List::RemovePrimitive 0x00458a20 — unlink the prim's
 │                                 nodes from their lists, return them to the free list,
 │                                 and zero node[+0x5c]
 │         then  0x0045ad10   FINALIZE:
 │                 z-sort the lists flagged dirty (0x006e8a20, comparators 0x004589e0 / 0x00459060)
 │                 occlude::BuildActiveSet 0x00461270 (culler, 150.0f) — frustum-test all 256
 │                 registered occluders (0x008108d0), write survivors to 0x00810da4
 │
 ├ 0x00464ac0   sweep the RenderableHandle array (g[0x8110bc], n = g[0x8110b4])
 ├ 0x004b23b0   StatePropManager::ProcessPendingRenderableAdds      (called at 0x004656ba)
 │
 └── DRAW PASS ────────────  RenderManager::Render  0x004689a0
      ├ canvas->view->Begin  0x0067ddb0   renderContext->vt[0x70] SetViewport, view + proj matrices
      ├ for i in 0..3:  Canvas::RenderScene 0x00458620 → scene->vslot3 = Display()
      │     GamePlayScene::Display 0x00468aa0 → Display_List::Playback (slot 9) 0x0045e680
      │        one 2697-byte hand-coded pass list; all sub-passes (shadow, reflection, main,
      │        water, effects, fog/exposure) live INSIDE this one function, each ending in
      │        Display_List::RenderList(list, doFade)  0x00459910
      │          for node in lists[idx]:
      │             prim->vt[0x38] / vt[0x3c]   set fade / set container state
      │             xform->vt[0x4c](0, &node->matrix)      ← the matrix comes from the NODE
      │             PrimArrayEntry::Display 0x00683340
      │                → Geometry::Display 0x0069c610 / PrimGroup::Display 0x006a4740
      │                  / InstancePrimitive::Display 0x0046f110 / BillboardObject::Display
      │                → pddiPrimBuffer->vt[0x88] → d3dPrimBuffer::Display 0x00703c60
      │                → d3dContext::DrawPrimBuf 0x0064b650
      │          (RenderList does NOT unlink anything)
      │        …and ends with  Display_List::Clear  0x0045b0c0  (see below)
      │     plain Scene::Display 0x00468b80 → DrawableContainer::Display 0x006834c0 (immediate)
      └ canvas->view->End    0x0067df00
```

Four corrections to the obvious reading, all of which matter for §4:

1. **The draw list is *hybrid*, not purely retained.** `Display_List::Clear` (`0x0045b0c0`), called at
   the very end of `Playback`, walks all 84 lists and recycles **only nodes whose `+0x5c` is zero**;
   nodes with a non-zero `+0x5c` survive into the next frame. The `Renderable` /
   `DisplayListPrimitive` path produces the surviving kind — membership changes only when visibility,
   the drawable or the LOD changes, exactly as `renderer/display_list.cpp` assumes. But a second,
   *immediate* path (the three non-GamePlay scenes, and anything that submits per frame) puts
   transient nodes into the same 84 lists and lets `Clear()` flush them. Both kinds coexist.
2. **There *is* per-frame sorting, but only where it is needed.** The finalize step `0x0045ad10`
   z-sorts only the lists flagged dirty (list 46 at `+0x228`, list 30 at `+0x168`, list 39 at
   `+0x1d4` are visible in the code), with two comparators. So the design is: bucket by material at
   load time, sort *within* a bucket only for the buckets that need depth order, and only when
   something changed.
3. **Per-object visibility is frustum + occlusion + distance, not distance alone.**
   `Renderable::Display` runs `culler->vt[0x58]` (sphere vs frustum), then `0x00460980` (test against
   the active occluder set), then the min/max/fade band — and *then* flips the membership bit. The
   distance test is the LOD/fade band, not the culler. `0x00460980` is the occlusion test;
   the frustum test is the vtable call before it.
4. **There is no spatial acceleration structure at all.** `GamePlayScene::Update` walks a flat
   `Renderable*` array in full, every frame, filtered only by `typeMask & scene->typeMaskFilter`.
   The "culler" is a plain `pure3d::Camera` (three of them at `0x008111d4/d8/dc`; `GetCurrentCuller`
   `0x00461ad0` returns `g[0x008111dc]`, which is why the façade can set rendering and culling
   cameras independently). Acceleration is: per-object draw distance from the ZonePkg chunk, the
   frustum, and 256 software occluders. The occluder active set is rebuilt *after* the cull walk, so
   frame N uses frame N−1's occluders [G].

And two facts that correct earlier notes in this directory:

* `RenderManager::GetLock(0xb)` (`0x004675b0`, `return this[0x4c + i*16]`) is **`GetHeap(i)`**, and
  `0x0043c710`/`0x0043c730` are `Get/SetMemoryAllocator`, not a lock. The names sit after the
  `RenderManager` vtable at `0x007379c0`: `EmptyBlock, RenderableHandleBlock, WakeBlock, ShadowBlock,
  ParticleEffectBlock, InstanceBlock, MediumStatePropBlock, SmallStatePropBlock, DecalBlock`.
  `instances.md §5.2`'s `ScopedLock` should read `ScopedHeap`.
* **The render path is single-threaded.** The only threads in the image are
  `LoadManager::LoadThreadEntry` (`0x006ea380`) and `core::DriveThread` (`0x007723a4`). There is no
  render thread and no command buffer — **the display list *is* the command buffer**, and it holds
  pointers, not encoded commands.

`StatePropManager::Update` (`0x004b3a90`) is *not* in this flow — its only caller is `0x004b3e30`,
the game update tick. It runs before the render flow and feeds it through
`ProcessPendingRenderableAdds`:

```
game update tick 0x004b3e30
  phase 1 → StatePropPreSimSet
  phase 2 → StatePropManager::Update 0x004b3a90
              ProcessFadeCollidableList 0x004b0530
              bFullScan = |camPos - lastCamPos|² > 400   (20 m)
              InstanceUpdate 0x004b31a0 — TIME-SLICED, 350 locations/frame, 2-D XZ squared distance,
                cullLimit² = (template->mVisibilityEnd * drawDistScale)² + 900  (30 m hysteresis)
                → QueueLocationForRenderableAdd / RemoveLocationFromRenderable
                → no fade and no LOD switch here; it only ADDS and REMOVES instances
              push mDrawDistanceScale to the renderer (0x00464040)
          → StatePropPostSimSet
  (game objects push transforms here: renderer::Renderable_SetTransform)
```

---

## 2. What changed at the 3D layer, and why

### 2.1 `tDrawable` → `DrawableHierarchy` / `DrawableContainer` / `DrawablePrimitive`

2003:

```cpp
class tDrawable : public tEntity {
    virtual void Display() = 0;                     // draws, NOW, against p3d::stack + p3d::pddi
    virtual void ProcessShaders(ShaderCallback&);
    virtual void GetBoundingBox(rmt::Box3D*);
    virtual void GetBoundingSphere(rmt::Sphere*);
    virtual void DisplayBoundingBox/Sphere(...);    // !RAD_RELEASE only
};
```

Everything drawable derived from it directly: `tGeometry`, `tSprite`, `tTextString`,
`tBillboardQuadGroup`, `tEffect`, `tAnimatedObject`, `tDrawablePose` → `tPolySkin` /
`tCompositeDrawable`, `Scenegraph`, and on the game side `IEntityDSG`.

2006 splits the *roles* rather than the *things*:

```cpp
class DrawableHierarchy : public Entity {             // the SUBMIT side (interior nodes)
    virtual void Display(DisplayList* list, GameDrawableInfo* info) = 0;
    virtual void CalcBounds() = 0;
    virtual void SetShaderCallback(ShaderCallback*); ...
    virtual void SetFading(bool); SetFadeAmount(float); IsFading(); GetFadeAmount();
};
class DrawableContainer : public DrawableHierarchy {  // concrete node: Array<PrimEntry>
    void DrawPrimitives(DisplayList* list, GameDrawableInfo* info);   // → list->AddContainer(...)
};
class DrawablePrimitive : public Entity {             // the DRAW side (leaves)
    u32 layer;                                        // ← the sort key, assigned at LOAD time
    virtual void Display() = 0;                       // issues the pddi draw
    virtual Shader* GetShader() const = 0;  virtual u32 GetSomeMask() = 0;
    virtual bool IsLit() = 0;  virtual bool IsALUM() = 0;
};
```

`Geometry : DrawableContainer`, `PrimGroup : DrawablePrimitive`, `CompositeDrawable :
DrawableHierarchy`.

**Verdict: essential complexity, and the single best change in the engine. [J]**
In 2003 one virtual `Display()` had to both *walk the hierarchy* and *issue the draw*. The moment you
want to defer and bucket, those must be different operations, and encoding them as two class families
is the honest way to say so in C++. This split is not gratuitous decoration — it *is* the deferred
renderer, expressed in the type system. It also lines up exactly with the file format: `0x00123000`
CompositeDrawable → `0x00123001` prim entries → `0x00010000` Mesh → `0x00010020` PrimGroup is
hierarchy/hierarchy/hierarchy/leaf.

Two warts worth fixing on the way past [J]:

* `GameDrawableInfo` (`displaylist.h`) is an *empty class* used purely as a type-erased back-pointer
  which `Display_List::AddContainerElement` immediately downcasts to `DisplayListPrimitive*`. That is
  obfuscation, not abstraction — it exists only so `pure3d::` need not name a `renderer::` type.
* Fade state (`Geometry::isFading`, `fadeAmount`, and `DrawableHierarchy::SetFading`) lives on the
  **shared asset**, and `Display_List::AddContainerElement` reads it with
  `container->IsFading()`. Two `Renderable`s sharing one `Geometry` cannot fade independently. It
  does not bite in Scarface because world geo is unique per placement and instanced props use a
  per-instance `MatrixPacket::fade` instead — but it is a layering error, and in a reimplementation
  instance state belongs on the instance.

### 2.2 `tCompositeDrawable`'s element subclasses → `ActivePrimitiveList` / `ActiveControllerList`

2003 (`p3d/anim/compositedrawable.hpp`): a `tPtrDynamicArray<DrawableElement*>` of heap-allocated
polymorphic elements —

| class | payload | `Draw(tPose*)` |
|---|---|---|
| `DrawablePropElement` | `tDrawable* prop; int poseIndex` | `stack->PushMultiply(pose->GetJoint(poseIndex)->worldMatrix); prop->Display(); Pop();` |
| `DrawablePoseElement` | `tDrawablePose* skin` | `skin->Display(pose)` (already in pose space) |
| `DrawableEffectElement` | `tEffect* effect; int poseIndex` | joint push, then `effect->Display()` |

each with 9 virtuals and per-element `{visible, lockVisibility, isTranslucent, sortOrder, tPose*}`.

2006 collapses this into two flat arrays plus an index map:

```cpp
class CompositeDrawable::ActivePrimitiveList : public NonCopyable {
    i32 numPrims;
    Array2<i32> usageMap;                 // parallel to primArray → index into primMap
    Array2<i32> primMap;                  // [numPrims] → index into primArray (the VISIBLE subset)
    Array2<ActivePrimitive> primArray;    // { DrawableContainer* drawable; i16 id; bool isVisible; }
    void UpdateMaps();                    // compacts the visible subset
};
```

plus a separate `ActiveControllerList` for the frame controllers that used to hang off each element.

**Verdict: essential, and a net *reduction* in complexity. [J]** This is the textbook
array-of-virtuals → SoA-with-index-compaction refactor, and it is driven straight by the data: state
props toggle sub-drawables per state via `0x08020002` VisibilityData (1975 visible / 1146 hidden
entries in z04 [M]), and `0x00123001` carries a per-primitive `isVisible` flag. With 1009 state-prop
templates each holding a dozen sub-drawables, per-element vtables and heap objects are the wrong
shape. `UpdateMaps()` makes "iterate the visible ones" O(visible) instead of O(all) with a branch.
The polymorphism that was lost (prop vs skin vs effect) was real but small — it is recoverable from
the primitive's own type, and the joint attachment became `ActivePrimitive::id` (the 16-bit pose
index read from the `0x00123001` `{ptr, u16 id}` pairs [V]).

### 2.3 The `renderer::Renderable` family + `RenderableHandle`

~20 subclasses: `WorldGeoRenderable`, `StatePropRenderable`, `InstanceRenderable`,
`CharacterRenderable`, `VehicleRenderable`, `SkyRenderable`, `ShadowRenderable`, `ZonePkgRenderable`,
`LightingRenderable`, `OceanRenderable`, `WakeRenderable`, `SkidmarkRenderable`, `RainRenderable`,
`MaskRenderable`, `NISRenderable`, `PlugInRenderable`, `ParticleEffectRenderable`,
`TraceFireRenderable`, `PropRenderable`, plus the matching `*Loader`s.

**Verdict: the base is right, the family is mostly cruft — but it is *private* cruft. [J]**

Important qualifier, from the leak: the game never names any of these types. It holds a
`RenderableHandle*` and calls `Renderable_SetTransform` / `_SetVisible` / `_Destroy`. So the family
is an internal dispatch mechanism, not an API surface, and the cost of getting it wrong is confined
to the engine. That said, it is still doing less work than its size suggests:

* `SkyRenderable` adds four fields, one of which is the constant `1000`. `ZonePkgRenderable` adds a
  pointer array and *no behaviour*. `LightingRenderable::Display` is a **nullsub** — a renderable
  that renders nothing, which exists solely so a chunk loader can return one object and the inventory
  can hold it. `ShadowRenderable`, `StatePropRenderable` and `WorldGeoRenderable` override only
  `dtor`, `SetVisible`, `Display` and one or two slots out of 16.
* `Renderable::SetName` is `ret 4` — **a no-op stub in the retail build** [V]. The whole name API is
  dead weight; identity is the 32-bit `GetHash` UID and nothing else.
* Type dispatch is done **three ways at once**: the C++ subclass, `typeMask` at +0x54 (a bitfield;
  `renderer::AddInstance` checks `typeMask == 0x200` before downcasting to `InstanceRenderable`), and
  `content::LoadInventory::DynamicCaster<T>` doing a real `__RTDynamicCast`. Three type systems for
  one job.

`RenderableHandle` (12 bytes, `Renderable*` at +4) is the one clearly-earned addition: a weak handle
so `StatePropManager` can keep references to renderables that the streaming system may destroy under
it. Essential for streaming. Keep the idea.

The reason the family exists at all is structural, not architectural: **`SimpleChunkHandler::LoadObject`
returns exactly one `IRefCount*` with exactly one UID, and the inventory is keyed by (uid, C++ type).**
So every chunk id that wants to be findable by name has to be a distinct class. That is the loader
system dictating the class hierarchy — and it is the sharpest concrete instance of aap's "opinionated
enough to make it hard to build your own".

### 2.4 `Display_List` with 84 render lists vs `tDisplayList` + `RenderLayer`

2003 ordering, in full:

* `RenderEnums::LayerEnum = {GUI, PresentationSlot, LevelSlot, MissionSlot1, MissionSlot2}` — **5
  layers**, drawn back-to-front by descending index.
* Inside the world layer, `WorldScene::RenderScene` rebuilds two `std::vector`s **every frame** from a
  spatial-tree walk: `mpZSorts` (opaque, cap 5000, keyed by `IEntityDSG::GetShaderUID()` for state
  coherence) and `mpZSortsPass2` (translucent, cap 5000, keyed by `mRank` = view depth), plus
  `mpZSortsPassShadowCasters` (cap 300). Then `RenderOpaque()`, `RenderSimpleShadows()`,
  `RenderTranslucent()` iterate them and call `Display()`.

2006 ordering:

* Each `DrawablePrimitive` gets a `layer` **at load time** via `SetLayer` (retail `0x00703300`,
  `[prim+0x0c] = layer`), from a shader-type table in `WorldGeoLoader::LoadObject`
  (`SIMPLE`/`CBVLIT`/`LAYERED`/`DECAL`/`ENV`/`SPECULAR`/`FOAM`/`NIGHTLIGHT`/`SHADOWDECAL`/
  `UNTEXTURED`/`VERTEXFADE` × lit × alphaTest × blend → layer 0..44), and from special cases in
  `SkyLoader` (28/38/39), `ShadowLoader` (2), `InstancePrimitive::ctor` (40/41/42),
  `StatePropManager::Register` (3, the night-light layer).
* `Display_List::AddContainerElement` maps `(layer, isFading, isLit, isALUM, shaderType, blendMode,
  GetSomeMask())` → one of **84** intrusive lists.
* `Display_List::Playback` (`0x0045e680`, vtable slot 9) walks the 84 in a hand-written order — one
  2697-byte function containing every sub-pass, each ending in `RenderList(list, doFade)`
  (`0x00459910`); 113 `RenderList(n)` calls in aap's reconstruction.
* **The lists are retained for the `Renderable` path.** `DisplayListPrimitive::SetVisible`
  (`0x00458f00`) submits only when `visible && !isInList`; `Hide` (`0x00458ea0`) calls
  `Display_List::RemovePrimitive`. The world matrix is captured into the node at submit time
  (`Multiply(*matrix, context->GetWorldMatrix())`) and re-applied at playback. **But it is a hybrid**:
  `Display_List::Clear` (`0x0045b0c0`) runs at the end of `Playback` and recycles every node whose
  `+0x5c` marker is zero, which is how the immediate-mode scenes share the same 84 lists (§1.3).
* **Sorting is per-bucket, on demand.** The finalize step `0x0045ad10` z-sorts only the lists flagged
  dirty. So the model is: bucket by material at load time; sort *inside* a bucket only for the
  buckets that need depth order, and only when something changed.

**Verdict: the mechanism is essential, the expression is accidental. [J]**

Essential: at SHR's scale a per-frame tree walk producing ≤5300 entries and a sort is fine. At
Scarface's scale it is not. z04 alone holds 420 `WorldGeoRenderable`s over 27 164 prim groups, 1009
state-prop templates, 47 631 instance placements [M]; aap's `Display_List` preallocates 200 000
entries. A retained bucket list turns the per-frame cost into *visibility deltas + a linked-list
walk*, which is right. Precomputing the material bucket at load time is also right when the cost you
are minimising is fixed-function state changes across PC/PS2/Xbox.

The one thing that is *not* essential and is worth noticing: retail still walks every `Renderable` in
the scene every frame to decide visibility (§1.3). The clever part (the retained list) sits on top of
a completely naive part (a linear scan of the world). With ~420 world-geo renderables per zone set
that is affordable; it is also the obvious place to put back a coarse grid if the reimplementation
ever loads more of Miami at once than retail did.

Accidental: the sort key is spread over **three hardcoded tables in three places** — the loader's
shader-type switch, `AddContainerElement`'s 45-case switch, and `Display()`'s hand-written call
sequence — none of which is data. There is no single place in the engine that says "here is the
render order". That is exactly why aap's `renderer/display_list.cpp` needed a 130-line ASCII table in
a comment to make sense of it. The fix is one array (§4).

A second consequence worth naming: because the matrix is captured into the draw record at submit
time, **the ambient `pddi` modelview stack stopped being the carrier of the frame.** In 2003 the
frame was a traversal with ambient state; in 2006 it is a list of self-contained draw records
(`{matrix, primEntry, shader}`) replayed with `context->SetWorldMatrix(draw->matrix)`. That is the
other half of "growing a spine", and it is the half that generalises.

### 2.5 `pure3d::DisplayList` became abstract

2003's `DisplayList` is a concrete 4 KB helper owned by `tCompositeDrawable`. 2006's
`pure3d::DisplayList` is an `Entity` with pure-ish virtuals `AddContainer`,
`AddContainerElement`, `Display`, implemented by `renderer::Display_List`.

**Verdict: correct dependency inversion, done half-way. [J]** `pure3d::` now defines the *submission
protocol* and lets the game define the *ordering policy* — good. But the protocol leaks the policy
anyway: `DrawablePrimitive::layer` (the policy's key) lives in `pure3d::`, and `GameDrawableInfo`
exists only to avoid naming `renderer::DisplayListPrimitive` from `pure3d::`. Either commit (put the
sort key in the submission call) or don't (let `pure3d::` own the list). Don't do both.

### 2.6 `radload` → `content::`

SHR's `tRefCounted` is literally `class tRefCounted : public radLoadObject {}`; `tEntityStore` is
`radLoadHashedStore`; `tFileHandler`/`tChunkHandler` are `radLoadFileLoader`/`radLoadDataLoader`;
`tLoadManager`/`tLoadRequest` are commented `// Legacy classes` and forward everything to
`radLoad->…`. In 2006 the shared middleware was folded into the engine's own `content::` namespace
with the same shapes (`LoadManager`, `LoadRequest`, `LoadInventory`, `P3DFileHandler`,
`SimpleChunkHandler`, `ChunkFile`, `LoadStream`).

**Verdict: mostly a rename, and a good one [J]** — the 2003 arrangement had two vocabularies for one
mechanism, which is worse. Two substantive changes came with it, though, and they are §3's material:
the inventory lost sections and gained a parent chain, and lookup gained a type filter.

### 2.7 `Key32` — the hash

| | 2003 | 2006 |
|---|---|---|
| function | `radMakeKey` (`libs/radcore/inc/radkey.hpp`), *"Pure3D Algorithm"* | `core::GetHash` (retail `0x006dc190`) |
| width | 64-bit `tUID` = `tUidUnaligned {u32 u0, u1}` (split to dodge PS2 `long` alignment) | 32-bit `core::Key32` |
| body | `key *= 65599; key ^= *p;` | `key = key*65599; key &= 0x7fffffff; if (c < 'a') c += 0x20; key ^= c;` |
| case | **sensitive** for entity names (`radMakeCaseInsensitiveKey` is used only for file extensions) | **insensitive**, by a blind `c < 'a' ? c+0x20 : c` fold that also mangles digits and punctuation |
| tag | none | `return key \| 0x80000000` (and the empty string returns the seed unmodified) |
| seeding | `radMakeKey(p, keyValue)` — a running hash | same: `MakeKey32("InstanceShape", modelUID)` == `MakeKey32("<model>InstanceShape")` |

**Verdict: essential. [J]** Halving the key saves 4 bytes on every entity and every hash slot, times
~10^5 entities — a real streaming-era decision. The `| 0x80000000` tag makes 0 a usable "no name"
sentinel. The seeded form is load-bearing: the renderer looks up
`MakeKey32("InstanceShape", t->mModelUid)` without ever building the concatenated string [V].
The masking and the tag are exactly the two things aap's `core.cpp` was missing, and `core::GetHash`
is now correct. Worth noting that the `core::MakeKey` aap kept alongside it (x65599 + XOR, no mask,
case-sensitive) is precisely SHR's `radMakeKey` truncated to 32 bits — so if joint UIDs really do
hash that way in Scarface, the *old* algorithm survived inside the skeleton/animation subsystem while
the inventory moved to the new one. Worth confirming; if true it is a nice example of the chunk
format freezing an implementation detail forever.

Note the price: a 31-bit key over ~10^5 names makes collisions plausible, and Scarface's data
*deliberately* collides — a state prop's `StatePropData`, its `CompositeDrawable`, its
`ravenphysics::CollisionObject` and its `renderer::StatePropRenderable` **all share the same name and
therefore the same UID**. `StatePropDataLoader` relies on it: `inv->Find<CollisionObject>(dr->uid)`
finds the collision object using the *drawable's* uid [V]. Which is why:

### 2.8 The inventory key became (uid, C++ type)

2003: `tInventory` is an array of up to 256 **named sections** (`section[0]` hard-wired to
`"default"`), with a section stack (`PushSection`/`PopSection`, depth 16) and an optional
8-entry section *path* for the search order. Lookup:
`tInventory::Find(SafeCastBase& caster, tUID)` searches the current section, then (unless
`currentSectionOnly`) the section path or every other section, and within a section walks the hash
collision chain calling `caster.safe_cast(obj)` until one accepts. `p3d::find<T>(store, name)`
searches the per-file temp store first, then falls back to `p3d::context->GetInventory()`. At load
completion the per-file store is **transplanted** into the named section via
`radLoadHashedStore::Dump(store)` and emptied.

2006: sections are gone. `content::LoadInventory` has a single table and a **parent pointer**;
`Find(DynamicCaster*, uid)` walks the collision chain doing `__RTDynamicCast`, then recurses into
`parent` [V]. One inventory per `.p3d` package, parents chained in load order.

**Verdict: a clear improvement, forced by streaming. [J]** Sections were a global namespace with a
manual search order; the parent chain is a scope chain. More importantly it changes the *unit of
unloading*: in 2003 a file's objects dissolve into the global inventory at load time and can only be
removed one at a time; in 2006 the inventory *is* the package, so releasing it releases the package.
That is the single change that makes zone streaming tractable. The type filter survived both eras
unchanged (`SafeCast<T>` → `DynamicCaster<T>`) and is **not optional** — see §2.7.

### 2.9 World-geo / LOD / zone data moved into engine chunks

2003: the game owned its own chunk ids in its project block (`SRR2::ChunkID::ENTITY_DSG`,
`INSTA_ENTITY_DSG`, `TREE_DSG`, `WORLD_SPHERE_DSG`, `FENCE_DSG`, …) and its own loaders
(`IWrappedLoader : tSimpleChunkHandler`, ~20 of them in `code/render/Loaders`), which wrapped p3d
loaders and then called back into `RenderManager::OnChunkLoaded(entity, userData, id)` where
`userData` packed `{LayerEnum in the low byte, GutsCallEnum in the high byte}` to pick which
`RenderLayer::AddGuts(...)` overload to call. The world model was *code*.

2006: the renderer's own chunk family lives in the engine and is registered in one function
(`InstallHandlers`, retail `0x00467b60`) [V]:

| id | loader | produces |
|---|---|---|
| `0x08800000` | `renderer::CharacterLoader` | `CharacterRenderable` (composite + LOD composite + 8 shaders) |
| `0x08800001` | `renderer::VehicleLoader` | `VehicleRenderable` |
| `0x08800002` | `renderer::SkyLoader` | `SkyRenderable` |
| `0x08800003` | `renderer::WorldGeoLoader` | `WorldGeoRenderable` (name, composite, type) |
| `0x08800004` | `renderer::ZonePkgLoader` | `ZonePkgRenderable` + `0x08800009` per-worldgeo draw distances |
| `0x08800005` | `renderer::SFStatePropLoader` | `StatePropRenderable` (binds to `prop::StatePropData`) |
| `0x08800007` | `renderer::SFLightGroupLoader` | `LightingRenderable` |
| `0x08800008` | `renderer::ShadowLoader` | `ShadowRenderable` |
| `0x0880000a` | `occlude::OccluderLoader` | `occlude::Occluder` |
| `0x08800112` | `LoadPackageIdentifierLoader` | the package's self-identification record |
| `0x0802000d` | `pure3d::prop::StatePropDataLoader` | `prop::StatePropData` (v4: states, visibility, frame controllers, user data, effects) |
| `0x0990019x` | `ScriptObjectDataLoader` / `GameGroupDataLoader` | script objects; `0x09900194` = 48-byte preprocessed instance placements |

The `0x08800009` entry is the whole LOD system in six floats: `{drawDist min, max, fade, unused,
otherPosition.x, -otherPosition.z}` applied to a named `WorldGeoRenderable` via
`SetElementDrawDist(0, …)` plus an optional 2-D *alternate distance reference point* [V]. The
`0x08011000`–`0x08011005` family is the streaming zone descriptor, including a per-cell
visibility/LOD byte grid and a zone-name → index table.

**Verdict: essential, deliberate, and the most important thing to copy. [J]** This is what makes a
1.5 GB world load without *renderer* code: a package is self-describing. (Gameplay setup is not — the
package also carries compiled TorqueScript in `0x08800104` and the load pump execs
`SZO_<packagename>()` from `zone_setup.cso` at the end of load-completion (§3.4). But nothing in the
*render* path needs it.) `re/notes/ps2.md` shows the
`0x088000xx` and `0x0990019x` payloads are **byte-identical between PC and PS2** [V] — only the mesh
data below `0x00010000` differs (PS2 uses one pre-swizzled `0x00010012` memory-image vertex list
where PC has separate `0x00010005/6/7/a` lists). The world description is genuinely
platform-independent; the vertex data is not, and pddi's memory-image prim buffers are the seam.

It is also the source of the "hard to build your own" feeling, and there is no way around it: if you
want to read Scarface's data, you inherit Scarface's world model.

### 2.10 Things that did *not* change

Worth stating, because it is evidence about which parts were right:

* **pddi is essentially unchanged 2003 → 2006** [V]. `pddiBaseContext`, `pddiBaseShader`,
  `pddiRenderState`/`ViewState`/`FogState`/`LightingState`/`StencilState`, `pddiMatrixStack`,
  `pddiExtension` all survive with the same names; Scarface only added extensions
  (`d3dExtInstancing`, `d3dExtHardwareSkinning`, `d3dExtFramebufferEffects`, `d3dExtTODFactor`,
  `d3dExtVertexProgram`, …). An abstraction that survives three years, five platforms and a complete
  rewrite of the layer above it was the right abstraction. **The reason is that pddi abstracts
  resources and state, not scenes** — it has no opinion about worlds, so it had nothing to be wrong
  about.
* The `PrimGroup` family is identical apart from spelling (`Optimised` → `Optimized`) and one
  addition (`VertexAnimPrimGroup`). `p3d/primgroup.cpp` (72 KB) is still the best available
  documentation of the vertex formats.
* `ChunkFile` is a near-exact port: same `struct Chunk {id, dataLength, chunkLength, startPosition}`,
  same `CHUNK_STACK_SIZE = 32`, same `BeginChunk`/`EndChunk`/`BeginInset`/`EndInset`, same
  `chunkLength > dataLength` container test.
* Animation is the same design: `Animation` → `AnimationGroup` → channels (with new compressed
  quaternion variants on PS2), `FrameController` → `Simple`/`Blend`/`Animation`, `MultiController`.

---

## 3. The chunk-loader system

### 3.1 How it works (both eras)

The format. Chunk header = **3 × u32: `id`, `dataLength`, `chunkLength`** — both lengths include the
12-byte header; `startPosition` is synthesized. A chunk **has children iff `chunkLength >
dataLength`**. File magic `0xFF443350` (`P3D\xFF`), with `_SWAP` and `_COMPRESSED` variants that flip
endian-swapping or install LZR decompression on the stream. Strings are Pascal: one length byte then
the bytes (Scarface pads to a multiple of 4). `BeginChunk()` seeks forward to
`parent.startPosition + parent.dataLength` before reading the child header; `EndChunk()`
*unconditionally* seeks to `startPosition + chunkLength`. Nesting is capped at 32.

The loader. `LoadManager` holds two maps — extension → `FileLoader`, chunk id → `ObjectLoader` —
populated by `AddHandler`. A load creates a `LoadRequest` plus a fresh `LoadInventory` whose parent
is the caller-supplied resolver inventory. `P3DFileHandler::LoadFile` walks **only the top level** of
the chunk tree; for each top-level chunk it looks up one handler by id and calls

```cpp
SimpleChunkHandler::LoadObject(IRefCount** pObject, u32* pUID, ChunkFile*, LoadInventory*)
```

and stores the single returned object with `inventory->Add(uid, object)`. Everything below the top
level is hand-written recursion inside each loader — `while (ChunksRemaining()) { BeginChunk();
switch (GetCurrentID()) {...} EndChunk(); }` — with a jump table where the compiler felt like it
(`StatePropDataLoader` has three: `0x68fd94`, `0x68fda0`, `0x68fdb0` [V]).

Name resolution happens **during the parse**: `inventory->Find<T>(GetHash(name))`, walking the parent
chain (2006) or the section path then the global inventory (2003).

Scale check [M]: z04's 220 packages contain **146 distinct chunk ids, of which 38 occur at top
level**. So ~38 registered handlers describe the whole world, and the other 108 ids are parsed by
hand-written switches inside them.

### 3.2 What is good

1. **The format.** Skipping an unknown chunk is free and safe. Per-chunk versioning works and was
   used: `StatePropData` v1→v4 each added a count field plus a child id, and the loader is written
   `if (version >= 3) numGlobalUserData = rd_u32();` [V]. Forward and backward compatibility are
   structural rather than conventional. Keep this verbatim.
2. **The id space is administered, and the administration is documented.** From
   `constants/chunkids.hpp`: *"Each project will be allocated a 24 bit block of IDs… The top level
   chunk ID list is the only piece of information that has to be managed across teams… There is room
   to accomodate 256 'project blocks'"*, and for sub-allocation *"an allocation scheme similar to how
   IP addresses are allocated… Huge sub-system - 4096, Major sub-system - 256, Minor sub-system -
   64"* with a `// next free 0x…` comment at each level. This is a better story than most engines
   have, and it is why `0x00023000` (Skeleton) and `0x00010020` (PrimGroup) could be allocated by
   Scarface inside blocks SHR had reserved but not used.
3. **The handler table is the spec.** One handler per id, unknown = skip, means "what does this
   engine understand?" is answerable by reading one function — and it is, at retail `0x00467b60`.
4. **Inventory-per-file with refcounted contents** gives you a natural unload unit and a natural
   scope chain (2006), and cross-chunk references are just AddRef'd pointers.
5. **`radLoadDataLoader::LoadData` has an `originalObject` hot-reload parameter** — *"When an object
   attempts to reload itself, it will pass in it's current self"*. Pure3D declines it
   (`if (originalObject) return originalObject; // Right now there's no re-loading any pure3d
   objects`), but the hook was designed in, which is more foresight than the rest of the system shows.

### 3.3 What is opinionated, and where it hurts

1. **Load-time name binding.** `Find<T>(hash(name))` runs *while the chunk is being read*.
   Consequences:
   * **Chunk order inside a file is significant.** `0x00123000` CompositeDrawable must precede
     `0x0802000d` StatePropData; the `0x08800009` zone entries must come after the
     `WorldGeoRenderable`s they annotate. Radical controlled the exporter, so this was free for them
     and is a trap for anyone else.
   * There is **no fixup/patch pass**, so loading cannot be parallelised or reordered, and a forward
     reference silently produces nothing: `StatePropDataLoader` does `if (!dr) return;` — the chunk
     evaporates with no diagnostic [V].
   * Cross-file references work only through the parent chain, so **the order in which you load
     packages is part of the content design**. aap's `p3dview.cpp` already has to chain 34 "common"
     files in a fixed order for this reason.
2. **One handler per chunk id, process-wide.** Two subsystems cannot both care about a chunk. The
   retail workaround is that handlers are registered from all over the place — `NISManager::ctor`,
   `FileHandlerManager::ctor`, `CVManager::ctor`, `sub_55c1a0` — so **which chunks the engine
   understands depends on which managers have been constructed** [V]. That is a hidden global
   initialisation-order dependency masquerading as a plugin system.
3. **The framework walk is one level deep.** There is no generic chunk tree; there are ~38 bespoke
   parsers. Fast (you read exactly what you need, once, streaming) but it means an unknown *child*
   id is swallowed by the parent's `default:` and is invisible unless the parent knows about it — and
   tooling has to reimplement every parser.
4. **`LoadObject` returns exactly one object with exactly one uid, and that is the only extension
   point.** radload even asserts it (*"This will also find chunk loaders which are storing more than
   one object (which is not allowed with radLoad)"*). Anything that wants to do something else has to
   cheat by reaching into a global:
   * `OccluderLoader` invents a name — `sprintf(name, "occluderobject%d", ++g[0x8110a0])` [V].
   * `LightingRenderable`'s ctor registers with the global light manager and its `Display` is a
     nullsub.
   * `SFStatePropLoader` calls `StatePropManager::Register`, which walks the drawable and rewrites
     layers.
   * `WorldGeoLoader` registers with a manager and calls `SetFadeDist(g->…() ? 3000 : 0)`.

   **This is the thing that makes it hard to build your own.** Not the chunk format — the loader
   signature, which forces every non-trivial loader to be a global-state mutation with a return value
   bolted on.
5. **The parse is single-threaded by construction.** Loader objects are shared singletons registered
   once, and the `ChunkFile*` for the load in progress is held *on the loader* — aap's port has
   `SimpleChunkHandler::chunkFile` with the comment *"so no multithreaded loading..."*, and the
   retail loader is 0x14 bytes with an otherwise-unexplained slot at `+0x0c` that looks like exactly
   that [V for the comment, [?] for the retail field]. In 2003 the mechanism underneath is explicitly
   cooperative: radload's "load thread" is one mutex, `SwitchTasks()` = `Unlock();
   radThreadSleep(0); Lock();`, a strict-FIFO 32-entry queue with `SetPriority` ignored, and
   `tFileFTT` yields every 5.2 ms (`gYieldTime`) from inside its 192 KB double-buffered read-ahead.
   2006 replaced the I/O side (`core::File`/`core::Drive`/`core::CementLibrary`, and the whole
   `StreamManager` layer above it) but there is no sign the *parse* ever became parallel — and the
   game-side post-load pump is likewise one package at a time (§3.4).

### 3.4 What actually drives the loader in 2006 — streaming 1.5 GB

The leaked game layer answers the question the disassembly cannot: **the game does not drive
streaming, it is a client of it.** [V]

* `StreamManager` owns a graph of `StreamPackage`s. Which ones it wants is decided by position:
  `StreamManager::PushObjectOfInterestId(GetID())` from `characterobject.cpp:2224`
  (*"Let the streamer know that it should be following the main character"*) and push/pop around NIS
  cameras. Subzone triggers used to be game objects — `load/subzonetrigger.cpp` still exists but the
  whole file is `#if 0`'d; that logic moved into `StreamManager`.
* A `StreamSlot` is a **named memory region** (`"dzone"`, `"region_s"`) with its own allocator.
  `LoadPackage::Load()` is `StreamSlot::Find(mSlotName); mStreamPackage->UpdateLoadPriority(slot);
  mStreamPackage->Activate(slot);`
* **An inventory's lifetime is exactly its stream package's residency in its slot**:
  `InventoryId LoadPackage::GetLoadInventory() const { return mStreamPackage->GetInventoryId(); }`.
  This is why §2.8's change from sections to one-inventory-per-package matters — it is the whole
  unload story.
* The protocol is push, on two virtuals of `class LoadPackage : public GameGroup, public StreamClient`:
  ```cpp
  virtual bool StreamConfirm(EStreamEvent, void*);   // may I?
  virtual void StreamNotify (EStreamEvent, void*);   // I did.
  // Load, Unload, Reload, RecreateClient, CreatingData, DataCreated, CancelLoad,
  // LoadComplete, ReloadComplete, CreateComplete, UnloadComplete, Delete
  ```
* **The post-load pump is the interesting part, and it is entirely time-sliced.** After I/O
  completes, `LoadPackage::LoadComplete()` takes a *global* lock —
  `if (!sLoadCompletionContext->Lock(this)) return;`, with the comment *"Only one LoadPackage can be
  in the load completion phase at a time. The StreamPacakge will continue calling LoadComplete until
  we are able to acquire the LoadCompletinContext."* [V, typos original]. It then sets a 5 ms (subzone)
  or 20 ms budget and, over subsequent frames, `LoadPackageLoadCompletionContext::Update()` walks the
  package's object array calling `pObject->CodeInit()`, checking the clock every
  `c_NumObjectsBeforeTimesliceCheck = 5` objects and yielding when over budget. When the array is
  drained it execs `SZO_<packagename>()` from the package's `zone_setup.cs` and finally calls
  `mStreamPackage->FinishCreating()` — *"after timeslicing code, only the load package can let the
  streampackage know when everything is done and can proceed loading the next streampackage"*.
  **The game layer is the backpressure valve on the streamer.** [V]

This is the part of the design that does not appear in the 2003 engine at all, and it explains the
three otherwise-odd 2006 decisions: the abstract `DisplayList` (submission must survive an asset
disappearing), `RenderableHandle` (a weak handle across an unload), and the retained draw list
(re-submitting everything after every zone swap would be unaffordable).

One anti-pattern to *not* copy [J]: the only way the game finds out which assets a freshly-loaded
package contains is to sweep the entire inventory with `dynamic_cast` —
`InstanceObject::NotifyLoadPackageComplete` does `content::LoadInventory::RawIterator` +
`dynamic_cast<pure3d::prop::StatePropData*>` over every object, with the author's own comment
`// SLOW FIX ME - find a quicker way of searching inventory` [V]. A per-package index built during
the parse costs nothing and removes this entirely.

And the detail that best illustrates "the data format is the API" [V]: `InstanceObject::CreateTemplate`
builds a `StatePropTemplate` by **parsing artist-authored key/value strings** out of
`prop::StatePropData::GetGlobalUserData` — `view_distance`, `sway`, `wakeup_radius`, `movement`,
`mass`, `hit_points`, `surface_type`, `time_of_day`, `life_cycle`, `plugin_templates` — and it
dispatches on `fieldValue[0]`, the *first character*: `'T','S','M','B','L','H'` → 40/60/100/200/300/500 m
draw distance; `'S','N','E','M','G'` → sway 1..5 (matching the retail sway table
`{0.0, 0.5, 1.0, 4.0, 50.0, 200.0}` at `0x0073bab0`). The eco-prop system is a string-keyed schema
living inside a Pure3D chunk, not an engine feature.

### 3.5 How a modern engine would do it

| | Pure3D | modern |
|---|---|---|
| parse | bespoke per-type reader, runtime | offline bake; load = read bytes, fix up offsets, or position-independent data with no parse at all |
| references | name hash resolved inline, during the parse | dependency list in the package header → loader fetches deps first, in parallel, off-thread; pointer fixup in one pass at the end |
| type identity | `dynamic_cast` probing down a hash chain | explicit type hash in the record header |
| extensibility | one global handler per id | a registry keyed by (type hash, version), or no registry at all because the data is the struct |
| ordering | significant | irrelevant |
| threading | one cooperative parser | N worker threads, one package per job |
| unload | inventory release (2006) — **Pure3D got this one right first** | package refcount, same idea |
| versioning | per-chunk version int + skip-unknown — **also fine** | same, or schema evolution |

The honest summary: **the chunk system is very good as a file format and merely adequate as a loader
architecture.** Keep the format and the reader; replace "loader does `Find()` inline and pokes
globals" with "loader emits a struct with unresolved keys; a separate pass binds them".

---

## 4. Recommendations for the reimplementation

### 4.1 Keep verbatim — the data format forces it

These are not design choices. Getting them wrong means not reading the data.

* **`content::ChunkFile`** — the 3-u32 header, the 32-deep stack, `chunkLength > dataLength`,
  `EndChunk`'s unconditional seek, `BeginInset`/`EndInset`, pstring = u8 len + bytes padded to 4.
  Already correct in `chunkfile.cpp`.
* **`core::GetHash`** exactly as in `re/notes/renderables.md §12`: `key*65599`, `& 0x7fffffff` **every
  iteration**, the `c < 'a' ? c += 0x20` fold, final `| 0x80000000`, empty string returns the seed.
  And keep the **seeded** form — `MakeKey32("InstanceShape", modelUID)` is how the renderer finds
  instance meshes without building a string.
* **Inventory keyed by (uid, C++ type), with a parent chain.** The type filter is load-bearing: a
  state prop's `StatePropData`, `CompositeDrawable`, `CollisionObject` and `StatePropRenderable` all
  hash to the same key on purpose.
* **`DrawableHierarchy` (submit) / `DrawablePrimitive` (draw)**, `Geometry : DrawableContainer`,
  `PrimGroup : DrawablePrimitive`. Mirrors `0x00123000 → 0x00123001 → 0x00010000 → 0x00010020` and is
  the right shape for a deferred renderer anyway.
* **`CompositeDrawable::ActivePrimitiveList`'s two-array + index-map structure**, including the 16-bit
  pose id on each `ActivePrimitive`.
* **The whole `prop::StatePropData` family as plain data** — `StatePropData` / `StateData` /
  `VisibilityData` / `FrameControllerData` / `EventData` / `CallbackData` / `UserData` / `EffectData`,
  field for field, with the `version >= 2/3/4` gates.
* **`SLocationData`'s 24-byte bit packing** and the matrix convention
  `M = Scale · Rot(-rotX, -rotY, +rotZ) · Translate(pos)` with `y/100` and rotations in **degrees**.
* **The layer-assignment rules** (shader type × blend × lit × alphaTest → layer 0..44) and the
  `details_` / `cbvlitdecals_` / `skyline_` / `shells_` / `underwater_` / `low_LOD_` name prefixes.
  These are baked into the art; there is no deriving them.
* **The `0x08800009` six floats** as the LOD/draw-distance record, including the negated Z on
  `otherPosition`.

### 4.2 Simplify — these are Scarface's accidents, not the data's

* **Collapse the `Renderable` family.** Keep the base concretely:
  `struct Instance { Matrix matrix; Array<Element> elements; u32 flags; }` with
  `Element { float dmin, dmax, dfade; Submission sub; bool fading; }`. Make WorldGeo / StateProp /
  Sky / Shadow / Character / Vehicle **loader outputs and a small enum tag**, not 20 subclasses.
  Then delete `typeMask` — one type system is enough.
* **`LightingRenderable` and `ZonePkgRenderable` are not renderables.** A light group is a light
  group; a zone package is a table of (world-geo, draw distances). Give them their own homes owned by
  the zone. The only reason they are `Renderable`s is §3.3's loader signature — and you control the
  loader signature.
* **Delete the name API.** Retail already did (`SetName` is `ret 4`). Keep a debug-only name behind a
  flag, as SHR did with `P3D_USE_ENTITY_NAMES` / `P3D_ALLOW_ENTITY_GETNAME`. Identity is the key.
* **Replace `GameDrawableInfo`** — an empty class that is always downcast — with a concrete
  `Submission*` (or an index into the draw-list's record array). Type-erasure that has exactly one
  implementation is not abstraction.
* **Make the render order data.** Replace the 45-case `AddContainerElement` switch + the 113
  `RenderList(n)` calls with:
  ```cpp
  // key computed once, at load: (materialClass, blend, lit, alphaTest) -> bucket 0..83
  static const u8 kBucketOf[45][8];        // layer x (fading|lit|alum) -> bucket    [data]
  static const u8 kRenderOrder[84];        // the order Display() walks them          [data]
  ```
  aap's `display_list.cpp` already contains both tables — in a comment. Turn the comment into the
  code and the comment becomes the documentation.
* **Move per-instance state off the shared asset.** `isFading` / `fadeAmount` belong on the
  `Element`, not on `Geometry`. (Retail gets away with it; you will not, the moment you instance
  anything.)
* **Change the loader signature** so no loader needs a global. Instead of
  `LoadObject(IRefCount**, u32*, ChunkFile*, LoadInventory*)`, pass a `LoadContext&` carrying the
  inventory, the package being built, a deferred-fixup list, and an output sink that accepts *zero or
  more* objects. Then `OccluderLoader` appends an occluder to `ctx.package.occluders` instead of
  inventing a name; `SFLightGroupLoader` appends a light group; `ZonePkgLoader` writes draw distances
  into a table instead of mutating `WorldGeoRenderable`s it had to find by name.
* **Split parse from bind.** Loaders record `Key32`s; a `Resolve(package)` pass after the file is
  read binds them. This costs one array per package and buys: order independence, real diagnostics
  for missing references, and the ability to load packages on a worker thread.
* **Build a per-package index during the parse** (`package.statePropData[]`, `package.geometry[]`,
  `package.worldGeo[]`). Retail's alternative is a `dynamic_cast` sweep of the whole inventory on
  every package load, and the author knew it (§3.4).

Three things from the 2006 *façade* that are better than the 2006 *internals*, and are worth
adopting even though aap is reimplementing the internals [J]:

* **Keep the model/instance split explicit.** `renderer::ModelHandle` (archetype) vs
  `RenderableHandle` (placement) is the right pair; the internal `Renderable` hierarchy blurs it
  (a `WorldGeoRenderable` is both).
* **Separate the rendering camera from the culling camera** (`View_SetRenderingCamera` /
  `View_SetCullingCamera`). Free to build in now, painful to retrofit, and it is what makes
  debug-camera and mirror/reflection passes tractable.
* **Two scenes, not N layers.** The whole game runs on `GAMEPLAY_SCENE` and `GUI_SCENE` with
  `Scene_Enable(scene, bool)`. SHR's five `RenderLayer`s were mostly used as an on/off switch; don't
  reinvent them.

### 4.3 The spine, on one page

The sentence to design around:

> **The frame is a delta on a retained, material-bucketed draw list, driven by a distance test. It is
> not a scene-graph traversal.**

Everything odd about Scarface — `isInList`, `SetLayer` at load time, 84 lists, sorting only the dirty
buckets, no octree, `RemovePrimitive` walking a per-primitive child list — falls out of that sentence
as a consequence. Say it out loud and you can rebuild it cleanly instead of reverse-engineering it
shape by shape.

(Retail hedges the "delta" with a second, immediate path: nodes tagged transient are flushed by
`Display_List::Clear` at the end of playback. That is a reasonable escape hatch for HUD and one-shot
geometry — build it in from the start rather than discovering you need it.)

```
  LOAD (rare, off-thread)
    package.p3d ──ChunkFile──▶ loaders ──▶ Inventory(uid,type) ──parent──▶ parent inventory
                                  │                                 (Common ← region ← shell ← detail)
                                  └──▶ Package {
                                         assets:   Geometry, CompositeDrawable, Shader, Texture,
                                                   Skeleton, StatePropData      (immutable, shared)
                                         world:    Instance[]  (worldgeo, stateprop, sky, shadow)
                                                   InstancePlacement[]          (0x09900194)
                                                   LightGroup[], Occluder[]
                                         lod:      per-instance {dmin, dmax, dfade, refPoint}
                                       }
                                  └──▶ Resolve(package)     // bind Key32 → pointer, once

  WORLD
    Zone = set of loaded Packages.  Add/Remove is the streaming unit.
    Instance = { Matrix, Element[] }.  Element = { dmin, dmax, dfade, Submission, fading }.
    Everything else is a tag.

  FRAME
    1. stream     packages in/out          → zone.Add/Remove(Instance)          [rare]
    2. animate    controllers → poses       (only for things that have one)     [per frame]
    3. visibility for each Instance element  (retail: a flat array, walked in full):
                     sphere vs frustum        (cullCamera, separate from renderCamera)
                     vs the active occluder set                    (built last frame)
                     d = |refPoint - cam|, 2-D XZ squared          → LOD / fade band
                     visible = inFrustum && !occluded && dmin <= d < dmax
                  → produce only the CHANGES
                  (eco-prop placements are a separate, time-sliced pass: 350/frame)
    4. submit     on change only:
                     became visible → drawable->Submit(list, handle)
                                      → list.bucket[key].push({matrix, prim, shader})
                                        mark bucket dirty
                     became hidden  → list.remove(handle)                        [O(children)]
                  immediate submitters (HUD, one-shots) push TRANSIENT nodes into the same buckets
    5. finalize   for each dirty bucket that is depth-ordered: sort it
                  rebuild the active occluder set for the next frame
    6. draw       for b in kRenderOrder:
                     for e in list.bucket[b]:
                        context->SetWorldMatrix(e.matrix); e.prim->Draw();
                        → pddi: shader->SetMaterial(pass); pddi->DrawPrimBuffer(...)
                  then drop the transient nodes
    7. present    context->End()

  ORTHOGONAL (do not put these in the traversal)
    occluders        CPU volume test that flips whole Instances off       (0x0880000a)
    per-batch cull   frustum-test each MatrixPacket inside one instanced draw call
    instancing       one InstancePrimitive per prop model, N matrix packets, one draw call
```

Three properties to hold onto while building it:

1. **Submission is idempotent and rare.** Nothing touches the draw list unless a visibility bit
   flipped. This is what makes 47 000 eco props and 27 000 prim groups affordable, and it is why the
   draw record carries its own matrix instead of relying on an ambient matrix stack.
2. **Assets are immutable and shared; instance state lives on the instance.** Pose, fade, visibility
   mask, per-instance tint. Retail violates this in two places (`Geometry::isFading`,
   `CompositeDrawable`'s pose) and works around it with clones — don't inherit the workaround.
3. **The ordering is one table.** If you cannot print the render order as an array of 84 numbers, you
   have the 2006 design, not a better one.

### 4.4 Sequencing, concretely

Roughly in the order that unblocks the most:

1. `core::GetHash` (mask + tag) — everything keys off it. *(Verified already correct in `core.cpp`.)*
2. Make `Display_List`'s two tables data (`kBucketOf`, `kRenderOrder`). Cheap, and it turns the
   biggest pile of magic numbers in the codebase into something reviewable.
3. Wire `0x00122000` (the `{u32 0, float}` sort-order chunk, 8275 in z04, value 0.5 in 24/27
   samples [M]) into `DrawableContainer` — it is SHR's
   `DisplayList::Add(drawable, matrix, sortOrder = 0.5f)` promoted to a per-drawable chunk, and it is
   aap's `// float unknown` / `// float containerUnk`.
4. Register an ignore-loader for `0x00007000`, `0x00007030-32`, `0x0001001d` exactly as
   `p3d/loaders.cpp:104-107` does, so the log shows only real gaps.
5. Split parse from bind (§4.2) *before* adding more loaders — every loader added under the old
   signature is one more global to untangle later.
6. Give the world a home. Right now `p3dview.cpp` keeps a `std::vector<renderer::Renderable*>` and
   walks all of it every frame, and `RegisterInstanceShape` is a global shape registry filled by the
   viewer. That is fine as scaffolding, but it is the place where the `Zone` / `Package` of §4.3
   belongs, and putting it in now costs less than retrofitting it after the loaders multiply.
7. Then port breadth from SHR, highest value per byte first:
   `p3d/primgroup.cpp` (72 KB, all the vertex formats), `p3d/anim/channel.cpp` (51 KB, every channel
   type), `p3d/anim/animate.cpp` (50 KB), `p3d/lightloader.cpp`, `p3d/effects/particleloader.cpp`,
   `p3d/billboardobject.cpp`.

---

## 5. Verdict on the original impression

* *"the class hierarchy is rather more complicated at the 3D layer"* — **half true.** `pure3d::` has
  163 classes against SHR's ~310 declared in `libs/pure3d/p3d`; the 3D layer did not grow. What grew
  is `renderer::` (58 classes), which in 2003 was 42 kLOC of game code that nobody counted as engine.
  Within that, the `tDrawable` split and `ActivePrimitiveList` are *simplifications* wearing extra
  class names; the 20-deep `Renderable` family is the genuine cruft — but the leaked game source
  shows it is hidden behind a 110-function C façade over an opaque handle, and the game derives from
  an engine class in only four places in 1903 files. The hierarchy is an internal dispatch
  mechanism, not an interface. What is genuinely *complicated to work with* is not the class count;
  it is that the sort key is distributed across three hardcoded tables and that every non-trivial
  loader mutates a global.
* *"it's not clear that was a good idea"* — **mostly it was.** The two big refactors (submit/draw
  split, SoA composite elements) are exactly what you do when you move from immediate traversal to a
  deferred bucketed list at 100× the world size. The bad idea was doing it without ever naming the
  spine, so the rationale survives only as a shape.
* *"the pddi layer looks pretty reasonable"* — **yes, and the evidence is stronger than it looks.**
  It survived 2003 → 2006 essentially untouched across five platforms and a full rewrite above it,
  because it abstracts resources and state rather than scenes. Its costs are real (a 120-method
  fixed-function god-object; `#define pddiPrimStream ps2PrimStream` to escape per-vertex virtual
  dispatch; a parity fiction where gl ignores shader names and DX8 silently downgrades by caps) but
  they are 2003 costs, not design errors.
* *"the general chunk loader system is not too bad either"* — **agreed, with one correction:** the
  *format* is genuinely good (versioned, skip-safe, administered id space); the *loader* is where the
  trouble is, and specifically the `LoadObject` signature that forces global state.
* *"lacking a good spine while being opinionated enough to make it hard to build your own"* —
  **this is the right diagnosis with the wrong location.** The spine exists, and it is concrete — one
  function, `RenderFlowClient::OnRender` (`0x00465590`), doing cull-and-build then draw, on one
  thread, with the display list as the only command buffer. It is just not in `pure3d::`, and the
  policy it encodes is spread across three hardcoded tables instead of one. The opinionation that
  actually constrains you is not in the C++ at all — it is that by 2006 the world model had moved
  into the file format. You cannot load Scarface's data and keep your own world model; you can only
  decide what sits on top of it. §4.3 is a proposal for what that should be.

---

## 6. Open questions worth one disassembly session each

Things this study still asserts on partial evidence, in rough order of how much they'd change §4.
(The three biggest — is the list retained, who walks the Renderables, what do occluders do — were
answered by tracing the retail frame and are now folded into §1.3.)

1. **The semantics of `Display_List` node `+0x5c`.** Read here as "persistent vs transient" (it comes
   from `dc->vt[0x4c]()` at `0x45d595`, and `RemovePrimitive` zeroes it), which is what makes
   `Clear()` recycle exactly the immediate-mode nodes. If it means something else, §2.4's hybrid
   story needs redoing. **This is the single highest-value follow-up.**
2. **Which of the 84 lists each of the ~25 helpers inside `Playback` (`0x0045e680`) draws.** Purely
   mechanical — each helper ends in `Display_List::RenderList(list, fade)` (`0x00459910`) — and it
   would turn aap's reconstructed order from "plausible" into "exact". Worth doing before anything
   else in `display_list.cpp`.
3. **What the three non-`GamePlayScene` scenes are for** (ctor type masks 1, ?, 3; drawn
   immediate-mode via `DrawableContainer::Display` `0x006834c0`). Probably sky/pre and HUD/post.
4. **Is `core::MakeKey` (x65599 + XOR, case-sensitive, unmasked) really what skeleton joints hash
   with?** If so, two hash functions coexist in the 2006 format (§2.7) and both must be implemented.
5. **Where does per-instance tint go?** `instances.md §5.3` shows `MatrixPacketList::Add` takes the
   alpha argument and never reads it on PC. Either the tint is applied elsewhere, or it is dead on
   PC and live on PS2.
6. **`0x0001001d`** (constant `(1, 3, 1, 0)`, on 24 % of meshes) and **`0x00010021`** (seven `0x20`
   bit widths — the vertex compression hint) should differ on PS2 assets. Confirming that would
   nail down the PS2 vertex path without reading VU code.
7. Occluder timing: `BuildActiveSet` (`0x00461270`) runs *after* the cull walk, so frame N appears to
   use frame N−1's occluder set. Confirm — it changes whether a reimplementation needs the same
   one-frame lag to match retail's popping behaviour. [G]
