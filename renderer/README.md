# `renderer/` — the retail render spine in code

This directory is a reimplementation of Scarface's `renderer::` namespace, named after the
retail PC classes and methods (`p3dview/README.md` is how to *run* the thing: the game's
`cement.rcf`, the keys and the `P3D_*` environment). Every non-obvious function carries a
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
| `shadow.*` | `ShadowRenderable`, `ShadowLoader` | the 0x08800008 building shadow composites (loaded, not drawn — see below) |
| `lighting.*` | `LightingRenderable`, `SFLightGroupLoader`, `LightManager` | which of the game's own lights the frame is lit with |
| `render_manager.*` | `FogParameters`, `EnvManager` | the distance fog, per time of day |

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
  sun flares and the moon, which sit at infinity. Neither walk culls per node.
* List 46 is sorted by `CmpKey`, descending, and its key is the drawable container's, out
  of the mesh's `0x00122000` chunk: the dome 1.0, the two cloud layers 0.3/0.2, the
  horizon gradient 0.0. Without that chunk everything is 0.5 and the dome paints over the
  gradient — the whole sky is then one flat colour, which it is *not* meant to be (the
  dome alone is: all 65 of its vertices carry the same colour).
* `SkyRenderable::Update` is retail's throttle (every frame for the first second, then
  every 15th) around one job: set **every** frame controller of the sky composite to
  `numFrames * phase`, then `Hide()`. There are three kinds (`re/notes/sky.md` §4):
  `PTRN_sky`, a pose animation that turns the skeleton's `sun_grp` joint and carries the
  sun, its two flare stars, the two lens flares and the moon across the sky; one `BQG_*`
  per billboard quad group for their colour, size and visibility (the sun's six-key colour
  curve, the moon's day/night visibility); and one `VRTX_*` per sky box mesh, picking the
  vertex colour offset set. The `Hide()` matters: the pose moves the billboards and a
  display list node caches the world matrix it was submitted with.
* **The sun's rest pose is 755 m underground** — the whole day is `sun_grp`'s rotation, so
  a viewer that does not play `PTRN_sky` draws the sun in the ground.
* `renderer::GetTimeOfDay()` is the 0..1 phase, and it reads
  `renderer::LightManager::timeOfDay` (hours): there is **one** clock, so `P3D_TIME=20`
  gives an evening sky, an evening sun and evening light together. `P3D_TIMEOFDAY` still
  works and sets the same clock as a fraction of a day. `renderer::g_skyEnabled` is
  retail's global on/off switch.
* The billboard quad groups themselves are `pure3d::` (`billboard.h`/`.cpp`, chunk
  `0x00017006`): a `BillboardObject` container whose single `BillboardQuadGroup`
  primitive builds all its quads into one triangle stream every time it is drawn, facing
  the camera. A `BillboardCutOffQuad` also runs `Calculate()` there, which evaluates the
  `0x1700a`/`0x1700b` cones and fades the quad out as it leaves the middle of the screen.
* `pure3d::Animation` and the frame controller family live in `anim.h`/`.cpp` (chunk
  `0x00121000` with all nine channel types, and `0x00121201`); `light.*` keeps the
  `LITE` controller, `billboard.*` the `BQG` one, `geometry.*` the `VRTX` one and `anim.*`
  the `PTRN` one. The composite drawable collects the controllers of its elements into its
  own list, which is retail's `CompositeDrawable +0x48`.

## The ocean

`ocean.*` is `renderer::OceanRenderable` / `OceanContainer` / `OceanPrimitive` and the
`pure3d::Ocean` behind them (`ocean.h`/`.cpp` at the top level); `re/notes/ocean.md` has the
reversed engine and where every number comes from. The ocean is a **singleton**: there is one
`OceanObject` in the whole game, z04's, and it builds the renderable out of
`OceanTemplateDefault` with `renderer::OceanRenderable_CreateInstance(reflectionTexture,
detailTexture, foamTexture, inventory)` — which p3dview calls in `InitApp` once the common
libraries are loaded, with the template's own `"skyTexture"` / `"water_01.BMP"` /
`"Water_Ocean_Foam.tga"`. The primitive carries **layer 27**, which
`AddContainerElement` maps to display list **60**; that list is walked in group 12 of
`Render()` with z-write on and **fog on**, so the water takes the horizon's fog colour like the
rest of the world. The container's sort key is 0.5, `CalcBounds` parks the bounding sphere on
the camera with radius 100000 and the renderable has `doDistanceTest` and `doFade` cleared, so
the ocean is never culled and never fades.

The sea is at **y = 0** — `pure3d::Ocean::GetSeaLevel` returns a float that nothing in the
whole image ever writes. `pure3d::WaveModel` is retail's: sixteen wave trains generated from
the six `OceanTuningTemplate` numbers by the PRNG at `0x6a8180` (wavelengths spread by the
**cube** of `i/15`, so the short chop lands in the first slots), each living 15 s with a 2 s
smoothstep fade in and out and then respawning, and `Ocean::Update` ticking them **twice per
frame** exactly as retail does. With the shipped values **the tallest wave in the game is
16 cm**: what you see is the shading of the normals, not the displacement. Retail hands only
the first **four** trains to the surface and gets its visible ripples from an animated EMBM
bump map; we have no bump map, so all sixteen displace the grid.

The grid is retail's too — a **projected grid**: rows are evenly spaced *angles from straight
down*, so the tessellation is uniform on screen, densest right in front of the camera,
stretching to the far plane, with one clamped row for the horizon. Retail builds three of them
once (50/110/170 quads across) and lets `Scale(cameraHeight)·RotateY(cameraYaw)·Translate(camXZ)`
and the vertex shader do the rest; we rebuild it on the CPU every frame because the height
field is evaluated there, and fade out any wave the local quad cannot carry. Where retail draws
one pass with a d3d effect and four texture stages, the viewer draws two through a pddi prim
buffer: an untextured "reflection" pass with z-write off, then the `ocean_text` shader out of
`Common.p3d` alpha blended over it at `DetailOpacity` (0.225) with the detail texture tiled at
`DetailTextureScale` (0.2, one tile per 5 m). There is no reflection render target, no foam and
no specular; `re/notes/ocean.md` §6 lists every deviation, including the two anti-aliasing
knobs the missing mip maps force on us. View tab > Ocean has the switches (and a live table of
the sixteen trains), `renderer::g_oceanEnabled` is the master one.

## The static shadows

Three unrelated things (`re/notes/shadows.md` has the whole reversal):

1. **The shadow decals** — the dark blobs baked into the ground geometry under trees,
   awnings and walls, as prim groups whose pddi shader is `shadowdecal`. `WorldGeoLoader`
   puts them on layer 37, so they land in display lists **7**, **8** (while their world geo
   cross-fades) and **77**, and `Display_List::RenderShadowDecals_7_8_77` draws them with
   z-write off after the decals and before the lit world. **These are on by default.**
   Retail brackets the pass with `pddiExtStaticShadowGen::Begin/End` (pddi extension
   `0x108`) and accumulates it as an *alpha mask* that is cleared to 0 and written with
   colour write = alpha only, then multiplies the frame by that mask with one
   fixed-function full-screen quad. The viewer does the same through
   `pddiContext::Begin/EndStaticShadows`, with the mask in the frame buffer's **own** alpha
   channel (`SDL_GL_ALPHA_SIZE = 8`) instead of a render target of its own: the decals'
   `PDDI_BLEND_ALPHA` accumulates `mask = c·c + mask·(1-c)`, so a decal of coverage `c`
   darkens by `c·c`, overlapping decals saturate instead of multiplying, and one quad
   applies `frame *= 1 - strength·mask` at the end of the pass. `strength` defaults to
   **0.5**, the extension's own `0xff808080` wash colour — the only number in the data that
   says how dark a static shadow is meant to get (View tab > Shadows,
   `P3D_SHADOWDECAL=<strength>[,nomask]`). Without destination alpha it falls back to
   painting `strength·c²` of black per decal, which is the same thing for one layer.
   Two things worth knowing: retail's own `End()` draws its composite quad into the scratch
   render target with the alpha-only write mask still set and only *then* restores the
   frame's render target, so **the retail PC build shows no static shadow decals at all**
   (`re/notes/shadows.md` §2.3) — the viewer implements what the pass was written to do, not
   what it does; and the polarity is the complement (`1 - mask`), because the mask holds
   coverage and is cleared to 0.
2. **The building shadows** — the 44 `0x08800008` `*_shadow` composites, one per shell that
   casts them. `shadow.*` loads them the way retail does (layer 2 and `isBuildingShadow` on
   every primitive, no distance test, element 0 = the composite, `Display` gated on
   `g_buildingShadowsEnabled` = retail's `g[0x7c0b46]`), and the layer-2 case of
   `AddContainerElement` routes them to list **61**. They draw nothing: their drawables are
   `0x0001001a` `pure3d::ShadowMesh` chunks — closed shells plus `0x0001001b` edge topology,
   i.e. **stencil shadow volumes**, extruded per frame away from the sun with a 6 m volume
   length and drawn in two cull/stencil passes plus a `0xff191919` wash. Nothing of that
   exists here, so the composites resolve to zero drawables (`P3D_VERBOSE` prints the
   count) and lists 61..64 stay empty.
3. The blob shadows under cars and NPCs, which need a car or an NPC.

Why the decals used to cut off hard, in order: 397 of the 408 geometries that carry a
shadowdecal prim group are `details_` world geo, i.e. the **120 m** band with a 20 m fade;
the viewer had **no** cross-fade at all until `PDDI_SP_FADE` (`gl/glshader.cpp`), so that
20 m band was a one-frame pop; and only 47 of the 220 z04 packages have any shadow decals in
the first place, which is simply how the map was authored.

Why they used to look like black *rectangles* is a different story and was the arithmetic,
not the data (`re/notes/shadows.md` §5.6 measures it): a decal prim group is the collapsed
ground polygons under several trees, its edge-connected patches each sample **one whole tree
silhouette** out of a 128×128 atlas — with the V flip, verified against the silhouettes'
own bounding boxes — and 437 of the 438 patches at the Little Havana camera name a
silhouette rather than a solid part of the atlas. But 26 % of that atlas is at alpha 1, one
cell of ~35×50 texels covers a 10..20 m ground patch, and the viewer painted 75 % of black
per decal and multiplied again where patches overlap (48 % of the covered ground), so the
dense core of a silhouette became square metres of solid black cut off at the patch
boundary. With the mask and the 0.5 cap nothing is darker than half.

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
| shadows | 7, 8, 77 (decals, drawn), 61..64 (stencil volumes, empty) | see "The static shadows" |
| environment / reflection | 9, 10 | stencil tested, z-write and alpha-write off |
| night lighting | 11, 12 | z-write off, fog forced off |
| instanced eco props | 72, 73, 74 | the pddi instancing extension in retail |
| water | 65, 66 (0, 1 foam: never drawn on PC) | |
| underwater | 78, 79 (fading 80, 81) | |
| special light sets | 67..71, 82, 83 | per-node light-set selection |
| depth only | 58 | a pure z-fill, colour write off |
| unused | 48, 57 | no writer, no reader |

Four details that are easy to get wrong:

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
4. **Z-write is display-list state.** On PC the only per-draw writer of
   `D3DRS_ZWRITEENABLE` is `d3dContext::SetZWrite` (`0x64b990`, context vslot `+0x118`);
   `d3dSimpleShader::SetPass` never touches it, and the `ZWRT` / `ZTST` shader params that
   the eco-prop `.p3d` files carry do not exist in the executable at all (no entry in the
   parameter table at `0x7f0700`), so the viewer is right to ignore them. The lists retail
   draws with z-write off are exactly 46, 47 (sky), 7, 8, 77 (shadow decals), 9, 10 (env),
   3, 17, 18, 4, 75 and 5, 19, 20, 6 (decals), 11, 12 (night) and 39, 30, 24 (additive) —
   `re/notes/zwrite.md` lists every bracket with its address. The one shader that owns its
   own depth state is `d3dShadowDecalShader` (`SetPass` tail-jumps `SetZWrite(false)`,
   `PostRender` restores it), which is why `gl/glshader.cpp` still does that there and
   nowhere else.

### Transparency: the two alpha-punch-through fixes

Both of these are in `re/notes/zwrite.md` with the disassembly behind them.

* **The low-LOD hull** (`islands_LOD.p3d`, world-geo kind `LOW_LOD`, the `vertexfade`
  shader, list 2) is drawn blended *with z-write on*, and its vertex shader fades it out
  towards the camera — so from close up it is a completely transparent hull over the whole
  island. What keeps it out of the depth buffer is an alpha test
  `d3dVertexFadeShader::SetPass` (`0x709a30`) turns on unconditionally: GREATEREQUAL with
  ref 20/255. Without it everything drawn after list 2 disappeared inside the hull — most
  visibly every tree crown behind the boathouse at Tony's mansion, which left the trunks
  standing as bare sticks.
* **The eco-prop foliage.** Retail never draws it through a plain list walk:
  `InstancePrimitive` takes layers 40..42 (lists 72..74) and the pddi instancing extension
  draws every instanced shape **twice** (`d3dExtInstancing` `[+0x10]`, `0x650810`) — a
  colour pass with `SetZWrite(false)` + `SetColourWrite(1,1,1,0)`, then a depth/alpha pass
  with z-write back on, `SetColourWrite(0,0,0,1)` and the shader forced to alpha-test at
  GREATEREQUAL 228/255 (`d3dSimpleShader::SetPass` `0x65c043`, gated on `g[0x830a31]`).
  So the transparent part of a leaf quad never writes depth, and only its near-opaque core
  occludes. Because we have no instancing, `InstanceRenderable` sorts its prims like world
  geo and the blended leaves land in **list 38** (>99% of it), so `RenderUnlit_38_37`
  reproduces the two passes there; `pddiInstancedDepthPass` is the viewer's `g[0x830a31]`.
  Before this, one crown's own leaf quads z-rejected each other inside a single draw call
  and every tree looked half-empty.

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

## Fog

`EnvManager` is retail's environment manager (`RenderManager+0x1c`) reduced to its fog: six
key frames at 4, 9, 12, 18, 21 and 24 h, each with a clear and a rainy `FogParameters`, and
`Update()` interpolates them at `LightManager::timeOfDay` and pushes the result through
`Canvas::SetFog` — exactly the path `View_SetFog` (`0x464e40`) takes in retail. The numbers
are the game's own: the key hours come from the `TODObject` of `packages/z04/miami_lod.p3d`
and the twelve `environment_{clear,rainy}_{hour}` objects from `scriptc/graphanims.cso`, so
noon is fog from 25 to 900 m in `200,200,150` and 4 a.m. is 20 to 800 m in `23,28,40`.
`re/notes/fog.md` has the whole table and how it was read out of the compiled script.

`Canvas::ApplyFog` then does what `pure3d::View::BeginRender` does — `EnableFog`, `SetFog`,
`SetFogClamp` — through the pddi fog API added in `pddi.h`. The retail fog is **linear, per
pixel and by depth** (`d3dContext::SetFog` writes nothing but `FOGTABLEMODE = D3DFOG_LINEAR`,
`FOGCOLOR`, `FOGSTART` and `FOGEND`, and never touches `RANGEFOGENABLE` or `FOGDENSITY`), so
`gl/shaders/shader.frag` blends `mix(fogColour, colour, (end - z)/(end - start))` into the
colour only, over the eye-space depth the vertex shader passes down. Everything is fogged,
including the low-LOD skyline drawn with the vertex-fade shader; `Display_List` brackets the
sky lists 46/47, the camera-locked list 76, the env/reflection lists 9/10, the night lights
(11) and the decal lists 18/4/20/6 with `EnableFog(false)`, the same way and in the same
places retail does.

What retail calls the fog "density" is really `EnvironmentObject::FogClamp`: it is *not* a
D3D state (`d3dContext` does not even override `SetFogClamp`), it only reaches the game's
own vertex programs as `c49 = (start, end, 1.25/(start+end), clamp/255)`, and what they do
with it is not reversed — so the viewer carries it to the shader but leaves it unapplied
unless the View tab's checkbox says otherwise. The View tab's **Fog** header has the enable,
the colour, the start and end and the "game values" switch (off = edit them by hand);
`P3D_NOFOG=1` starts with the env manager out of the way.

## What is deliberately missing

No reflection pass, no occluders (`occlude::IsBoxVisible` is a hook that always says
"visible"), no per-object light sets (see Lighting above), no stencil shadow volumes and no
`ShadowMesh` loader (see The static shadows), no shader-mode extension (`ext(0x10b)`) and no
hardware instancing — the eco props are drawn one placement at a time. The per-primitive
fade reaches the shaders as `PDDI_SP_FADE` and is applied after the alpha test, so the
blended lists cross-fade but a fading *opaque* primitive is still drawn at full strength
until it is gone (retail switches it to alpha blending). In the sky, the uv atlas animation of `0x00017008`, the `0x1700d` size scale
of a cut-off quad and the occlusion query the sun flares use are still missing
(`re/notes/sky.md` §6). The indoor/outdoor deferral of group (B) in `Render()` exists but
`Display_List::cameraIndoors` is never set. `RenderManager::GetHeap` returns nil: there are
no pools.
