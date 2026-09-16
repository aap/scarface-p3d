# Distance fog — where the values come from and what the hardware does with them

PC retail, unpacked-image VAs (`re/dis.sh`). Companion to `rendercore.md` (the Canvas),
`lighting.md` (the same time-of-day clock) and `displaylist.md` (which lists are fogged).

**[V]** = read out of the disassembly or the data. **[?]** = inferred / guess.

---

## 0. The chain, top to bottom

```
scriptc/graphanims.cso                12 EnvironmentObjects: environment_{clear,rainy}_{4,9,12,18,21,24}
      │  FogStart FogEnd FogColor_Red/Green/Blue/Alpha FogEnabled FogClamp
      ▼
EnvironmentObject::ApplyChanges  ->  Renderer_AddEnvParam(EnvParameters)     registry g[0x8112c0]
      ▼
packages/z04/miami_lod.p3d            the TODObject "tod": six key hours + the two names each
      │  TimeOfDay_SetKeyFrameTime / AttachClearEnvParam / AttachRainyEnvParam
      ▼
EnvManager (g_renderMgr+0x1c)         blend by time of day, then blend clear<->rainy by the rain %
      ▼  Renderer_ApplyEnvParameters 0x46b300
renderer::View_SetFog(FogParameters) 0x464e40     packs the colour, truncates the clamp, time = 0
      ▼  0x461ed0 (both canvases)
renderer::Canvas::SetFog 0x458300     -> the canvas fields and the pure3d::View
      ▼  pure3d::View::BeginRender 0x67ddb0
pddiContext::EnableFog / SetFog / SetFogClamp
      ▼  pddiFogState::Sync 0x658270
d3dContext::EnableFog 0x64bb80 / d3dContext::SetFog 0x64bbb0
      ▼
D3DRS_FOGENABLE, FOGTABLEMODE = D3DFOG_LINEAR, FOGCOLOR, FOGSTART, FOGEND
```

---

## 1. The values **[V]**

`EnvironmentObject` is in the leak (`scarface_src/ocean/environmentobject.*`,
`si_environmentobject.cpp`); its eight fog properties are registered in retail at
`0x005e0750..0x005e0829` with these offsets into `EnvParameters::fogParams`:

| property | type | offset |
|---|---|---|
| `FogEnabled` | bool | +0x34 |
| `FogColor_Red / Green / Blue / Alpha` | s32 | +0x38 / +0x3c / +0x40 / +0x44 |
| `FogStart` | f32 | +0x48 |
| `FogEnd` | f32 | +0x4c |
| `FogClamp` | f32 | +0x50 |

The instances are **not** in any p3d: they are created by the compiled script
`scriptc/graphanims.cso` in `cement.rcf` (the islands have their own,
`scriptc/island_envobjects.cso`; `*_xbox.cso` variants exist and differ only in `FogClamp`
— 185..240 instead of 10..70 — so use the non-`_xbox` files for the PC).

### z04 / Miami, clear **[V]**

| hour | start | end | R | G | B | A | clamp |
|---|---|---|---|---|---|---|---|
|  4 | 20 | 800 |  23 |  28 |  40 |  95 | 10 |
|  9 | 25 | 800 | 245 | 240 | 160 | 128 | 70 |
| 12 | 25 | 900 | 200 | 200 | 150 | 110 | 60 |
| 18 | 25 | 800 | 227 | 223 | 105 | 150 | 65 |
| 21 | 20 | 800 |  50 |  35 |  33 |  95 | 45 |
| 24 | 20 | 800 |  20 |  25 |  32 |  95 | 15 |

### z04 / Miami, rainy **[V]**

| hour | start | end | R | G | B | A | clamp |
|---|---|---|---|---|---|---|---|
|  4 | 10 | 525 | 16 | 16 | 17 |  95 | 18 |
|  9 | 20 | 700 | 35 | 35 | 23 | 128 | 50 |
| 12 | 20 | 700 | 35 | 35 | 28 | 110 | 45 |
| 18 | 20 | 600 | 35 | 35 | 21 | 150 | 55 |
| 21 | 20 | 700 | 26 | 21 | 21 |  95 | 45 |
| 24 | 10 | 500 | 14 | 14 | 16 |  95 | 20 |

`FogEnabled` is 1 in all twelve. Distances are world units, the same ones the ZonePkg draw
distances use — so the fog is fully opaque well before the 3000 m skyline band, which is
why the low-LOD city reads as a silhouette in the haze rather than as geometry.

The islands (`island_envobjects.cso`) are the same shape: clear 4h `20/700 (25,30,40) a95
clamp 15`, 9h `20/800 (205,200,132) a128 clamp 80`, 12h `20/800 (190,190,145) a110 clamp
90`, 18h `20/800 (217,213,100) a150 clamp 80`, 21h `20/800 (65,45,43) a95 clamp 45`, 24h
`20/600 (20,25,32) a95 clamp 15`; rainy starts at 2 and ends at 500..550.
One more, fully plain-text, is the `environment` script object of
`packages/z04/DevilsCay_01_shell.p3d`: `fogstart 0, fogend 400, 255/250/223, alpha 128,
fogenabled 1, fogclamp 150, fogtransitiontime 0`.

### The key frames **[V]**

The `TODObject` named `tod` in `packages/z04/miami_lod.p3d` (a plain `0x09900190`
script-object block, so `re/p3dblock.py` reads it):

| slot | hour | hold | transit | clear | rainy |
|---|---|---|---|---|---|
| 0 |  4   |   0 | 200 | environment_clear_4  | environment_rainy_4  |
| 1 |  9   |  20 | 360 | environment_clear_9  | environment_rainy_9  |
| 2 | 12.0 |   0 | 700 | environment_clear_12 | environment_rainy_12 |
| 3 | 18.0 |  40 | 340 | environment_clear_18 | environment_rainy_18 |
| 4 | 21.0 | 120 | 260 | environment_clear_21 | environment_rainy_21 |
| 5 | 24.0 |  40 | 200 | environment_clear_24 | environment_rainy_24 |

plus `TimeOfDaySpeed 2000`, `SetTimeOfDayHour 18`, `SetTimeOfDayMinute 00`,
`PauseTimeOfDay 0`, `EnableRaining 0`, `RainPercentage 1`, `Rain{Start,End}TransitFrames 50`.
`islands_LOD.p3d` has the island one. The same six hours drive the `LITE` light animations
(`lighting.md`), so fog and lighting share one clock.

`EnvManager::PickTimeOfDayKeys 0x469fd0` turns the clock into (keyA, keyB, t) using the
per-key hold and transit lengths; `ComputeBlendedEnvParams 0x46b180` lerps the whole
`EnvParameters` (`0x46a720`) and, when it is raining, lerps the rainy pair as well and
cross-fades the two by the rain percentage.

### Reading the compiled script

`.cso`/`.dso` are Torque-derived. Layout: `u32 version | u32 globalStringTableSize + bytes
| u32 functionStringTableSize + bytes | u32 globalFloatCount + f64[] | u32
functionFloatCount + f64[] | u32 codeSize | code | identTable`. A code word is one byte, or
`0xFF` followed by a **u16**; the ident table is `u32 count`, then per entry `u32
stringOffset, u32 n, u32 ip[n]`, and each `ip` is patched into the code (that is how
identifiers get in — they are 0 in the stream).

Identifiers are **hashed**: the string table holds `stx` + eight characters in `a..p`,
which are the nibbles of `core::GetHash` (retail `0x6dc190`, the x65599/0x7fffffff hash the
repo already has in `core.cpp`) — e.g. `EnvironmentObject` -> `stxgaaiomgj`,
`FogStart` -> `stxalkjneok`. Field assignments are the eleven-word pattern
`72, 0, <valueStringOffset>, 77, 51, 52, <identFieldName>, 0, 82, 59, 62`, and the fields
come out in `REGISTER_MEMBER` order, so the first eight of every `EnvironmentObject` are
exactly the fog block above.

---

## 2. What reaches the hardware **[V]**

`pure3d::View::BeginRender 0x67ddb0`, fog fields `+0x30` colour, `+0x34` start, `+0x38`
end, `+0x3c` clamp, `+0xc0` enable:

```c
if (view->fogEnabled) {
    ctx->EnableFog(true);                                    // vslot +0x158
    ctx->SetFog(view->colour, view->start, view->end);       // vslot +0x160
    ctx->SetFogClamp(view->clamp);                           // vslot +0x168  (Scarface's own)
} else
    ctx->EnableFog(false);
```

`pddiFogState` (vtable `0x768628`, `Sync` `0x658270`) is the stock SHR one:
`{ vtable, bool enabled +0x04, pddiColour colour +0x08, float start +0x0c, float end +0x10 }`.

`d3dContext::SetFog 0x64bbb0` writes exactly four render states:

```
SetRenderState(0x23 D3DRS_FOGTABLEMODE, 3 = D3DFOG_LINEAR)
SetRenderState(0x22 D3DRS_FOGCOLOR,     colour)             // 0xAARRGGBB
SetRenderState(0x24 D3DRS_FOGSTART,     start)
SetRenderState(0x25 D3DRS_FOGEND,       end)
```

and `d3dContext::EnableFog 0x64bb80` writes `0x1c D3DRS_FOGENABLE`. So:

* **linear**, never EXP/EXP2 — `FOGTABLEMODE` is written in exactly one place in the whole
  image and always with `D3DFOG_LINEAR`;
* **per pixel** — it is the *table* mode; `D3DRS_FOGVERTEXMODE` (0x8c) is never written
  anywhere in the image;
* **by depth, not by range** — `D3DRS_RANGEFOGENABLE` (0x30) is never written, so it keeps
  its `FALSE` default: the factor comes from the eye-space depth, not the radial distance;
* `D3DRS_FOGDENSITY` (0x26) is **never** written. There is no density in this engine.

So the visible fog is the plain `f = (end - d)/(end - start)`, clamped to 0..1, blended
into the colour only (D3D's fog stage does not touch alpha, and the alpha byte of
`FOGCOLOR` is ignored by D3D).

### `FogClamp` (and the colour's alpha) **[V] for the plumbing, [?] for the meaning**

`pddiBaseContext::SetFogClamp 0x659db0` is `ctx->[0x208] = min(value, 255)` and
`GetFogClamp 0x659de0` reads it back. **`d3dContext` overrides neither**, so the clamp
reaches no D3D state at all. What consumes it is Scarface's own shaders: a helper repeated
in every d3d shader (`0x709e34..0x709ed1` and a dozen inline copies) does

```c
ctx->GetFog(&colour, &start, &end);
float clamp = saturate(ctx->GetFogClamp() * (1.0f/255.0f));
float c49[4] = { start, end, 1.25f/(start + end), clamp };
vertexProgram->SetShaderConstant(0x31 /* c49 */, c49, 1);
```

`d3dVertexProgramManager::GetVertexProgram 0x656c50` also branches on the fog state, so fog
is part of the vertex-program variant key. What the programs themselves do with `c49` is
**not** reversed — "a cap on how opaque the fog may become" is the obvious reading but a
guess. Same for `FogColor_Alpha`: it is packed into the colour (`A<<24|R<<16|G<<8|B` in
`View_SetFog 0x464e40`) and D3D drops it, so on the fixed-function path it does nothing.

### Two corrections to `rendercore.md` §4

1. `Canvas+0x24` is **`FogClamp`**, not a density: `(int)EnvironmentObject::FogClamp`,
   default 200, clamped to 0..255 by `SetFogClamp`. There is no `FogDensity` string in the
   image.
2. `Canvas::UpdateFog`'s lerp is **dead on PC**: the only producer, `View_SetFog 0x464e40`,
   pushes a hard-coded `0.0f` transition time (`push 0` at `0x464e62`), so `SetFog` always
   takes the snap branch. Smooth fog changes come from the time-of-day blend upstream.
   (`FogTransitionTime` is commented out in the leak and 0 in the shipped data.)

---

## 3. Where fog is switched off **[V]**

Every `EnableFog` call site, by function:

| function | lists | what |
|---|---|---|
| `Display_List::Render 0x45e680` | 46, 47 | `IsFogEnabled` saved at `0x45e6c3`, off at `0x45e715` around `RenderSky`, **restored** at `0x45e734` |
| " | 9, 10 | off at `0x45e8e6`, on at `0x45e902` — a **hard-coded `EnableFog(true)`**, not the saved flag |
| " | 76 | off at `0x45ef72` around `RenderCameraLocked76`, restored at `0x45ef98` |
| `RenderDecals_3_17_18_4_75 0x45b590` | 18, 4 | off/on(1) around each of the two |
| `RenderDecalsFading_5_19_20_6 0x45b870` | 20, 6 | off/on(1) around each of the two |
| `RenderNightLights11 0x45db80` | 11 | off for the whole list, on(1) after |
| `RenderReflection 0x45bb90` | 46, 47 | both `RenderSky` calls bracketed with save/off/restore |
| `Scene::Render 0x468b80` | — | off around the pre-pass over the renderables with `flags80 & 0x10` |
| `pure3d::BillboardQuadGroup::Display 0x698940` | — | saves, forces off, restores — **the sun, the flares and the stars are never fogged** on top of the list-76 bracket |
| `pure3d::SpriteParticleEmitter::Display 0x686a90` | — | same pattern: sprite particles are never fogged |
| `sub_651330 / sub_651be0 / 0x69feae / 0x6a06dd` | — | the fullscreen blits save the device state and force fog off |

Everything else is fogged, including the low-LOD skyline (59, 2) drawn with
`d3dVertexFadeShader`.

`pddiBaseShader` also has a per-shader opt-out: `PDDI_SP_ISFOGGED = PDDI_FOURCC('F','O','G',0)`
(SHR `pddishade.hpp`).

---

## 4. What the viewer does

`renderer::EnvManager` (`renderer/render_manager.*`) carries the six key frames and the
table of §1 and pushes the interpolated `FogParameters` into `Canvas::SetFog` from
`RenderManager::Update`, driven by `LightManager::timeOfDay` / `raining` — the same clock
that plays the light animations. `Canvas::ApplyFog` then does the three pddi calls of §2
before the scene is drawn.

The GL backend implements `EnableFog/IsFogEnabled/SetFog/GetFog/SetFogClamp/GetFogClamp` in
`glState`/`glContext`, and the shaders do the linear ramp per pixel over the eye-space
depth. The clamp is carried all the way to the shader but **not applied** unless the "apply
FogClamp" box in the View tab is ticked, because §2 leaves its meaning unverified.

Deviations, all deliberate:

* we lerp linearly between the key hours instead of using the per-key hold/transit lengths;
* rain switches the curve instead of cross-fading it over `RainStartTransitFrames`;
* there is only one env parameter set (the z04 one) — the islands' table is in §1 but is
  not wired up, and neither is the per-level `environment` object of a shell package.
