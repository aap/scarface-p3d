# The sky (PC retail)

Everything the sky is made of lives in `assets/packages/z04/Common.p3d`:
two `renderer::SkyRenderable`s (chunk `0x08800002`), two `pure3d::CompositeDrawable`s
(`sky` and `rainy_skybox`), twelve sky box meshes and six `pure3d::BillboardQuadGroup`s
(chunk `0x00017006`) for the sun, its two flare stars, two lens flares and the moon.
(There are no stars: the only night object is the moon.)

```
0x08800002 sky            -> composite "sky"            6 meshes + 6 billboard groups
0x08800002 rainy_skybox   -> composite "rainy_skybox"   6 meshes
```

The clear sky's composite, its skeleton and its animations:

| element | pose joint | what | animation |
|---|---|---|---|
| `skyboxShape` | 1 `skybox` | the dome, one flat colour | `VRTX_skyboxShape`, 6 colour sets |
| `skybox16Shape` | 9 | untextured, no animation | — |
| `skybox17Shape` / `skybox18Shape` | 11 / 12 | two cloud layers (`Cloud_variant_001.tga`) | `VRTX_*`, 5 / 4 sets |
| `skybox2Shape` / `skybox_horizonShape` | 10 / 13 | the horizon gradient (`skyBoxGradHorizon_XB.tga`) | `VRTX_*`, 2 sets, both all zero |
| `sunShape` | 3 `sun` | the sun disc | `BQG_sunShape` |
| `fxSys_SunFlareShape` / `fxSys_SunFlare1Shape` | 4 / 7 | the two star bursts around the sun | `BQG_*` |
| `p3dBillboardQuadGroupShape1` / `2` | 5 / 6 | the lens ring and the lens octagon | `BQG_*` (Shape2 has none) |
| `p3dBillboardQuadGroupShape3` | 8 | the moon | `BQG_p3dBillboardQuadGroupShape3` |
| (the composite itself) | — | — | `PTRN_sky` |

Joints 3..8 all hang off joint 2, `sun_grp`, which is the one thing `PTRN_sky` turns:
**the whole sun rig is one rotating joint.**

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

### The cut-off cones — `0x0001700a` / `0x0001700b` / `0x0001700c` / `0x0001700d`  **[V]**

A `BillboardCutOffQuad` (the quad's `cutOff` word is set) can carry up to four more
chunks. The loader arm is `0x006979f9`..`0x00697e16` inside the quad loader `0x006976f0`;
every one of them is ignored when `cutOff` is 0 except `0x1700d`.

```
0x0001700a  { u32 version; u32 mode4cc; float angle[4]; }   the SOURCE cone
0x0001700b  { u32 version; u32 mode4cc; float angle[4]; }   the EDGE cone
0x0001700c  { u32 version; u32 "LINE"/other; float a, b; }  a falloff curve, unused in z04
0x0001700d  { u32 version; float scale[4]; }                two (hi, lo) size-scale pairs
```

* the mode four-CC is mapped at `0x00695a50` (and inline at `0x006990d6` for the group's
  own pair) to a **bit mask**: `"VERT"` 1, `"HRZT"` 2, `"BOTH"` 3, anything else 0.
* the loader stores the **cosine** of each of the four angles (`fcos` right after every
  `File::GetData`), as `(vertInner, vertOuter, horzInner, horzOuter)`, at quad `+0xd0`
  (source) and `+0xe0` (edge). The mode words go to `+0xc4` / `+0xc8`.
* `0x1700c` -> `+0xcc` (1 for `"LINE"`), `+0x100`, `+0x104`; `0x1700d` -> `+0xf0`..`+0xfc`.

`BillboardCutOffQuad::Calculate` (vslot 8, `0x00696f80`; the plain quad's slot is a
nullsub) takes the quad's object-to-world matrix **A** and the camera-to-world matrix
**B**, builds `dir = normalize(A.pos - B.pos)` and calls the cone evaluator `0x00695a90`
twice: once with the source mode/ranges and **A**, once with the edge mode/ranges and
**B**. The final intensity is the product of the (up to) four factors.

The cone evaluator, for each enabled bit, projects `dir` onto the matrix's z axis and onto
its y ("VERT") or x ("HRZT") axis, adds the two projections, normalises, and takes the dot
with `dir`:

```c
float Cone(float cos, const float range[2]) {
    if (cos >  range[0]) return 1;            // inside the inner angle
    if (cos <= range[1]) return 0;            // outside the outer angle
    return 1 - (cos - range[0])/(range[1] - range[0]);
}
```

The sum of the two projections *is* `dir` projected into the plane the two axes span, so
the cosine is the cosine of the angle between `dir` and that plane, and the whole thing is
independent of the **sign** of the axes. That is worth knowing for a port: the viewer's
camera-to-world z points backwards where D3D's points forwards, and it does not matter.

In practice: the **edge** cone with the camera matrix is "how far is this quad from the
middle of the screen" (the two lens flares, inner 10°, outer 90°), and the **source** cone
with the quad's own matrix is SHR's "is the quad turned towards the camera" (the two sun
flare stars, inner 80°, outer 90°).

`0x1700d` is **not** an intensity: `fxSys_SunFlareShape`'s quad has `1, 1, 1.3, 1.3` and
`fxSys_SunFlare1Shape`'s `1.6, 1.6, 1.8, 1.8`, and `0x00696f80` lerps the two cone
products into those ranges and writes the result to its *other* output — a size scale.
Every sky quad has each pair's two ends equal, so it is a constant.

---

## 4. The animations and their frame controllers

Everything that moves in the sky is a `pure3d::Animation` (chunk `0x00121000`) played by a
frame controller (chunk `0x00121201`) that `SkyRenderable::Update` drives from the time of
day. Ten of the eleven animations in `Common.p3d`'s sky block are the sky's.

### `0x00121000` — animation                                        **[V]**

```
u32     version
pstring name
u32     type4cc          'LITE' 'BQG\0' 'PTRN' 'PVIS' 'VRTX' 'PSYS' 'TEX\0' 'SHAD' ...
float   numFrames        241 for everything time-of-day: 241 frames at 30 fps = 24 h
float   speed            frames per second
u32     cyclic
  0x00121006 { u32 version; u32 numEntries; }        a preallocation hint, Scarface's
    0x00121007 { u32 version; u32 channelChunkId;    replacement for SHR's 0x00121004
                 u32 numChannels; u16 numKeys[n]; }  SIZE chunk
  0x00121002 { u32 version; u32 numGroups; }
    0x00121001 { u32 version; pstring name; u32 groupId; u32 numChannels; } x numGroups
      <channel> x numChannels
```

The group **name** is what binds it to its target (SHR: `GetGroupByUID(entity->GetUID())`),
not `groupId`: a 'PTRN' group is named after a skeleton joint, a 'BQG' group after a
billboard quad. `groupId` happens to be the joint index for 'PTRN' and the prim group
index for 'VRTX', and is 0 for 'BQG'.

### The channels                                                    **[V]**

All of them start `u32 version; u32 param4cc;` and end with `u32 nKeys; u16 frames[nKeys];`
and the values; the two vector forms squeeze extra fields in between. Scarface pads the
three-letter four-CCs with a **NUL** where SHR uses a space.

| chunk | name | body after the param | per key |
|---|---|---|---|
| `0x00121100` | FLOAT1 | — | 1 float |
| `0x00121101` | FLOAT2 | — | 2 floats |
| `0x00121102` | VECTOR_1DOF | `u16 movingIndex; float constants[3];` | 1 float |
| `0x00121103` | VECTOR_2DOF | `u16 frozenIndex; float constants[3];` | 2 floats |
| `0x00121104` | VECTOR_3DOF | — | 3 floats |
| `0x00121105` | QUATERNION | — (version **1**, no `0x12110f` format chunk) | 4 floats x,y,z,w |
| `0x00121108` | BOOL | `u16 startState;` | — (the frames are TOGGLES) |
| `0x00121109` | COLOUR | — | 1 u32, `0xAARRGGBB` |
| `0x0012110e` | INT | — | 1 i32 |

Any channel may be followed by `0x00121110 { u32 version; u32 interpolate; }`.
The 1DOF/2DOF forms keep the components that never move in `constants` and only key the
rest — `mapping` is the *moving* index for 1DOF and the *frozen* one for 2DOF, and the two
moving indices are `{1,2} / {0,2} / {0,1}`. `tBoolChannel::GetValue` starts at
`startState` and flips it at every key frame at or before the sampled frame.

### `0x00121201` — frame controller                                 **[V]**

```
u32     version = 1
pstring name             e.g. "BQG_sunShape"
u32     type4cc          the animation type it plays
u32     'ANIM'
float   frameOffset
u32     1
pstring target           the object it drives
pstring animation        the 0x00121000 to play
```

SHR's `0x00121200` is the same without the `'ANIM'` word and the trailing `u32`. Where the
controller sits says what `target` means:

| parent chunk | type | target |
|---|---|---|
| top level (`miami_lod.p3d`, `islands_LOD.p3d`) | `LITE` | a `pure3d::Light` |
| `0x00123000` composite drawable | `PTRN` | the composite (its `Pose`) |
| `0x00010000` mesh | `VRTX` | the mesh |
| `0x00017006` -> `0x00121204` | `BQG\0` | the billboard quad group |
| `0x0001580c` particle system | `PSYS` | the emitter |

`0x00121204` is a plain wrapper: `{ u32 version; u32 count; }` and then `count`
`0x00121201` chunks.

### `PTRN_sky` — what moves the sun                                  **[V]**

A `tPoseAnimationController`: for every joint of the composite's skeleton it looks up the
animation group of the same name and writes the `TRAN` (translation) and `ROT` (rotation)
channels into the joint's local matrix, leaving the skeleton's rest pose where there is no
channel. `PTRN_sky` has exactly two groups:

* `sun_grp` — a 21-key **quaternion** `ROT` channel, and
* `sun` — a 2-key `TRAN` channel that is a constant `(832.6, -0.755, -1.53)`.

So the sun sits 832 m out along the `sun_grp` joint's x axis and the whole day is that one
joint's rotation. The rest pose puts it at `(8, -755, 350)` — **755 m underground**, which
is exactly where a viewer that never plays the animation draws it. The 21 keys walk the
sun from -65° at midnight up to +72° at 14:00 and back down; sunrise is about 7.7 h and
sunset about 18.6 h. The keys **flip sign** between frames 153 and 154 (the same rotation,
the opposite quaternion), so the slerp has to do the `dot < 0` shortest-arc fix or the sun
jumps across the sky at 15:20.

### `BQG_*` — the quads                                             **[V]**

`tBillboardQuadGroupAnimationController` (SHR `p3d/anim/billboardobjectanimation.cpp`):
one group per quad, matched by name, with these channels
(`Pure3DAnimationChannels::BillboardObjects`):

| param | channel | what |
|---|---|---|
| `VIS\0` | BOOL | `quad->visible` |
| `TRAN` | VECTOR_*DOF | `quad->transform` position |
| `ROT\0` | QUATERNION | `quad->transform` rotation |
| `WDT\0` / `HGT\0` | FLOAT1 | width / height (the file value, so **halved** like the loader does) |
| `DIST` | FLOAT1 | the extrusion towards the camera |
| `CLR\0` | COLOUR | `quad->colour` |
| `OFF\0` | FLOAT2 | `quad->uvOffset` |
| `ORNG` | FLOAT2 | the uv offset range (SHR `SetUVOffsetRange`) |
| `SRNG` / `ERNG` | FLOAT1 | the source / edge cut-off angle |
| `FSF\0` | INT | Scarface only: the frame of the `0x00017008` uv atlas |

The sky's are almost all constants — one key at frame 0 — with two exceptions that matter:

* `BQG_sunShape` has a **6-key `CLR` channel**: grey `969696` at midnight, `673222` at
  05:17, `68301c`, `763b16`, the familiar `dcac31` sun yellow at 11:39 and `69301a` at
  17:55. That is the only thing that colours the sun over the day.
* `BQG_p3dBillboardQuadGroupShape3` (the moon) has a **`VIS` channel** with
  `startState = true` and toggles at frames 70 and 200, i.e. the moon is drawn from
  midnight to 06:58 and from 19:55 to midnight.

The flare quads' `TRAN` is a VECTOR_2DOF with the frozen component in `constants`; note
that their positions are in their own joint's frame, which the skeleton has built as the
*inverse* of `sun_grp`'s rest rotation — so at the rest pose those joints are the identity
and the numbers in the file are plain world positions.

---

## 5. The sky's colours: the "VRTXANIM" vertex colour animation

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
familiar Miami sky blue. Every offset set is **one colour for every vertex of the mesh**
(`count` is the whole vertex list and all its entries are equal), so the dome really is a
flat colour, always and everywhere: the structure in the sky comes from the other five
meshes, not from this.

`SkyRenderable::Update` selects between the sets through the mesh's frame controller — and
the mapping is **not** linear in the time of day. The `VRTX_*` animation has a single INT
channel, also tagged `'VRTX'`, whose value is the **index of the morph key frame**:

```
VRTX_skyboxShape           frames  40  90 120 180 210 240  ->  0 1 2 3 4 5
VRTX_skybox17Shape         frames  40  41  90 210 240      ->  0 1 2 3 4
VRTX_skybox18Shape         frames  40  90 210 240          ->  0 1 2 3
VRTX_skybox2Shape          frames   0 240                  ->  0 1
VRTX_skybox_horizonShape   frames   0 240                  ->  0 1
```

so at noon (animation frame 120.5) the dome is on colour set **2**, not `6*0.5 = 3`.
SHR's `tVertexAnimController` has an *entity* channel of morph keys there and blends
between the two bracketing ones; Scarface's int channel is that index list, and keeping
the fraction of the sampled int is what makes the sky cross-fade instead of pop.

Two more things about these meshes that matter to a renderer:

* the prim groups are **non-indexed triangle strips** (`indexCount == 0`), so a pddi that
  only ever draws indexed primitives draws nothing at all;
* they are "streamed" prim groups (the `u3` field of the prim group header is 1 on the
  five animated ones), because retail keeps their vertices in system memory so the
  animation can rewrite them.

### The order the six are drawn in — `0x00122000`                   **[V]**

`{ u32 version; float key; }`, clamped to `[0,1]` and stored in the drawable container's
sort key (`GeometryLoader::LoadObject` `0x0069ca7a` -> container `+0x3c`), which
`Display_List::AddContainerElement` copies into the node. List 46 is sorted with `CmpKey`,
**descending**, and drawn with z-test and z-write off — so the largest key is painted
first and the smallest last:

```
skyboxShape 1.0   skybox16Shape 0.9   skybox17Shape 0.3   skybox18Shape 0.2
skybox2Shape 0.0  skybox_horizonShape 0.0
```

i.e. the dome, then the two cloud layers, then the horizon gradient on top. Skip this
chunk and every container sits at the constructor's 0.5, the order is whatever the qsort
happens to produce, and the dome can paint over the clouds and the gradient — which is
exactly the "always and everywhere the same flat colour" the viewer had.

The billboard groups carry the same chunk (sun 0.5, the two flare stars 0.3 and 0.7, the
lens quads 0.1 / 0.4 / 0.2), though list 76 is not sorted at all.

---

## 6. What the viewer implements

`billboard.cpp` / `billboard.h` (`pure3d::BillboardQuad`, `BillboardQuadGroup`,
`BillboardObject`, `BillboardObjectLoader`, `BillboardQuadGroupAnimationController`),
`anim.cpp` / `anim.h` (`pure3d::Animation` and every channel type above,
`AnimationLoader`, `FrameController`, `PoseAnimationController`) and
`renderer/sky.cpp` / `sky.h` (`renderer::SkyRenderable`, `SkyLoader`, `g_skyEnabled`,
`GetTimeOfDay`), plus

* `Display_List::RenderSky` / `RenderCameraLocked76` with the camera translation and no
  culling, `pddiContext::SetZTest` / `GetZTest` / `GetZWrite`;
* `GamePlayScene::AddRenderable` puts the rainy sky in the secondary slot;
* `PrimGroup::SetFade` honours the "completely gone" end of the fade, which is what hides
  the rainy sky box;
* the vertex colour animation (`pure3d::VertexColourAnim`, `Geometry::SetColourAnimFrame`,
  `PrimGroup::SetVertexColourOffsets`), driven through `pure3d::VertexAnimationController`
  and its INT channel, not linearly in the phase;
* the `0x00122000` container sort key on meshes and billboard objects, which is what puts
  the horizon gradient and the cloud layers on top of the dome;
* the cut-off cones, `BillboardQuad::Calculate`, so a lens flare fades as it leaves the
  middle of the screen;
* `glPrimBuffer::Display` draws a prim group with no index list with `glDrawArrays`;
* the GL shader no longer modulates an **unlit** shader with the ambient light, which is
  what pddi does and what the sky box needs (its vertex colours *are* the sky).

`SkyRenderable::Update` keeps retail's throttle (every frame for the first second, then
every 15th), sets **every** frame controller of the composite to `numFrames * phase` and
ends with `Hide()`, because the pose animation moves the billboard groups and a display
list node caches the world matrix it was submitted with. The composite collects the frame
controllers of its elements into its own list at load time, which is what retail's `+0x48`
list holds.

There is one clock: `renderer::LightManager::timeOfDay` in hours (`P3D_TIME`;
`P3D_TIMEOFDAY` is the same clock as a 0..1 fraction of a day), so the lights, the sky
colour and the sun all move together. `renderer::GetTimeOfDay()` is the 0..1 phase.

Deviations, all deliberate:

* the `0x1700d` size scale of a cut-off quad is parsed but not applied (it would make the
  two sun flare stars 1.3x and 2.9x bigger);
* the `0x1700c` falloff curve is parsed and ignored; nothing in z04 has one;
* the INT channel keeps its fractional part where retail's `tIntChannel` truncates, so the
  sky cross-fades between its colour sets instead of popping;
* the 'PTRN' controller binds its groups to joints once at load time instead of looking
  them up by UID on every frame, and it re-evaluates the pose itself;
* retail plays the animations through blendable frame controllers with min/max frames and
  a cycle mode; ours is one animation, `MakeValidFrame` over the whole range.

Still not implemented: the uv atlas animation of `0x00017008` (and its `'FSF\0'` channel),
the `'ORNG'` / `'SRNG'` / `'ERNG'` channels (nothing in z04 has them), the occlusion query
the sun flares use, a partial fade (only 0 and 1 are honoured), and the vertex animation of
anything that is not a colour channel.

## 7. The horizon gradient is a palette, not a gradient (2026-09-16, [V])

`skybox2Shape` / `skybox_horizonShape` sample `skyBoxGradHorizon_XB.tga` with **u = 0 on
every vertex** and v running 0..1. The texture is a 128x128 palette: every column is one
horizon colour by hour (x=0 dark blue, 32 cream, 48/80 yellow-cream, 96 orange, 112
red-brown, 127 dark blue again) and the alpha ramps vertically (0 at the top, 178 at the
bottom). Their `VRTX_*` vertex animation carries, next to `CLR0`, a **`0x00010F01 'UV0\0'`**
set per key frame: `{ u32 version; u32 'UV0\0'; u32 count; { u32 vertex; float u, v; }[count] }`,
frame 0 = (0, 0), frame 1 = (1, 0), so the u offset slides across the palette over the day
and the band under the sky takes the hour's horizon colour, which the artists keyed to the
fog colours of the environment objects (notes/fog.md). Without it the meshes sit on the
night column and the band is dark blue all day. `pure3d::VertexUVAnim`,
`PrimGroup::SetVertexUVOffsets` (geometry.cpp, primgroup.cpp) implement it; the uv frame is
clamped, not wrapped, so 23:59 does not slide back to the night column.

Debug: `P3D_SKYHIDE=a,b,c` hides sky composite elements by name substring.
