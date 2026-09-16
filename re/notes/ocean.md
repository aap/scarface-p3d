# The ocean

PC retail, addresses are unpacked-image VAs (`re/dis.sh`). Companion to
`notes/renderable_classes.md` §5.14 (the three `renderer::` classes), `notes/displaylist.md`
(where layer 27 lands) and `notes/fog.md` (the fog the water takes).

**[V]** = read out of the disassembly or out of shipped data. **[?]** = inferred.

There is **no SHR ancestor**: `grep -ril ocean` over `/u/aap/fun/ps2engines/extracted/{shr,pure3d}`
finds nothing but a Maya header. The ocean is Scarface-only
(`pure3d/LegionExtensions/ocean/` in the leak's directory layout), and the leak only has the
*game* half of it (`ocean/oceanobject.cpp`, `ocean/si_oceanobject.cpp`), not the engine half.

---

## 1. The object graph  [V]

```
OceanObject                       game side, in the leak; vtable 0x75750c, ApplyChanges 0x5e0360
  +0xa8 mRenderable  +0xb0 mpOcean  +0xb4/+0xb8 template names
  +0xbc..0xc8 mReflection{R,G,B,A}Scale   +0xcc/0xd0/0xd4 mWaterColour{R,G,B}
  +0xd8 mDetailOpacity  +0xdc mDetailTextureScale
  +0xe0/0xe4/0xe8 mFoam{Min,Max}Height / MaxOpacity
  +0xec/0xf0 mSpecular{Highlight,Brightness}   +0xf4 mRoomIndex
       |
renderer::OceanRenderable     ctor 0x470ad0, vtable 0x7380c4, 0x88 bytes
       |                      typeMask 0x40, doDistanceTest AND doFade cleared
renderer::OceanContainer      ctor 0x470a40, vtable 0x737ffc, 0x50 bytes, sortKey 0.5
       |
renderer::OceanPrimitive      ctor 0x470820, vtable 0x73807c, 0x48 bytes
       |                      layer 27; +0x38 the pure3d Ocean, +0x3c/+0x40/+0x44 the textures
pure3d::Ocean                 ctor 0x689af0, vtable 0x76a2dc, 0x44 bytes, singleton g[0x859ba8]
  +0x08 WaveModel   (0x128 bytes, ctor 0x6a8120)
  +0x0c OceanParams (0x1dc bytes, ctor 0x6a8040)   <- sea level, the 16 wave trains, the shading
  +0x10 win32OceanRenderer (0x484 bytes, ctor 0x6a0fb0, vtable 0x76b77c)
  +0x14..0x38 the pending ShadingParameters of a transition, +0x3c/+0x40 its clock
```

`g[0x8111e0]` is the one `OceanRenderable`, `g[0x859c70..0x859c78]` the three LOD grid meshes,
`g[0x859bf8]..` the 30 procedural bump textures.

The `pure3d::Ocean` vtable has **four slots only** (AddRef / Release / GetRef / dtor); every
"setter" below is a direct call.

### `renderer::OceanRenderable_CreateInstance` 0x466940  [V]

```c
RenderableHandle OceanRenderable_CreateInstance(
        const char *reflectionTextureName,   // OceanTemplate::ReflectionTextureName
        const char *detailTextureName,       // OceanTemplate::DetailTextureName
        const char *foamTextureName,         // OceanTemplate::FoamTextureName
        const char *inventoryName,           // OceanTemplate::ModelInventory ("Common")
        int sceneId);                        // 0 = GAMEPLAY_SCENE
```
Range-checks `sceneId`, finds the inventory by name, resolves the three textures through
`0x4654a0(inventory, name)`, `new OceanRenderable(refl, detail, foam)`, stores it in
`g[0x8111e0]`, `SetName("OceanRenderable")`, adds it to the scene and returns a handle.
The only caller is `OceanObject::ApplyChanges` at 0x5e0408.

### The per-class slots  [V]

| slot | addr | what |
|---|---|---|
| prim 8 | 0x4707a0 | `Display()` → `if(+0x38) jmp 0x689aa0` |
| prim 13 | 0x4707c0 | `CalcBounds()`: `View_GetCamera()->GetPosition(&sphere.centre); sphere.radius = 100000` |
| prim ? | 0x4707b0 | `GetTypeMask()` → 0x20000 |
| rend 9 | 0x470be0 | `Update(TimeInfo*)` → 0x470c20 → `Ocean::Update` |
| rend 10 | 0x470810 | thunk to `Renderable::Display` |
| rend 12 | 0x438400 | `SetMatrix`, a stub |

Container slot 10 is `j_DrawableContainer::Display` (0x4707f0 → 0x6834c0), the normal
"push the element into the Display_List" path.

---

## 2. Where it is drawn  [V]

`OceanPrimitive`'s layer is **27**, and `Display_List::AddContainerElement`'s layer table maps
case 27 to **list 60** unconditionally. List 60 is walked in group 12 of `Display_List::Render`:

```
12.  if (GetOceanLevelOfDetail() >= 0) {
         SetColourWrite(0,0,0,0); lists[58];  SetColourWrite(1,1,1,1);   // the player shadow z-fill
         lists[60].head;                                                 // the ocean
     }
```

so: **z-write on, colour write on, fog ON**, after the lit world and the light sets, before the
decals and the second specular pass. Retail forces fog *off* only around the sky (46/47), the
env/reflection pass (9/10), the night lights (11) and the camera-locked list (76) — and the
ocean's own `BindPass` writes `D3DRS_FOGENABLE = 1` explicitly at 0x69feac, so the water is
fogged like the world and the horizon is the fog colour.  **[V]**

`0x4636f0`, which `notes/displaylist.md` had as "gates lists 58/60", is
`renderer::GetOceanLevelOfDetail()`: `g[0x8111e0] && (flags & 0x10) ? Ocean::GetLevelOfDetail()
: -1`. It is the **ocean gate**, not a shadow gate.  **[V]**

List 60 is never sorted and only `lists[60].head` is drawn — the ocean is a singleton.
`CalcBounds` keeps the primitive's sphere on the camera with radius 100000, so
`Display_List::IsNodeVisible` can never cull it, and the renderable has both `doDistanceTest`
and `doFade` cleared.

`d3dState::SetAlphaBlend(1, 1, 6, 7)` in `BindPass`: the water is **alpha blended**, with the
tuning template's `ReflectionAlphaScale` (0.98) as the alpha — i.e. 2% of the sea bed shows
through. **[V]**

---

## 3. Where the numbers come from  [V]

All of it is compiled Torque script, not p3d data: **no p3d in the game contains an
`OceanTemplate` / `OceanTuningTemplate` / `OceanObject` `0x09900190` block.**
`scriptc/templates/ocean.cso` holds the templates (it is also embedded verbatim in
`assets/packages/InGameLoadTemplates.p3d` at file offset 0x10ac7, chunk 0x08800104), and
`scriptc/missions/z04/objects_static.dso` holds the one and only `OceanObject` instance.
`notes/fog.md` §1 documents the .cso format; two corrections to it fall out of this work:

* the float tables are **f32, not f64**, and the real section order is
  `version | gstSize+gst | globalFloatCount + f32[] | fstSize+fst | funcFloatCount + f32[] |
  codeSize | code | identTable`;
* the `stx` name hash spells the **low nibble first**
  (`OceanTemplate` → `stxejfaaopo`, `OceanTuningTemplate` → `stxhdmngbil`,
  `OceanObject` → `stxhhaiejol`).

### Field names, out of `RegisterMembers` in the exe  [V]

`OceanTemplate::RegisterMembers` 0x61b190: `ModelInventory` +0x24, `OceanModelName` +0x28,
`ReflectionTextureName` +0x2c, `DetailTextureName` +0x30, `FoamTextureName` +0x34.

`OceanTuningTemplate::RegisterMembers` 0x61b300 (sizeof 0x60): `MinWaveLength` +0x24,
`MaxWaveLength` +0x28, `AmplitudeRatio` +0x2c, `WindDirectionMean` +0x30,
`WindDirectionVariance` +0x34, `SpeedScaleFactor` +0x38, `ReflectionRed/Green/Blue/AlphaScale`
+0x3c..+0x48, three **unregistered** water-colour floats +0x4c..+0x54, `DetailOpacity` +0x58,
`DetailTextureScale` +0x5c.

There is **no `SeaLevel` and no grid-size script property anywhere.**

### The templates  [V]

```
OceanTemplateDefault   ModelInventory "Common"   OceanModelName "OceanModel"   (dead: no such model)
                       ReflectionTextureName "skyTexture"       <- the runtime reflection target
                       DetailTextureName     "water_01.BMP"
                       FoamTextureName       "Water_Ocean_Foam.tga"
OceanTemplateNightDefault  ... DetailTextureName "water_night_01.BMP"
OceanTemplateRiver         ... DetailTextureName "ter_rock_001.BMP"
```
Only `OceanTemplateDefault` is ever referenced.

`OceanTuningTemplateDefault` — the one z04 starts with:

| field | value |
|---|---|
| MinWaveLength | 0.1 |
| MaxWaveLength | 12.0 |
| AmplitudeRatio | 0.013 |
| WindDirectionMean | 90.0 (degrees) |
| WindDirectionVariance | 30.0 |
| SpeedScaleFactor | 1.2 |
| ReflectionRed/Green/Blue/AlphaScale | 0.25 / 0.23 / 0.25 / 0.98 |
| DetailOpacity | 0.225 |
| DetailTextureScale | 0.2 |

Fourteen more exist (`Light` 1.1/14/0.0045, `Calm` 0.5/8/0.01, `Churn`, `Boundary`, `Ripples`,
five `River*` with tint 0.22/0.20/0.16/0.80 and opacity 0.3, five `Wave*`); only **Default,
Light and Calm** are referenced by shipped data. z04 swaps them from trigger volumes:
`roomTrigger*` → `Light` over 4 s and back, one volume → `Calm` over 6 s, and six
`waveTrigger*` volumes call a script global that rewrites `WindDirectionMean` on
`OceanTuningTemplateDefault` **in place** (45 or 145).

### The one OceanObject  [V]

```
OceanObject "Ocean" { Subzone z04s00; Position 0 0 0; Direction 1 0 0; isVisible true;
    oceanTemplate OceanTemplateDefault;  tuningTemplate OceanTuningTemplateDefault;
    DetailTextureScale 0.075;  DetailOpacity 0.225;
    ReflectionRed/Green/BlueScale 0.5;  ReflectionAlphaScale 1.0;
    FoamMinHeight 0.15;  FoamMaxHeight 1.0;  FoamMaxOpacity 1.0;  RoomIndex 1; }
```
`SetPosition` / `SetDirection` are no-ops in the leak.

**What actually survives startup** matters: `ApplyChanges` (0x5e0360) writes the OceanObject's
own fields first and then calls `SetTuningTemplate(t, 0.0f)` → `Ocean::SetShadingParameters`
(0x6899b0) with transition time 0, which takes the snap branch and **overwrites the shading
half**. So the effective z04 state is the tuning template's 0.25/0.23/0.25/0.98 tint, opacity
0.225 and detail scale **0.2** (not the object's 0.5/0.5/0.5/1.0 and 0.075); only the foam and
specular values are the object's. **[V]**

The three water-colour floats of `OceanTuningTemplate` are a genuine engine bug: the ctor
(0x61b110) initialises nothing and no script can reach them, yet `SetTuningTemplate` copies
them into `ShadingParameters.mWaterColour` and `SetShadingParameters` blits them over the
255/255/255/255 `ApplyChanges` had just written. Nothing in the win32 renderer reads
`params+0x1cc..0x1d8`, so it is probably dead on PC. Water colour is **255, 255, 255, 255**
(the params ctor, and `SetWaterColour` 0x6898c0 forces alpha 255). [V] for the code path,
[?] for the runtime value.

---

## 4. Sea level, the height field and the wave model  [V]

### Sea level = **0.0f, hard-coded**

`OceanParams+0x184` is written in exactly one place in the image — the params ctor at 0x6a8072,
`mov dword [eax+0x184], 0`. The only two other references are the reads in
`Ocean::GetHeight` (0x689739) and `Ocean::GetSeaLevel` (0x689777). There is no `SeaLevel`
script property and z04's `Position` is `0 0 0`. **The water plane is world y = 0.**
It reaches the vertex shader as `c61.w`.

### `GetHeight(x, z, nWaveTrains)` 0x689710 → 0x6a0bc0

```c
float Ocean::GetHeight(float x, float z, int n) {          // n from "NumOceanWaveTrainsForBuoyancy"
    if (n > 4) n = 4;                                      // hard clamp
    float h = 0;
    for (int i = 0; i < n; i++)
        h += amp[i] * cosf(phase[i] - (x*kx[i] + z*kz[i]));
    return h + params->seaLevel;                           // seaLevel = 0
}
```
`GetMaxHeight` (0x689750 → 0x6a8000) is `seaLevel + sum of all sixteen amplitudes`.

### The 16 wave trains — `WaveModel` (ocean+0x08)

Layout: the six template floats at +0x00, train lifetime **15.0 s** at +0x18, fade in/out
**2.0 s** at +0x1c, then `amp[16] @+0x20`, `k[16] @+0x60`, `omega[16] @+0xa0`,
`dir[16] @+0xe0`, and a two-stream 16-bit multiply-with-carry RNG at +0x120/+0x124.

`SetWaveParameters` 0x6a8370 → the generator 0x6a8180:

```c
for (int i = 15; i >= 0; i--) {
    float u      = i*(1.0f/15.0f) + frand()*(1.0f/64.0f);       // frand() in (-1, 1)
    float lambda = minWaveLength + u*u*u*(maxWaveLength - minWaveLength);   // CUBED
    k    [i] = 2*PI / lambda;
    amp  [i] = lambda * amplitudeRatio;
    omega[i] = speedScaleFactor * sqrtf(9.8f * k[i]);           // deep water, g = 9.8
    dir  [i] = windDirectionMean + frand()*windDirectionVariance;   // DEGREES
}
```

`Initialize()` 0x689ad0 → 0x6a83b0 seeds the sixteen 0x18-byte records in `OceanParams`
(`faded, amplitude, direction, omega, k, age`), staggering `age = i*15/16` seconds.
`WaveModel::Update` 0x6a8420 ages them by dt, cross-fades the amplitude with a **smoothstep**
over the first and last 2 s, and respawns a train with the *same* parameters when it passes
15 s. `Ocean::Update` calls `WaveModel::Update` **twice in a row with the same dt**
(0x689de1 and 0x689df2), so the lifetime effectively runs at 2x; the travelling phase comes
from `params+0x180` and is only advanced once. **[V]**

Only the **first four** trains reach the GPU (`0x6a0a70`), as
`amp, cos(dir), sin(dir), k*cos, k*sin, k*cos², k*cos*sin, k*sin*cos, k*sin², omega*t, k, lambda`
in VS constants c10..c21 — the same formula `GetHeight` evaluates on the CPU, so buoyancy and
rendering agree, plus the `k·cos²` products the shader needs for the analytic normal.

With `OceanTuningTemplateDefault`: lambda in [0.1, 12] m, amplitudes 0.0013..0.156 m, periods
0.25 s to 2.3 s, all heading 90° ± 30°. **The tallest wave in the game is 16 cm.** The ocean is
essentially a flat plane with ripples; what you see is the shading of the normals, not the
displacement.

---

## 5. The renderer: a projected grid and one draw call  [V]

`win32OceanRenderer::win32OceanRenderer` 0x6a0fb0 builds **three static grid meshes**
(`0x6adde0(n, n)` for n = 50, 110, 170; LOD index defaults to 2, the 170 one) and loads three
d3d effects by name: `+0x238 = "ocean_pass1"`, `+0x228 = "ocean_pass1_spheremap"`,
`+0x224 = "ocean_pass2"`. **`ocean_pass2` is never read anywhere in the image — the PC ocean is
single pass.** `+0x480` picks pass1 (the real-time reflection render target) or
pass1_spheremap (the static `skyTexture`). It also creates **30 procedural 64x64 bump textures
with 6 mip levels**; `Update` advances the index `(n+1) % 30` per frame, and that is the
animated ripple / EMBM map on stage 0.

The grid is a **projected grid in camera space**, not a world grid:

```c
float S = (fov*1.4f)/nRing;                  // angular step
float T = tanf(fov*1.4f*0.5f)*aspect;        // half horizontal extent per unit depth
int segs = nSeg - 2, half = segs/2;
for (float A = -3*S; A < PI/2 - S/3; A += S) {
    float t0 = tanf(A), t1 = tanf(A+S);
    float w0 = T/cosf(A), w1 = T/cosf(A+S);
    for (int j = 0; j < segs; j++) {
        v->x = w0*((float)j/half) - w0;      // spans [-w0, +w0)
        v->z = t0;                           // tan(angle from the nadir)
        v->y = 1/length((w1*(j/half) - w1) - v->x,  t1 - t0);   // a 1/quad-size weight
        v++;
    }
}
// one extra clamped ring at z = maxTan if the last ring did not reach it
```
The vertex is **12 bytes, three floats, no normal, no uv, no colour** (fvf = 0, the declaration
comes from the effect). Indices are one triangle strip with degenerates at the end of every row,
drawn with a single `DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, ...)`. The buffers are rebuilt
only when the viewport changes.

Per frame (`0x6a0940`) the renderer computes

```
world = Scale(cameraHeight) * RotateY(-atan2(forward.x, forward.z)) * Translate(camX, 0, camZ)
c60 = { 1/cameraHeight, 0.40, 0.75 }      c61 = { camX, camY, camZ, seaLevel }
```

so a row at angle `A` lands at ground distance `cameraHeight * tan(A)`: the tessellation is
uniform *on screen*, densest right in front of the camera and stretching to the far plane, and
nothing has to be re-centred CPU-side. **There is no per-frame vertex write at all.** Other
constants: `c0 = {?, 1/sum(amplitudes), 255, 0.5}`, c22 `{0, 0.05, 0.3, 1}`, c37..c39 viewport,
c49 `{near, far, 1.2/(near+far)}`, c62..c94 a 33-entry sin/cos lookup table (this is vs_1_1).

`Display` 0x689aa0 → `win32OceanRenderer::Display` 0x6a08a0 is
`SaveRenderState; SetupPass; BindPass(0); grid[lod]->Draw(); RestoreRenderState`.
`Canvas::Render` (0x4689a0) runs `Ocean::RenderReflection(0x4679b0)` just before
`View::BeginRender` when `GetOceanLevelOfDetail() >= 0` — that is the reflection render target
being filled with a mirrored scene.

---

## 6. What the viewer does  ---  deviations

`ocean.h`/`ocean.cpp` (pure3d) and `renderer/ocean.h`/`.cpp`. The class names, the layer, the
sort key, the bounds and the parameter set are retail's; the drawing is not.

| retail | viewer |
|---|---|
| a projected grid in camera space, three static LOD meshes, one draw call, no CPU vertex work | a camera-centred **world** grid (`gridCells` x `cellSize`, default 128 x 2 m) plus four flat trapezoids out to `farExtent` (20 km), rebuilt on the CPU every frame through a pddi prim buffer |
| vertex = 3 floats, the height field and the normal come out of the vertex shader | vertex = position + normal + colour + uv, all evaluated on the CPU |
| single pass, a d3d effect with 4 texture stages (animated EMBM bump, reflection, detail, foam) | two passes over the same buffer: an untextured "reflection" pass with z-write **off**, then `ocean_text` (the detail texture) alpha blended over it at `DetailOpacity` with z-write on. Pass 1 leaves the depth alone so that pass 2, at the same depth, still passes GL's default `GL_LESS`; pass 2 lays the water's depth down. |
| the reflection is the sky rendered into `skyTexture` every frame, scaled by `ReflectionColourScale` | a constant `reflectionColour` stands in for that render target. It is pre-divided by the template's 0.25 so `ReflectionColourScale` still reads through as the game wrote it. |
| the wave trains live 15 s, fade in and out and respawn; 16 of them, 4 reach the surface | the same six template parameters, but a fixed set of waves with a deterministic direction spread, no lifetime and no respawn. The amplitude is tapered to 0 over the outer fifth of the grid so the wavy part meets the flat skirt exactly. |
| foam (`Water_Ocean_Foam.tga`, min/max height 0.15/1.0), specular highlights | neither; the textures are resolved and held, as retail does, and nothing reads them. The FOAM display lists (0 and 1) are dead on PC anyway. |
| alpha blended at `ReflectionAlphaScale` = 0.98 | opaque |
| the detail texture is mip mapped | the GL backend has no mip maps, so the detail pass is faded out over `detailFadeStart..detailFadeEnd` (80..250 m) before the 5 m tiling turns into noise. Past that band the water is the flat reflection colour, which the fog takes over from at 700 m at noon anyway. |
| both ocean effects light the water themselves | `ocean_text` and the base shader are unlit pddi shaders, so the vertex colour carries the shading: `ambient + sum over the directional lights of N.L`, doubled, exactly what `gl/shaders/shader.vert` does for a lit shader. The zone group's two directional lights are the sun and a fill light pointing the other way, in collection order — taking only the first picks the fill. |

Everything else is the game's: sea level 0, water colour 255/255/255,
reflection scale 0.25/0.23/0.25/0.98, detail opacity 0.225, detail scale 0.2, foam 0.15/1.0,
min/max wave length 0.1/12, amplitude ratio 0.013, wind 90° ± 30°, speed scale 1.2.

`renderer::g_oceanEnabled` is the on/off switch (there is no such global in retail; retail
gates on `GetOceanLevelOfDetail() >= 0`), and the Explorer's View tab > Ocean has the rest.
