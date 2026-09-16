# The sky (PC retail)

Everything the sky is made of lives in `assets/packages/z04/Common.p3d`:
two `renderer::SkyRenderable`s (chunk `0x08800002`), two `pure3d::CompositeDrawable`s
(`sky` and `rainy_skybox`), twelve sky box meshes and six `pure3d::BillboardQuadGroup`s
(chunk `0x00017006`) for the sun, its flares and the stars.

```
0x08800002 sky            -> composite "sky"            6 meshes + 6 billboard groups
0x08800002 rainy_skybox   -> composite "rainy_skybox"   6 meshes
```

---

## 1. `renderer::SkyLoader` / `SkyRenderable` — chunk `0x08800002`

* loader ctor `0x00477660`, vtable `0x00738628`, **`LoadObject` `0x00477680`**
* renderable vtable `0x007385e4`, dtor `0x004774e0`, size `0x98`, `typeMask = 1`
* `Display` `0x00477450` (vslot 10), `Update` `0x00477500` (vslot 9),
  `SetMatrix` = `0x438400`, the no-op stub

### Chunk layout                                                    **[V]**

```
0x08800002 { pstring name; pstring compositeName; }
```

Both strings are the ordinary p3d "length byte, then that many bytes, the length already
padded so that 1+len is a multiple of 4" form.

### Class layout                                                    **[V]**

```
SkyRenderable : Renderable                       (0x98)
  +0x54  typeMask = 1
  +0x84  pure3d::CompositeDrawable *composite   (AddRef'd)
  +0x88  bool isRainy       strstr(compositeName, "rainy_") == compositeName
  +0x8c  i32 tickCount
  +0x90  i32 timer = 1000    (ms)
  +0x94  i32 lastTime
```

`"rainy_"` is `g[0x007c0c64]`, a plain `char*` in .data.

### `LoadObject` `0x00477680`                                       **[V]**

```c
name  = GetString();  *uid = GetHash(name);            // 0x6dc230
sky   = new SkyRenderable;                             // Renderable(0), typeMask 1,
                                                       // +0x90 = 1000, the rest 0
compositeName = GetString();
sky->isRainy  = strstr(compositeName, "rainy_") == compositeName;
if (sky->isRainy) { flags80 |= 2 /*doFade*/; sky->SetToFadeOut(40.0f); }  // 0x473ac0
else                flags80 &= ~2;
composite = inventory->Get<CompositeDrawable>(GetHash(compositeName));
for (ActivePrimitive p : composite->primList)                  //   composite +0x44
  for (PrimEntry e : p.drawable->prims) {                      //   container +0x44
      if (dynamic_cast<BillboardQuadGroup>(e.prim)) {
          if (group->occlusion /*+0x82*/) group->+0x90 = runningIndex++;
          e.prim->SetLayer(28);                                // -> display list 76
      } else
          e.prim->SetLayer(sky->isRainy ? 38 : 39);            // -> list 47 / list 46
      for (fc : e.frameControllers) fc->[0x0d] = 0;   // "do not advance yourself"
  }
SetNumElements(1); SetElement(composite, 0, false);
flags80 &= ~1;                            // doDistanceTest = false: never culled
matrix.Identity(); matrix.FillScale(2.0f);            // 0x660ce0, a uniform 2x scale
```

So the whole sky box is drawn at **twice** its modelled size, and the renderable is never
distance tested or frustum culled.

### `Display` `0x00477450`                                          **[V]**

```c
if (g_skyEnabled /* g[0x007c0c60], a .data bool that starts true */)
    Renderable::Display();
else
    Hide();
```

`Renderable::Display` (renderspine §2.2) has a `typeMask == 1` branch: with the distance
test off it still pushes `max(fade, fade2)` into `Drawable::SetFadeAmount`. That is the
cross-fade between the clear and the rainy sky box — the rainy one is loaded already
faded out (`SetToFadeOut(40)`), and the weather code fades it back in.

### `Update` `0x00477500`                                           **[V for the shape]**

A throttle around one job: push the time of day into the sky's frame controllers.

```c
if (!g_skyEnabled) return;
t = wallClock;                                 // g[0x80ac04]->+0x34, in ms
if (t - lastTime > 10) timer = 1000;           // restart the countdown after a stall
lastTime = t;
if (paused || cutScene)   { timer = 1000; tickCount = 0; }
else if (timer > dt)      { timer -= dt;  tickCount = 0; }   // dt = TimeInfo +0x04
else                      { tickCount++;  timer = 0;     }
if (tickCount % 15) return;                    // every frame for the first second,
                                               // then every 15th frame
float phase = (u32)g_timeOfDayMs * (1.0f/86400000.0f);  // g[0x8114ec], g[0x737b00]
for (fc : composite->+0x48) {                  // the composite's frame controllers
    fc->SetFrame((float)(int)fc->GetNumFrames() * phase);
    fc->[0x0d] = 1; fc->Update(0.0f, true); fc->[0x0d] = 0;
}
Hide();                                        // force the nodes to be re-submitted
```

`1.0f/86400000` is milliseconds per day, so `phase` is the time of day as 0..1 and every
frame controller of the sky is set to `numFrames * phase`.

### How the sky is hooked up

`GamePlayScene::AddRenderable` (`0x468cc0`):

```c
Scene::AddRenderable(r);
if (r->typeMask == 0x100 && r->[0x84]) 0x475760(r);             // building shadow
if (r->typeMask == 1) renderMgr->SetSky(r, r->[0x88]);          // isRainy -> secondary
```

`RenderManager::SetSky` (`0x467950`) is a plain ref-counted assignment to `+0x28` (the
primary sky) or `+0x2c` (the secondary, rainy one).

---

## 2. Drawing: display lists 46, 47 and 76

`SkyLoader` puts the sky box meshes on layer 38/39 and the billboard groups on layer 28,
which `Display_List::AddContainerElement` maps to lists 47, 46 and 76.

### `Display_List::RenderSky` `0x00459a00` — lists 46, 47          **[V]**

```c
if (lists[46].head == nil) return;
ctx->GetExtension(0x10b)->vslot2();            // the shader mode extension
ctx->EnableZBuffer(false);                     // ctx+0x110
ctx->SetZWrite(false);                         // ctx+0x118
Vector p = {0,0,0};
View_GetRenderingCamera()->GetPosition(&p);    // 0x461ac0, NOT the culling camera
p.y = 0.0f;
for (node : lists[46]) {                       // no per-node culling at all
    PushMatrix(0); Translate(p); [MultMatrix(pre->m);] MultMatrix(node->matrix);
    node->elem->Display(); PopMatrix(0);
}
for (node : lists[47]) { ... same, plus prim->SetFade(node->container->GetFadeAmount()); }
```

so the sky follows the camera in **x and z only**: it turns with the camera but does not
rise and fall with it. `Render()` draws it first of everything, with fog off and the
frame buffer's alpha channel write-protected.

### `Display_List::RenderCameraLocked76` `0x00459810` — list 76     **[V]**

The same walk, but translated to the camera's **full** position (y included), drawn near
the end of the frame with fog off, and with the container fade applied. That is where the
sun, the sun flares and the stars go: they sit at infinity.

---

## 3. `pure3d::BillboardQuadGroup` — chunk `0x00017006`

SHR's Pure3D has the same class with older chunk ids
(`p3d/billboardobject.cpp`, `constants/chunkids.hpp`: `QUAD 0x17001`,
`QUAD_GROUP 0x17002`); Scarface renumbered them and added the cut-off variants.

| chunk | what |
|---|---|
| `0x00017005` | `BillboardQuad` (`0x108` bytes as a `BillboardCutOffQuad`, else `0xc4`) |
| `0x00017006` | `BillboardQuadGroup` (`0xc8` / `0x94`), wrapped in a `BillboardObject` |
| `0x00017007` | a transform: quaternion + position, on the group and on every quad |
| `0x00017008` | uv animation frames (a texture atlas), quad only |
| `0x00017009` | the uv set, quad only |
| `0x0001700a` / `0x0001700b` | the source / edge cut-off cones, cut-off quads and groups |
| `0x0001700d` | a cut-off range pair |
| `0x00121204` / `0x00121201` | a `BillboardQuadGroupAnimationController` ("BQG"/"ANIM") |
| `0x00010003` / `0x00010004` | bounding box / sphere |
| `0x00122000` | a float, the container sort key |

### `0x00017006` — quad group                                       **[V]**

```
u32     version
pstring name                    e.g. "sunShape"
pstring shaderName              e.g. "Sun"
u32     cutOff                  != 0 -> BillboardCutOffQuadGroup
u32     zTest                   -> group +0x80
u32     zWrite                  -> group +0x81
u32     occlusion               -> group +0x82  (the PS2/Xbox occlusion query)
u32     numQuads                -> the quad array at group +0x84
```

`pure3d::BillboardObjectLoader::LoadObject` (`0x006993d0`) allocates a **`BillboardObject`**
— a `DrawableContainer` with one element, `0x5c` bytes, `+0x58 = 1.0f` (the intensity
bias) — calls `billboard_loader` (`0x00698ce0`) to fill it, and registers *that* in the
inventory under the group's name. The composite drawable looks it up with element type 8.

`BillboardObject::Display` (`0x00697660`) pushes the group's `0x17007` transform onto the
matrix stack, copies its intensity bias into the group, and calls the normal container
`Display` — so the transform is baked into the display list node's world matrix.

### `0x00017005` — quad                                             **[V]**

```
u32     version
pstring name
u32     cutOff                  != 0 -> BillboardCutOffQuad
u32     visible                 -> quad +0x88
u32     billboardMode (4cc)     -> quad +0x8c, mapped at 0x006977ff:
                                   "NOAX" 0, "XAX" 2, "YAX" 3, "LXAX" 4, "LYAX" 5,
                                   anything else (the files say "AAX") 1 = ALL_AXIS
u32     colour                  -> quad +0x38   (D3DCOLOR, bytes b g r a)
float   width                   -> quad +0x7c * 0.5      (g[0x7644ec] == 0.5)
float   height                  -> quad +0x80 * 0.5
float   distance                -> quad +0x84   extrusion towards the camera
```

### `0x00017007` — transform                                        **[V]**

```
u32 version; float quat[4] (x,y,z,w); float pos[3]
```

32 bytes. An identity transform is `(0,0,0,1)` + `(0,0,0)`, which is what every sky quad
*group* has; the individual quads carry the real positions (the sun sits about 800 m out).

### `0x00017009` — uv set                                           **[V]**

```
u32 version; u32 flipU; u32 flipV; float uv[4][2]; float uvOffset[2]
```

`flipU`/`flipV` pick a flip mode in `quad+0x90` (3 / 5 / 4 at `0x00697a84`). The uv order
is bottom left, bottom right, top right, top left.

### Drawing                                                         **[V for the shape]**

`BillboardQuadGroup::Display` (`0x00698940`) matches SHR's `tBillboardQuadGroup::Display`
almost line for line: take the world matrix off the matrix stack and the camera-to-world
and world-to-camera matrices off the view, drop every quad that is invisible or has a
zero colour, set the group's zTest/zWrite (saving the old values), `PushIdentity()`, and
build all the surviving quads into one `PDDI_PRIM_TRIANGLES` / `PDDI_V_CT` stream of
`numVisible*6` vertices. The per-quad construction is `tBillboardQuad::Display`:

* `NO_AXIS`: a flat quad `(±width, ±height, 0)` through `transform * world`, extruded
  along its own -z by `distance`.
* `ALL_AXIS`: the transform's position through `world`, extruded towards the camera by
  `distance`, then `±width` / `±height` along the *camera space* x and y.
* the four axis modes: a heading matrix built from the camera-to-quad direction.

Everything ends up in camera space and is drawn with an identity model-view matrix.

---

## 4. The sky's colours: the "VRTXANIM" vertex colour animation

The sky box meshes are ordinary `0x00010000` meshes, but five of the six have a vertex
animation hanging off them, and it is what makes the sky change with the time of day:

```
0x00010000 skyboxShape
  0x00010020 prim group    shader pure3dUntexturedShader1, TRISTRIP, 65 vertices,
                           NO index list  (0x00010005 positions, 0x00010008 colours)
  0x00121305 { u32 version; u32 numFrames; u32 keyFrame[numFrames]; u32 unk[numFrames]; }
    0x00121306 { u32 version; u32 frame; u32 unknown; }      x numFrames
      0x00010F02 { u32 version; u32 "CLR0"; u32 count;
                   { u32 vertex; u16 r, g, b; u16 zero; } [count] }
  0x00121201 frame controller "VRTX_skyboxShape" / "VRTXANIM"
```

The `0x00010F02` values are **offsets added to the mesh's own COLOURLIST**, not
replacements: the COLOURLIST is the night sky (`skyboxShape` is `0x0a0d14`, a dark blue)
and frame 0 is all zeros, so at midnight the mesh keeps its own colours and the daytime
frames add the blue back in. `skyboxShape` has six frames, `skybox17Shape` five,
`skybox18Shape` four, `skybox2Shape` and `skybox_horizonShape` two (both all zero — their
gradient texture does the work) and `skybox16Shape` none.

`skyboxShape`'s six offsets are `(0,0,0)`, `(104,201,209)`, `(132,149,204)`,
`(104,177,112)`, `(4,3,11)`, `(0,0,0)`; added to `(10,13,20)` the second one is the
familiar Miami sky blue. `SkyRenderable::Update` is what selects between them, through
the mesh's frame controller, from the time of day.

Two more things about these meshes that matter to a renderer:

* the prim groups are **non-indexed triangle strips** (`indexCount == 0`), so a pddi that
  only ever draws indexed primitives draws nothing at all;
* they are "streamed" prim groups (the `u3` field of the prim group header is 1 on the
  five animated ones), because retail keeps their vertices in system memory so the
  animation can rewrite them.

---

## 5. What the viewer implements

`billboard.cpp` / `billboard.h` (`pure3d::BillboardQuad`, `BillboardQuadGroup`,
`BillboardObject`, `BillboardObjectLoader`) and `renderer/sky.cpp` / `sky.h`
(`renderer::SkyRenderable`, `SkyLoader`, `g_skyEnabled`, `g_timeOfDay`), plus

* `Display_List::RenderSky` / `RenderCameraLocked76` with the camera translation and no
  culling, `pddiContext::SetZTest` / `GetZTest` / `GetZWrite`;
* `GamePlayScene::AddRenderable` puts the rainy sky in the secondary slot;
* `PrimGroup::SetFade` honours the "completely gone" end of the fade, which is what hides
  the rainy sky box;
* the vertex colour animation (`pure3d::VertexColourAnim`, `Geometry::SetColourAnimFrame`,
  `PrimGroup::SetVertexColourOffsets`), driven by `SkyRenderable::Update` from
  `renderer::g_timeOfDay` (0..1, `P3D_TIMEOFDAY`, default 0.25);
* `glPrimBuffer::Display` draws a prim group with no index list with `glDrawArrays`;
* the GL shader no longer modulates an **unlit** shader with the ambient light, which is
  what pddi does and what the sky box needs (its vertex colours *are* the sky).

Not implemented: the cut-off cones of `BillboardCutOffQuad` (`intensity` is always 1),
the uv animation of `0x00017008`, the `BillboardQuadGroupAnimationController`
(`0x00121204`), so the sun and stars keep their rest colour, a partial fade (only 0 and 1
are honoured), the occlusion query the sun flares use, and the vertex animation of
anything that is not a colour channel.
