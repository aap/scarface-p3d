# `renderer/` — the retail render spine in code

This directory is a reimplementation of Scarface's `renderer::` namespace, named after the
retail PC classes and methods. Every non-obvious function carries a
`// retail: renderer::Foo::Bar 0x4xxxxx` comment; the addresses are unpacked-image VAs and
the reasoning behind them is in `re/notes/` (start with `displaylist.md`, then
`rendercore.md` and `renderable_classes.md`; `re/names/*.txt` are the name lists).

## Who owns what

| file | classes | in one line |
|---|---|---|
| `render_manager.*` | `RenderManager`, `Scene`, `GamePlayScene`, `Canvas`, `RenderableHandle` | the owner and the frame |
| `renderable.*` | `Renderable`, `DisplayListElement`, `DisplayListPrimitive` | "am I visible, and how faded" |
| `display_list.*` | `Display_List`, `DisplayListNode` | the retained draw list, 84 buckets |
| `view.*` | `Camera`, `View_Get/SetRenderingCamera`, `View_Get/SetCullingCamera` | where we look from, and the frustum |
| `worldgeo.*`, `zonepkg.*`, `instance.*` | `WorldGeoRenderable`, `ZonePkgRenderable`, `InstanceRenderable` and their chunk loaders | the three renderable classes the viewer actually loads |

* **`RenderManager`** is *not* a renderer. It owns 4 scenes, 2 canvases and (in retail) 12
  memory heaps, the time-of-day manager and the decal / skid-mark / tracer pools.
* **`Scene`** is *not* a scene graph: a fixed-size array of `Renderable*` slots plus a
  typeMask filter. Adding one is "find the first nil slot"; a full scene drops it silently.
* **`GamePlayScene`** (scene 0) is the only scene with a `Display_List`, i.e. the only one
  that does deferred, sorted, batched drawing. Scenes 1..3 are the HUD/frontend layers and
  draw immediately through `Renderable::RenderImmediate`.
* **`Canvas`** is *not* a render target: a view plus animated fog. `RenderScene` is "tick
  the fog fade, then tell the scene to render".
* **`Renderable`** never draws. It is a matrix plus N `(drawable, draw-distance band)`
  slots, and once a frame it decides visible/faded and pushes the surviving drawables into
  the display list.
* **`RenderableHandle`** is a weak reference with a generation check (`uniqueId`). The game
  never holds a `Renderable*`.

## The frame

```
p3dview RenderScene()                      (retail: RenderFlowClient::OnFrame 0x465590)
  RenderManager::Update(t)                 0x467810   scenes 0,1,2,3 in index order
      GamePlayScene::Update(t)             0x468c50
          per renderable: Renderable::Tick (age + UpdateFade, even when hidden)
                          Renderable::Update  (per-class pre-display hook)
                          Renderable::Display 0x4740f0
                              distance band -> frustum -> occluders -> fade amount
                              DisplayListPrimitive::Display(true)  0x458f00
                                  drawable->Display(g_displayList, prim)
                                      Display_List::AddContainer(Element) 0x45d360/0x45d3b0
          Display_List::SortAllLists       0x45ad10
  DestroyPendingRenderables()              0x464ac0   always between Update and Render
  RenderManager::Render(t)                 0x4689a0
      Canvas::RenderScene -> GamePlayScene::Render -> Display_List::Render  0x45e680
```

`Renderable::Display` is a *state machine, not a draw call*: it decides visibility and
fade, and only `DisplayListPrimitive::Display(bool)` ever touches the display list. That
call is edge triggered — it submits when the primitive becomes visible and withdraws when
it stops being visible — so **a static, continuously visible object is submitted once,
ever**, and every later frame just walks a linked list. At Scarface's scale (3000
renderables and 44000 eco-prop placements in the viewer, far more in the game) that is the
whole point.

The corollary is the thing that trips people up: **the world matrix is baked into the node
at submit time**. Anything that invalidates it — the renderable moved (`isMatrixDirty`), or
its fade state flipped — has to call `DisplayListPrimitive::RemoveFromList()` to force a
re-submit.

## The 84 lists

The `layer` (0..44) baked into each `DrawablePrimitive` at load time is a *material class*
(shader type x lit x alpha-test x blend). `Display_List::AddContainerElement` expands
`(layer, isFading, isLit, isALUM, shaderType, blendMode, mask)` into one of 84 buckets, and
a bucket is "a set of primitives that want exactly the same pddi state and the same place
in the draw order". `Render()` then walks the buckets in a hand-written order, setting the
state once per bucket instead of once per object. Roughly:

| group | lists | what |
|---|---|---|
| sky / camera-locked | 46, 47, 76 | first and last, z-test and z-write off, fog off |
| low-LOD / skyline | 59, 2 | the distant city silhouette; never distance culled |
| unclassified opaque world | 49, 50, 52, 53, 54 (fading 51, 55, 56) | the bulk of the streamed world |
| material buckets | 21..24, 26..30, 35..39, 43 (fading 25, 31, 32, 40, 41, 44, 45) | lit / cbvlit / unlit buildings |
| layered (interiors) | 42, 43 (45, 44) | the two-texture-layer shader |
| interior floors | 33, 34 | unlit alpha blend |
| specular road/ground | 13, 15 (fading 14, 16) | drawn twice, early and again after the shadow volumes |
| decals | 3, 4, 17, 18, 75 (fading 5, 6, 19, 20) | z-write off |
| shadows | 7, 8, 77 (decals), 61..64 (stencil volumes) | |
| environment / reflection | 9, 10 | stencil tested, z-write and alpha-write off |
| night lighting | 11, 12 | z-write off, fog forced off |
| instanced eco props | 72, 73, 74 | the pddi instancing extension in retail |
| water | 65, 66 (0, 1 foam: never drawn on PC) | |
| underwater | 78, 79 (fading 80, 81) | |
| special light sets | 67..71, 82, 83 | per-node light-set selection |
| depth only | 58 | a pure z-fill, colour write off |
| unused | 48, 57 | no writer, no reader |

Three details that are easy to get wrong:

1. `listDirty[]` is a **dirty flag**, not "has content". `AddContainerElement` sets it, the
   sort pass sorts the list and clears it again, so an untouched list is not re-sorted. The
   seven lists whose key depends on the camera (65, 66, 69, 70, 71, 82, 83) are sorted
   unconditionally.
2. **Every list walk culls again per node** (`IsNodeVisible`): the retained list is not
   "visible geometry", it is "geometry that was visible when it was submitted".
3. Four sort policies, and which list gets which matters:
   `CmpShader` (pure material batching) for the opaque buckets, `CmpKeyThenDepth`
   (far to near) for the blended ones after `ComputeDepthKeys`, `CmpKeyThenMaterial` for the
   decals and light sets, `CmpKey` for the sky. `sortKey` is the fade/priority class
   (0.0, 0.5, 1.0, and 1.0 for anything fading), so fading geometry is always drawn before
   non-fading geometry in the same bucket.

## Coordinates in the viewer

p3dview draws the world with **x flipped**, and it sets that flip as the world matrix before
the frame. `Renderable::Display` loads its own (native) matrix over it while the nodes are
submitted, exactly like retail, so **node matrices are in native file coordinates**; the
list walks in `Display_List::Render` push and multiply onto the flip again, which is
precisely the mechanism retail's reflection pass uses (`RenderReflection` puts a mirror
there instead of the identity). The culling camera therefore also lives in native
coordinates: `View_GetCullingCamera()` gets the position with x negated and a frustum taken
out of `flip * view * proj`.

## What is deliberately missing

No reflection pass, no occluders (`occlude::IsBoxVisible` is a hook that always says
"visible"), no light sets, no stencil shadow volumes, no shader-mode extension
(`ext(0x10b)`) and no hardware instancing — the eco props are drawn one placement at a
time. The indoor/outdoor deferral of group (B) in `Render()` exists but
`Display_List::cameraIndoors` is never set. `RenderManager::GetHeap` returns nil: there are
no pools.
