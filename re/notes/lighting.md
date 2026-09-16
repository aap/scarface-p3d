# Lighting — `pure3d::Light` / `LightGroup`, `renderer::LightManager`, the time of day

How Scarface lights the world, reversed from the PC build (addresses are unpacked-image
VAs) and cross-checked against the SHR-era Pure3D source in
`/u/aap/fun/ps2engines/extracted/shr/libs/pure3d` (`p3d/light.*`, `p3d/lightloader.cpp`,
`p3d/lightschooser.*`, `p3d/anim/lightanimation.cpp`, `p3d/anim/channel.cpp`, `pddi/`).
`[V]` marks what was verified in the disassembly or by decoding the shipped data;
everything else is inference.

Implemented in `light.cpp` (pure3d side), `renderer/lighting.cpp` (renderer side) and
`gl/` (the pddi light API). `renderables.md` §5 has the `LightingRenderable` class layout.

---

## 1. The chunks

### 1.1 `0x00013000` `pure3d::Light` **[V — all 1830 lights in z04 decode cleanly]**

Identical to SHR's `tLightLoader::LoadObject`; Scarface added nothing.

```
0x00013000 {
    pstring name;
    u32     version;            // 0x00000101 everywhere in z04
    u32     type;               // 0 ambient, 1 point, 2 directional, 3 spot
    u32     colour;             // 0xAARRGGBB
    float   attenConstant, attenLinear, attenQuadratic;   // always (1,0,0) in z04
    u32     enabled;
    // children, all optional:
    0x00013001 DIRECTION  { float x, y, z; }
    0x00013002 POSITION   { float x, y, z; }
    0x00013003 CONE_PARAM { float phi, theta, falloff, range; }
    0x00013004 SHADOW     { u32 isShadowCaster; }
    0x00013006 DECAY_RANGE {
        u32   type;             // 0 none, 1 sphere, 2 cuboid, 3 ellipsoid
        float innerX, innerY, innerZ;
        float outerX, outerY, outerZ;
        0x00013007 DECAY_RANGE_ROTATION_Y { float radians; }
    }
    0x00013008 ILLUMINATION_TYPE { u32; }   // 0 positive, 1 zero, 2 negative
}
```

The **decay range is the thing that separates the two kinds of light** in this game: the
four global sun/ambient lights have **no** `0x13006` child, every local lamp/interior light
has one. `renderer::LightManager` keys on exactly that (§3.3).

`tLight::Decay(pos)` (SHR, reimplemented verbatim in `light.cpp`) is 1 inside the inner
range, a smoothstep down to 0 at the outer range and 0 outside; the sphere form only uses
the x component of the ranges, and the ellipsoid form is treated as a cuboid (SHR's own
"HBW TODO"). A rotation about y is applied to the cuboid first.

### 1.2 `0x00002380` `pure3d::LightGroup` **[V]**

```
0x00002380 { pstring name; u32 numLights; pstring lightName[numLights]; }
```
152 in z04, one per `0x08800007`.

### 1.3 `0x08800007` `renderer::SFLightGroupLoader` **[V]**

Already in `renderables.md` §5; re-verified by decoding all 151 in z04:

```
0x08800007 { pstring name; pstring lightGroupName; u32 kind; u32 numControllers;
             pstring controllerName[numControllers]; }
```

### 1.4 The LITE animation, `0x00121000` **[V]**

The generic Pure3D animation chunk (SHR `tAnimationLoader::LoadObject`), unchanged:

```
0x00121000 { u32 version; pstring name; u32 animType /*4CC*/;
             float numFrames; float speed /*fps*/; u32 cyclic;
    0x00121006 {...}                     // Scarface addition, not needed
    0x00121002 GROUP_LIST { u32 version; u32 numGroups;
        0x00121001 GROUP { u32 version; pstring name; u32 groupId; u32 numChannels;
            0x00121104 VECTOR_3DOF { u32 version; u32 param; u32 n;
                                     u16 frame[n]; float xyz[n][3];
                                     0x00121110 INTERPOLATION_MODE { u32 v; u32 mode; } }
            0x00121108 BOOL   { u32 version; u32 param; u16 startState; u32 n; u16 frame[n]; }
            0x00121109 COLOUR { u32 version; u32 param; u32 n;
                                u16 frame[n]; u32 argb[n];
                                0x00121110 INTERPOLATION_MODE { ... } }
        }
    }
    0x00121402 {...}
}
```

The frames and the values are two separate arrays, not interleaved. **The channel
parameter four-CCs are NUL padded, not space padded** as in SHR: `'CLR\0'` (0x00524C43),
`'DIR\0'` (0x00524944), `'PARM'`, `'EABL'`. The animation type is `'LITE'`. **[V — this
is what cost the first implementation its keys]**

### 1.5 `0x00121201` frame controller **[V for the field values]**

SHR's `FRAME_CONTROLLER` is `0x00121200`; Scarface writes `0x00121201` with `version == 1`,
which is the SHR record plus an `'ANIM'` word and one extra `u32`:

```
0x00121201 { u32 version /*1*/; pstring name; u32 type /*'LITE'*/; u32 'ANIM';
             float frameOffset /*0*/; u32 /*1*/;
             pstring targetName /*the Light*/; pstring animationName; }
```
(The order of `frameOffset` and the trailing `u32` cannot be told apart from the shipped
data — both are constant.) 298 of them in z04; only 8 are `'LITE'`.

---

## 2. What is actually in z04 **[V]**

151 `0x08800007` chunks in 71 packages, by `kind`:

| kind | n | where | what |
|---|---|---|---|
| 0 | 2 | `miami_lod.p3d`, `islands_LOD.p3d` | `zone_lights` — the daylight: `MiamiAmbientShape`, `MiamiSunShape`, `MiamiFillShape`, `MiamiBuildingAmbientShape`, 4 controllers |
| 1 | 2 | same two files | `zone_rainlights` — the same four for rain |
| 2 | 30 | `*_detail.p3d` | exterior night lights (gas station, theatre front, shop signs) |
| 3 | 116 | `*_shell.p3d` | one group per interior (`fidelrecords_lights`, `ugintop_lights`, …) |
| 4 | 1 | `Common.p3d` | `lights_template`: `lamp_talllight`, `lamp_shortlight`, `vehicle_headlight` |

The kind 0 group, at the `0x13000` values in the file (i.e. before the animation plays):

| light | type | colour | direction |
|---|---|---|---|
| `MiamiAmbientShape` | ambient | 35, 37, 48 | — |
| `MiamiBuildingAmbientShape` | ambient | 21, 25, 28 | — |
| `MiamiSunShape` | directional, shadow caster | 31, 45, 48 | 0, −0.788, −0.616 |
| `MiamiFillShape` | directional | 6, 6, 6 | −0.863, 0.087, 0.498 |

Those are the *midnight* values — the file keeps the last exported frame. Sampled at
**frame 120 of 241 (noon)** the same four are

| light | colour | direction |
|---|---|---|
| `MiamiAmbientShape` | **63, 52, 31** | — |
| `MiamiBuildingAmbientShape` | 52, 44, 27 | — |
| `MiamiSunShape` | **99, 97, 72** | −0.473, −0.743, 0.473 (miami_lod) / −0.5, −0.707, 0.5 (islands_LOD) |
| `MiamiFillShape` | 6, 6, 6 | −0.863, 0.087, 0.498 |

which is within a few units of the `(51,43,27)` / `(97,95,70)` aap had eyeballed into
`glShader::SetPass`.

---

## 3. `renderer::LightManager` = `g[0x008111cc]`

Ctor `0x00460600`, 0x70 bytes. `rendercore.md` §9.3 has the room / template-light members.

### 3.1 `RegisterLightGroup(LightGroup*, u32 kind)` `0x0045fe10` **[V]**

A `switch(kind)` (jump table at `0x0045ff70`), i.e. the manager stores the group *by kind*:

| kind | what it does | member |
|---|---|---|
| 0 | ref-counted assign | `+0x20  LightGroup *zoneGroup` |
| 1 | ref-counted assign | `+0x24  LightGroup *zoneRainGroup` |
| 2 | `push_back` + AddRef | `+0x00/+0x04/+0x08  vector<LightGroup*>` |
| 3 | `push_back` + AddRef | `+0x10/+0x14/+0x18  vector<LightGroup*>` |
| 4 | for every light of the group: `new TemplateLight(light->uid, light)` (0x8c bytes, ctor `0x0045f750`), `push_back` | `+0x4c/+0x50  vector<TemplateLight*>` |

So kind 4 is not a light set at all: it turns `lights_template` into the prototype table
that `AddTemplateLight(templateId, xform)` (`0x0045fbd0`) instantiates for street lamps and
car headlights.

### 3.2 The `LightingRenderable` ctor `0x0046ff10` **[V]**

```c
LightingRenderable(LightGroup *group, LightAnimationController **ctrls, int n, u32 kind) {
    Renderable(0);  typeMask = 0x400;
    kind_ = kind;  numControllers = n;  Assign(group);  AddRef every controller;
    if (group) {
        gLightManager->RegisterLightGroup(group, kind);          // 0x45fe10
        if (kind == 0)
            gLightManager->AddLightingZone(g[0x8113a0] /*GetHash("zone_lights")*/, 0, group->uid);  // 0x460380
        if (kind == 0 || kind == 1)
            this->AttachToTimeOfDay();                           // 0x46fcd0
    }
}
```

`0x0046fcd0` **[V]**: if the renderable has controllers, compare the *group's* UID against
`g[0x8113a0] = GetHash("zone_lights")` and `g[0x8113a4] = GetHash("zone_rainlights")` (both
built by the static initialisers at `0x0071cd70` / `0x0071cd90`) and call
`RenderManager::env->0x0046a270(this, isClear)`, which stores the renderable in
`env+0x00` (clear) or `env+0x04` (rainy). It then reads the clock (`0x00469ea0`) and feeds
it back through `0x0046af70` so the animations start at the right frame.

### 3.3 `LightManager::Update(TimeInfo*)` `0x00460130` **[V]**

Called from `RenderManager::Update` (`0x0046786a`). It is the "which lights can reach the
camera" pass, and it fills a `pure3d::LightsChooser` at `+0x58`:

```c
if (!renderMgr->env->+0x42) return;                  // lighting enabled at all
if (g[0x8251a8]) {                                   // camera object present
    this->camPos = renderCamera->GetPosition();      // +0x5c .. +0x64
    this->radius = 50.0f;                            // +0x68   [V: 0x42480000 at 0x46017e]
}
chooser->RemoveAllLights();                          // 0x685bf0
for (group : kindTwoGroups /* +0x00 */)              // only the kind 2 vector  [V]
    for (light : group->lights) {
        switch (light->decayType) {
        case 0: case 2:
            if (!0x4d6de0(light)) { chooser->AddLight(light); break; }
            /* fall through to the distance test */
        case 1:
            if (|light->+0x18 - camPos|² < radius²) chooser->AddLight(light);
            break;
        default: break;                              // decayType 3 is dropped
        }
    }
for (t : templateLightInstances /* +0x38, 0x100 slots of 4 bytes */)
    if (t->+0x45 /*inUse*/ && |t->+0x30 - camPos|² < radius²)
        for (l : lightVector /* +0x48..+0x4c */)
            if (l->classId == t->+0x40 && (x = 0x0045f870(t)) != nil)
                chooser->AddLight(x);
renderMgr->env->0x0046ba90(chooser, true);           // add the zone lights on top
```

(The vector at `light+0x18` is the light's position — the same three floats the distance
test uses for every light type; `0x004d6de0` is a predicate on the light that lets a
no-decay or cuboid light skip the distance test entirely.)

Note that **only the kind 2 (exterior/detail) groups are in this loop**; the 116 kind 3
(interior) groups sit in the other vector and must be selected elsewhere — presumably by
`LightManager::FindRoom` / the lighting zones, which the viewer does not have.

### 3.4 `EnvManager::AddZoneLights(chooser, excludeBuildingAmbient)` `0x0046ba90` **[V]**

```c
LightingRenderable *r = (env->+0x44 /*camera indoors*/ && env->+0x04) ? env->+0x04 : env->+0x00;
if (!r) return;
for (light : r->group->lights)
    if (!(light->decayType == 0 && excludeBuildingAmbient && IsBuildingAmbient(light)))
        chooser->AddLight(light);
if (env->+0x45) chooser->AddLight(env->+0x08);
```

`IsBuildingAmbient` is `0x0046b4f0` **[V]**: `light->uid == GetHash("MiamiBuildingAmbientShape")
|| light->uid == GetHash("MiamiRainyBuildingAmbientShape")`. The only caller of `0x46ba90`
passes `true`, so **the building ambient is never part of the general light set**; it
belongs to the `buildinglights` world geo (the lit windows at night), whose
`WorldGeoRenderable` dtor `0x00471a70` unregisters it from the env by exactly that name
hash (`g[0x8113a8]`).

Also note the slot the renderable goes into is picked by **rain**, not by the camera:
`env+0x00` is the clear group and `env+0x04` the rainy one; the `env+0x44` test only
decides which of the two to prefer when the camera is indoors.

### 3.5 `pure3d::LightsChooser`

RTTI `??_R4LightsChooser@pure3d@@` at `0x0079ca74`, vtable `0x00769ee8`; SHR's
`tLightsChooser` is the same class. Its job is "given N world lights and a target point,
make at most 4 directional lights that look like them", with an accumulated ambient
colour, a cache keyed on the target position, and a separate "best shadow casters" query.
`GetBestLights(group, target, cache)` is called **per lit object**, which is why a lamp can
outshine the sun for the car next to it without touching the rest of the city. The viewer
does not implement it (§6).

---

## 4. How a light reaches the hardware **[V]**

`d3dContext` vtable `0x00767a3c`. The virtual order matches SHR's `pddiBaseContext` with a
constant offset of **+12** in the lighting/state region (checked against the two entries
already known: `SetColourWrite` = SHR 52 → index 64 = `0x0064b8d0`, `SetZWrite` = SHR 58 →
index 70 = `0x0064b990`):

| index | offset | method | d3d | base |
|---|---|---|---|---|
| 42 | +0xa8 | `GetMaxLights` | | `0x006e3eb0` |
| 43 | +0xac | `SetAmbientLight` | `0x0064b850` | `0x00659410` |
| 44 | +0xb0 | `GetAmbientLight` | | `0x00659430` |
| 45 | +0xb4 | `SetLight` | | `0x00659450` |
| 46 | +0xb8 | `EnableLight` | | `0x00659580` |
| 47..61 | | `IsLightEnabled` … `GetLightCone` | | `0x006595c0` … `0x00659a90` |

`d3dContext::SetAmbientLight` `0x0064b850` is `base::SetAmbientLight(colour);
device->SetRenderState(0x8b /* D3DRS_AMBIENT */, colour)` — **that is the proof of the
mapping**; the ambient really is one global render state.

The `pure3d::Light` subclasses push themselves through it:

* `AmbientLight::Update` `0x006894a0` = `renderContext->SetAmbientLight(this->colour)`
  (vtable `+0xac`).
* `PointLight::Update` `0x006893a0` builds a `pddiLightDesc` on the stack (enabled,
  colour, position, `attenuation = (1,0,0)`, range `0x4b189680` = 1e7) and calls
  `renderContext->SetLight(this->slot, &desc)` (vtable `+0xb4`).
* `Light::Activate(slot)` is vtable slot 9 (`0x00688c60`), `Deactivate` slot 10
  (`0x00688c40`), `Update` slot 11 (pure virtual on the base).

---

## 5. Time of day

The four `LITE_Miami*Shape` animations are 241 frames at 30 fps — one 24 h day, one frame
every six minutes. `EnvManager` (`RenderManager::env`, `+0x1c`, `rendercore.md` §8) owns
the clock:

* `+0x30` = time of day in **milliseconds**. `GetTime(h,m,s)` `0x00469ea0` is three magic
  divisions out of it; `SetTime(h,m,s)` `0x0046af70` is `((h*60+m)*60+s)*1000`, and it also
  sets `+0xc24 = (ms > 43200000)` (afternoon) and `+0x38 = 0x1499`. **[V]**
* `+0x34` = the **animation** time, computed by `0x0046a400` **[V for the shape, not the
  meaning]**: the day is divided by six key-frame times at `env+0x60..0x74` and each
  segment has a "hold" (`+0x78`) and a "transit" (`+0x90`) duration, so the clock is
  *remapped*, not scaled — dawn and dusk are stretched and the long flat parts of the
  curve are held. The result is taken modulo 86400000.

Facade: `TimeOfDay_SetTime` `0x00461ae0`, `_SetSpeed` `0x00461b00`, `_Enable` `0x00461b50`,
`_SetKeyFrameTime` `0x00461c10`, `_SetTimeToHold` `0x00461ba0`, `_SetTimeToTransit`
`0x00461b80`, `_EnableRain` `0x00461bc0`, `_IsRaining` `0x004642a0`.

`pure3d::LightAnimationController` (SHR `tLightAnimationController::UpdateNoBlending`)
samples the `'CLR\0'`, `'PARM'`, `'DIR\0'`, `'EABL'` (and for spots `'ATTN'`, `'CONE'`)
channels at the current frame and writes them into the light. In z04 only `'CLR\0'` and
`'DIR\0'` carry real curves.

Sun colour and direction over the day (`islands_LOD.p3d`, key frames):

```
frame    0   40   60   90  120  180  207  210  229  234  240
colour  1f2d30 253134 3c4038 5e593f 636148 635c36 3b2919 372316 272927 232b2b 1f2d30
```

---

## 6. What the viewer does and what it leaves out

Implemented (`light.cpp`, `renderer/lighting.cpp`, `gl/`):

* the four chunk loaders and `renderer::SFLightGroupLoader` / `LightingRenderable`;
* `renderer::LightManager` with the retail per-kind registration, the 50 m camera sphere,
  the decay test and the building-ambient exclusion;
* the LITE animation and `LightAnimationController`, sampled at a settable hour;
* `pddiContext::GetMaxLights / SetAmbientLight / SetLight / EnableLight` with a
  `pddiLightDesc`, and a GL vertex shader that does ambient + 4 directional/point lights.

Deliberate deviations, all marked in the code:

1. **No `pure3d::LightsChooser`.** There is one light set for the whole frame, chosen at
   the camera, instead of four directional lights recomputed per lit object. The zone
   lights therefore keep the first pddi slots, otherwise a lamp next to the camera would
   push the sun out of the last one.
2. **The kind 3 (interior) groups are treated like the kind 2 ones** — in range of the
   camera or not. Retail only walks the kind 2 vector in `LightManager::Update`; the
   interior ones must come in through rooms / lighting zones, which the viewer has no
   equivalent of.
3. **The clock is mapped linearly** onto the 241 frames instead of through the six
   key-frame / hold / transit bands of `0x0046a400`.
4. **Point lights keep their decay range as a shader falloff** instead of being collapsed
   into a directional light by the chooser. pddi has no such field; `pddiLightDesc` carries
   `innerRange`/`outerRange` as an addition.
5. No template light *instances* (street lamps, headlights): `lights_template` is loaded
   and registered, but nothing calls `AddTemplateLight`.
6. No lighting zones, no rooms, no shadow-caster query, no `buildinglights` night windows.
