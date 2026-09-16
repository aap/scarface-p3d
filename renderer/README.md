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
| `worldgeo.*`, `zonepkg.*`, `instance.*`, `sky.*` | `WorldGeoRenderable`, `ZonePkgRenderable`, `InstanceRenderable`, `SkyRenderable` and their chunk loaders | the four renderable classes the viewer actually loads |
| `lighting.*` | `LightingRenderable`, `SFLightGroupLoader`, `LightManager` | which of the game's own lights the frame is lit with |

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

## Two Display paths for world geo

A `details_` / `cbvlitdecals_` / `skyline_` / `shells_` / `underwater_` composite is a whole
city block or a whole island shell; one bounding sphere for the lot decides nothing useful.
Retail therefore gives `WorldGeoRenderable` its own `Display` (0x471640, `worldgeo.cpp`) that
skips the base entirely and walks `primitives[]` / `poseIDs[]` — one `DisplayListPrimitive`
per sub-drawable of the composite, built by the loader. Each sub-drawable is transformed by
its own pose matrix out of the composite's pose table, distance tested, frustum culled and
faded **on its own**, and becomes its own display list node; element 0 (the composite) is
never submitted on that path. `low_LOD_` and plain (unprefixed) world geo still go through
`Renderable::Display`. The reversed loop is `re/notes/renderspine.md` §4.5; the differences
from the base that matter are:

* the distance is measured to the sub-sphere's **surface** and is not clamped at 0, where the
  base measures to a reference *point*;
* the near/far band is **not** the zone package's `drawDistMin/Max/Fade`. It is one of three
  globals chosen by the kind — 120 m for details, 1500 m for shells, 3000 m for the skyline at
  the highest of the three "DrawDistance" video settings (`SetWorldGeoDrawDistanceLevel`) —
  with a fade band of 20 / 50 / 80 m and no near distance, so a sub-primitive never fades *in*;
* the renderable-wide fade is combined with `max()`, not with the base's `alpha*(1-g) + g`;
* the "am I already fading" edge state is the sub-drawable's own `IsFading()`, because there is
  no `DisplayListElement` per sub-primitive.

One deviation is left in the **base** `Renderable::Display`: it measures the draw distance to
the element's bounding sphere surface instead of to the reference point. Retail's point works
because the huge composites never reach the base; the plain world geo that does reach it in the
viewer has an identity matrix and no `otherPosition`, so the point would be the world origin.

## The sky

`sky.*` is `renderer::SkyRenderable` (chunk `0x08800002`, typeMask 1) and its loader;
`re/notes/sky.md` has the reversed chunk formats and the retail behaviour. The short
version:

* `Common.p3d` holds two of them, `("sky","sky")` and `("rainy_skybox","rainy_skybox")`.
  The loader looks the composite up, gives every **billboard quad group** in it layer 28
  and every other primitive layer 39 (or 38 when the name starts with `rainy_`), clears
  `doDistanceTest` and sets the renderable's matrix to a uniform **2x scale**. The rainy
  box is loaded already faded out (`SetToFadeOut(40)`) and goes into the RenderManager's
  *secondary* sky slot; the weather code cross-fades the pair through
  `Renderable::Display`'s `TYPE_SKY` branch.
* Layer 39/38 are display lists **46 and 47**, drawn first of everything with z-test and
  z-write off, fog off, and **translated to the rendering camera's x and z** (y stays 0),
  so the sky turns with the camera but does not rise with it. Layer 28 is list **76**,
  drawn near the end, translated to the camera's *full* position — that is the sun, the
  sun flares and the stars, which sit at infinity. Neither walk culls per node.
* `SkyRenderable::Update` is retail's throttle (every frame for the first second, then
  every 15th) around one job: set every frame controller of the sky composite to
  `numFrames * timeOfDay`. The only one we can drive is the sky boxes' vertex colour
  animation, which is the one that matters — the meshes' own vertex colours are the
  *night* sky and the animation adds the daylight back in. `renderer::g_timeOfDay` is
  that 0..1 phase (`P3D_TIMEOFDAY`, default 0.25); `renderer::g_skyEnabled` is retail's
  global on/off switch.
* The billboard quad groups themselves are `pure3d::` (`billboard.h`/`.cpp`, chunk
  `0x00017006`): a `BillboardObject` container whose single `BillboardQuadGroup`
  primitive builds all its quads into one triangle stream every time it is drawn, facing
  the camera.

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

## Lighting

`lighting.*` is `renderer::LightingRenderable` (chunk `0x08800007`, `SFLightGroupLoader`)
and `renderer::LightManager` (`gLightManager`, retail `g[0x008111cc]`); the `pure3d::Light`
/ `LightGroup` / LITE-animation side is the top-level `light.*`. The whole story, with the
chunk formats and which rules were read out of the disassembly, is in
`re/notes/lighting.md`.

The short version. A `LightingRenderable` **draws nothing**: its only job is to hand its
`pure3d::LightGroup` to the light manager, which files it by the chunk's `kind`:

| kind | n in z04 | what | where it goes |
|---|---|---|---|
| 0 | 2 | `zone_lights` — the daylight (sun, fill, ambient, building ambient) | `zoneGroup` |
| 1 | 2 | `zone_rainlights` — the same four for rain | `zoneRainGroup` |
| 2 | 30 | one per `*_detail` package: exterior night lights | `exteriorGroups` |
| 3 | 116 | one per `*_shell` package: the lights of one interior | `interiorGroups` |
| 4 | 1 | `lights_template`: lamp / headlight prototypes | `templateLights` |

Once a frame (from `RenderManager::Update`, as retail does at `0x0046786a`)
`LightManager::Update` rebuilds the active set:

1. play the four `LightAnimationController`s of the active zone group onto their lights —
   this **is** the time of day: 241 frames = 24 h, and at noon the sun is `99, 97, 72` from
   `(-0.47, -0.74, 0.47)` and the ambient `63, 52, 31`;
2. take every light of the zone group except `MiamiBuildingAmbientShape` (retail excludes
   it too — it belongs to the `buildinglights` night windows);
3. add the local lights **that have a decay range** and whose decay at the camera is not
   zero, within 50 m — the same radius and the same "has a decay range" test retail uses;
4. accumulate the ambient lights into one colour and push the rest into the pddi slots
   through `pddiContext::SetAmbientLight` / `SetLight` / `EnableLight`.

The "has a `0x00013006` decay range chunk" test is the whole trick: the four global
sun/ambient lights are the only lights in the game without one.

`gl/shaders/shader.vert` consumes 4 slots, directional or point (point lights use the
decay range as the falloff), and `glShader::SetPass` no longer sets any light of its own.
The View tab's **Lighting** header shows the active group, the hour, the ambient and every
light with its colour and direction, and `game lights` off restores the old hardcoded
`51,43,27` / `97,95,70`. Env knobs: `P3D_TIME=<hours>`, `P3D_RAIN=1`,
`P3D_NOGAMELIGHTS=1`; `P3D_VERBOSE=1` prints the active set once.

What is missing is `pure3d::LightsChooser`: retail reduces the world lights to four
directional lights **per lit object**, so a lamp only outshines the sun for the car next to
it. The viewer has one set per frame, chosen at the camera, and therefore keeps the zone
lights in the first slots. `re/notes/lighting.md` §6 lists the rest.

## What is deliberately missing

No reflection pass, no occluders (`occlude::IsBoxVisible` is a hook that always says
"visible"), no per-object light sets (see Lighting above), no stencil shadow volumes, no
shader-mode extension (`ext(0x10b)`) and no hardware instancing — the eco props are drawn
one placement at a time. The sky draws, but not everything on it moves: there are no frame
controllers, so the sun's and the stars' `BillboardQuadGroupAnimationController` never runs,
the cut-off cones that fade a flare out as you look away from it are parsed but not
evaluated, and only the two ends of a primitive fade (0 and 1) are honoured, not the middle.
The indoor/outdoor deferral of group (B) in `Render()` exists but
`Display_List::cameraIndoors` is never set. `RenderManager::GetHeap` returns nil: there are
no pools.
