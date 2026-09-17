# The static shadows — shadow decals, the StaticShadowGen extension, shadow volumes

PC retail, addresses are unpacked-image VAs (`re/dis.sh`). Companion to
`notes/displaylist.md` (§1 group 10 and group 21), `notes/renderables.md` §6 (the 0x08800008
chunk) and `notes/renderable_classes.md` §5.10 (`ShadowRenderable`).

**[V]** = read out of the disassembly / the data. **[?]** = inferred.

There are **three** unrelated shadow mechanisms in the renderer and it is worth separating them
before anything else:

| # | what | data | layer | lists | how it is drawn |
|---|---|---|---|---|---|
| 1 | **static shadow decals** — the dark blobs baked into the ground geometry under trees, awnings and walls | prim groups of the world geo whose pddi shader is `shadowdecal` | 37 (or 1 for the unclassified case) | **7** / **8** (fading) / **77** | into the frame-buffer alpha channel, then one full-screen multiply (§2) |
| 2 | **building shadow volumes** — the 44 `*_shadow` composites of chunk 0x08800008 | `0x0001001a` `pure3d::ShadowMesh` (+ `0x0001001b` topology) inside a `0x00023000` composite | **2** | **61** | stencil shadow volumes + a dark wash (§4) |
| 3 | blob shadows under cars and NPCs | the same chunk with `isBuildingShadow == 0`, plus two built-in blob geometries | 2 | 62 / 63 / 64 | `ShadowRenderable::Display` projects a quad (§3.3) |

Everything below is about 1 and 2 — 3 needs a player/vehicle and the viewer has neither.

---

## 0. TL;DR

* The shadow-decal pass **is** enabled in retail: `g_byte[0x007bfb55]`, the flag that gates lists
  7/8/77 in `Display_List::Render`, holds **1** in the image (§1.1). So does
  `g_byte[0x007bfa0c]`, the one that gates the real stencil begin/end of the shadow-volume pass,
  and `g_byte[0x007c0b46]`, the one `ShadowRenderable::Display`/`::Update` test. **[V]**
* A shadow decal is **not** a dark polygon blended onto the road. The pass is bracketed by
  `pddiExtStaticShadowGen::Begin/End` (pddi extension `0x108`), which redirects it into a
  screen-sized `D3DFMT_A8R8G8B8` render target with `SetColourWrite(0,0,0,1)` — **only the alpha
  channel is written** — and then multiplies the whole frame by that alpha with one screen-space
  quad (`SRCBLEND = ZERO`, `DESTBLEND = SRCALPHA`). §2.
* For a single, non-overlapping decal that is *arithmetically the same thing* as blending black
  over the ground with the decal's own alpha (`dest *= (1-a)` ≡ `mix(dest, black, a)`), which is
  why the plain-decal approximation the viewer now does looks right. What the mask buys retail is
  that overlapping decals do not double-darken and that nothing drawn after them can paint over
  them.
* The PC build is **Direct3D 9**, not D3D8: the extension talks to the device through its own
  `IDirect3DDevice9` vtable and every offset used (`+0x88` `StretchRect`, `+0x94/+0x98`
  `Set/GetRenderTarget`, `+0xac` `Clear`, `+0xe4/+0xe8` `Set/GetRenderState`, `+0x104`
  `SetTexture`, `+0x10c` `SetTextureStageState`, `+0x114` `SetSamplerState`, `+0x14c`
  `DrawPrimitiveUP`, `+0x164` `SetFVF`, `+0x170` `SetVertexShader`, `+0x1ac` `SetPixelShader`,
  and `+0x48` `GetSurfaceLevel` on a texture) lands exactly on the D3D9 vtable. **[V]**
* **The cause of the hard cuts is the data, not the blend**: 397 of the 408 geometries that carry
  a `shadowdecal` prim group are `details_*` world geo, whose draw distance is the *details* band
  — **120 m with a 20 m fade** (`WorldGeoRenderable::Display`, notes/renderspine.md §4.5) — and
  only 47 of the 220 z04 packages have any shadow decals at all. §5.

---

## 1. Where the decals come from and where they go

### 1.1 The gate in `Display_List::Render` (0x0045e680) **[V]**

```c
// ... group 9: decals (3,17,18,4,75), interior floors (33,34), projected shadows (62) ...
if (g_byte[0x007bfb55] && (lists[7].head || lists[8].head)) {
    pddiExtStaticShadowGen *ext = renderContext->GetExtension(0x108);   // ctx vslot +0x190
    ext->Begin();                                                       // ext vslot +0x1c
    this->RenderShadowDecals_7_8_77();                                  // 0x0045caf0
    ext->End();                                                         // ext vslot +0x20
}
```

`0x007bfb55` has exactly **one** reference in the whole image (this `cmp`), and no code anywhere
writes it — but the unpacked image is a *memory dump of the running game*, and the byte reads
**1** in it, as do its neighbours (`0x7bfb54 = 1`, `0x7bfa0c = 1`, `0x7bfa08 = 600.0f`). It is a
plain initialised `bool` in the data segment (IDA's segment list in `re/README.md` puts the
`.data`/`.bss` split a little early), i.e. a compile-time "draw the static shadow decals" switch
that shipped **on**. **[V]** *(the same argument applies to `0x7bfa0c` and `0x7c0b46`, §4/§3.)*

The cross-check for reading these bytes out of the image: `0x007c0c60`, which `notes/sky.md`
already calls "a `.data` bool that starts out true" because both `SkyRenderable::Display` and
`::Update` bail out on it and nothing writes it either, also reads **1** — and the sky is
obviously drawn in the retail game. Same shape, same conclusion. **[V]**

### 1.2 `RenderShadowDecals_7_8_77` — 0x0045caf0 **[V]**

```c
void Display_List::RenderShadowDecals_7_8_77(void) {
    renderContext->SetZWrite(false);                    // ctx +0x118, for the whole function
    Camera *cam = GetCurrentCamera();                   // 0x461ad0
    if (lists[7].head) {
        renderContext->GetExtension(0x10b);             // result DISCARDED, no mode bracket
        for (nd : lists[7]) if (IsNodeVisible(cam, nd->elem, &nd->matrix)) {
            ctx->PushMatrix(0); ctx->MultMatrix(0, &nd->matrix);
            nd->elem->prim->Display();                  // prim vslot +0x20
            ctx->PopMatrix(0);
        }
    }
    if (lists[8].head) {                                // the fading half
        renderContext->GetExtension(0x10b);             // discarded again
        for (nd : lists[8]) if (IsNodeVisible(...)) {
            float f = nd->container->GetFadeAmount();   // container +0x50
            if (f <= 0.0f) f = 0.0f;                    // fcomp against 0x72f994 == 0.0f
            prim->SetFade(f);                           // prim vslot +0x38
            ... Display() ...
            prim->SetFade(0.0f);
        }
    }
    for (nd : lists[77]) {                              // no IsNodeVisible, no extension
        prim->SetFade(nd->container->GetFadeAmount());
        ... PrimArrayEntry::Display(nd->elem) ...       // 0x683340, not prim->Display()
        prim->SetFade(0.0f);
    }
    renderContext->SetZWrite(true);
}
```

So: **z-write off, z-test on, per-node frustum culling for 7 and 8, the container fade applied to
8 and 77, and no shader-mode bracket** (the two `GetExtension(0x10b)` calls are leftovers — the
result is never used, which is unique to this function).

### 1.3 How a world-geo primitive gets into 7/8/77 **[V]**

`pure3d::Shader::ctor` maps the pddi shader name to a type byte, `"shadowdecal" -> 0x0a`
(0x67eaa9), and `WorldGeoLoader::LoadObject` maps shader type `0x0a` to **layer 37**
(`renderer/renderable.cpp SetPrimLayerByShader`). `AddContainerElement` then does

```
case 37: listID = isFading ? 8 : 7;
case  1: if (shaderType == SHADOWDECAL) { listID = 77; if (isFading) sortKey = 1.0f; }
```

i.e. **7** = shadow decals, **8** = the same while their world geo is cross-fading, **77** = the
"unclassified layer 1" case (a shadowdecal shader on a mask-1 primitive).

### 1.4 The `shadowdecal` pddi shader — `pure3d::d3dShadowDecalShader` **[V]**

* ctor `0x00709110`, vtable `0x00774e1c`, `SetPass` = vtable `+0x3c` = **0x007091b0**, created by
  the factory `0x007046e0` (`malloc(0x84)`).
* The ctor calls `d3dSimpleShader::SetBlendMode(2 /*PDDI_BLEND_ADD*/)` and clears the texture
  slot; the shader *data* then overrides the blend mode — every `shadowdecal` shader in z04 is
  `..._shadowdecal_blmd1_mcbv0_lit0_alpha1_atst0_alum0_2sid0`, i.e. **blend ALPHA, unlit, no
  alpha test, no two-siding**. (`blmd1` = `PDDI_BLEND_ALPHA` = `SRCALPHA`/`INVSRCALPHA`, from the
  16-byte blend table at `0x7ec848` + the pddi→D3D factor map at `0x7ec8c8`.)
* `SetPass` (0x7091b0), the parts that differ from `d3dSimpleShader::SetPass`:
  * if it has a texture: `d3dState::SetTextureStage(0, D3DTOP_MODULATE, D3DTA_DIFFUSE,
    D3DTA_TEXTURE)` and the same for the alpha channel
    (`SetTextureStageAlpha(0, MODULATE, DIFFUSE, TEXTURE)`), then `SetSamplerStates(0,0)`.
    So **colour = diffuse·texture and alpha = diffuse.a·texture.a**.
  * `D3DRS_SPECULARENABLE = 0`, `D3DRS_SHADEMODE` from the shader, `d3dState::SetMaterial(...)`,
    then `0x65b4a0` = the ordinary `SetAlphaBlend(blendTable[effectiveBlendMode])` +
    `SetAlphaTest(...)`.
  * it ends by tail-calling `renderContext->SetZWrite(false)` (`[shader+0x10]->[+0x118](0)`) — the
    shader turns z-write off by itself as well as the list walk doing it. **[V]**
* The textures are 128×128 A8R8G8B8 with `alphaDepth = 8`: **black shapes, alpha = coverage**
  (verified by extracting `r_tree_shadow.tga` out of `downtown_region.p3d` — it is a PNG of black
  tree silhouettes on alpha 0). **[V]**

---

## 2. `pddiExtStaticShadowGen` — the render-to-alpha shadow generator

`d3dContext::GetExtension(0x108)` → `ctx+0x52c`, RTTI name `d3dExtStaticShadowGen`
(`notes/displaylist.md` §4). vtable **0x007687e4**, 13 slots, ctor **0x0065dad0**:

| slot | addr | what |
|---|---|---|
| 0 `+0x00` | 0x65d0b0 | dtor |
| 1,2,4,6 | `nullsub_2` | empty |
| 3 `+0x0c` | `0x438400` = `ret 4` | `BeginVolumes(bool)` — **empty on PC** |
| 5 `+0x14` | `0x438400` = `ret 4` | `EndVolumes(bool)` — **empty on PC** |
| 7 `+0x1c` | **0x0065cf60** | `Begin()` — the static-decal begin (§2.2) |
| 8 `+0x20` | **0x0065d0f0** | `End()` — the static-decal composite (§2.3) |
| 9 `+0x24` | **0x0065cfd0** | stencil-volume begin (§4) |
| 10 `+0x28` | 0x0065dac0 → 0x65d6d0 | stencil-volume end |
| 11 `+0x2c` | 0x0065ca00 | `return this->+0xc4` (its own shader) |
| 12 `+0x30` | 0x0065ca10 | `SetWashColour(u32)` → `this->+0xc8` |

### 2.1 Fields, from the ctor 0x0065dad0 **[V]**

```c
+0x20  IDirect3DTexture9 *shadowTex;    // screen-sized, format 0x15 = D3DFMT_A8R8G8B8
+0x28  IDirect3DSurface9 *shadowSurf;   // the render target it is used through
+0x2c  IDirect3DSurface9 *savedRT;      // filled by Begin()
+0xc4  d3dSimpleShader   *washShader;   // ATST=0, LIT=0, SHMD=0, BLMD=4 (PDDI_BLEND_MODULATE)
+0xc8  u32 washColour = 0xff808080;     // pddiColour is ABGR: (128,128,128)
+0xcc.. saved D3D render states (0x65ca20 reads them back with GetRenderState)
+0x158 d3dContext *ctx;
```

The two creations go through the helpers `0x655180(h, w, 1, 1, 0x15, &shadowTex, 1.0f)` and
`0x655300(h, w, 0x15, ..., &shadowSurf, 1.0f)` with `w/h` from `ctx[0x21c]->GetWidth()/
GetHeight()` (`+0x34`/`+0x38`), i.e. **one screen-sized ARGB8 render target**. **[V for the
format and the size, [?] for which helper makes the texture and which the surface]**

### 2.2 `Begin()` — 0x0065cf60 **[V]**

```c
IDirect3DDevice9 *dev = ctx[0x21c][0x10];
dev->GetRenderTarget(0, &savedRT);          // +0x98
dev->SetRenderTarget(0, shadowSurf);        // +0x94
ctx->SetZWrite(false);                      // +0x118
ctx->SetColourWrite(false,false,false,true);// +0x100 -- ALPHA CHANNEL ONLY
dev->Clear(0, nil, D3DCLEAR_TARGET, 0x00000000, 0.0f, 0);   // +0xac
```

### 2.3 `End()` — 0x0065d0f0 **[V]**

```c
SaveRenderStates();                                  // 0x65ca20: GetRenderState(0xf,0x1b,0x19,0x18,...)
shadowTex->GetSurfaceLevel(0, &surf);                // texture +0x48
dev->StretchRect(savedRT, nil, surf, nil, D3DTEXF_LINEAR);   // +0x88   [?] see below
// one screen-space quad, 4 verts, stride 0x18, FVF 0x104 = D3DFVF_XYZRHW|D3DFVF_TEX1:
//   x = -0.5 .. width-0.5, y = -0.5 .. height-0.5   (the D3D9 half-texel offset)
//   u,v = 0,0 .. 1,1
dev->SetRenderState(D3DRS_CULLMODE,          D3DCULL_NONE);
dev->SetRenderState(D3DRS_ZENABLE,           FALSE);
dev->SetRenderState(D3DRS_ALPHATESTENABLE,   FALSE);
dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  TRUE);
dev->SetRenderState(D3DRS_BLENDOP,           D3DBLENDOP_ADD);
dev->SetRenderState(D3DRS_SRCBLEND,          D3DBLEND_ZERO);
dev->SetRenderState(D3DRS_DESTBLEND,         D3DBLEND_SRCALPHA);   //  dest *= src.a
dev->SetTexture(0, shadowTex);
dev->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_SELECTARG1);
dev->SetTextureStageState(0, D3DTSS_COLORARG1,D3DTA_TEXTURE);
dev->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_SELECTARG1);
dev->SetTextureStageState(0, D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
dev->SetSamplerState(0, D3DSAMP_ADDRESSU/V, D3DTADDRESS_CLAMP);
dev->SetSamplerState(0, D3DSAMP_MAGFILTER/MIPFILTER, g[0x7f0adc]/g[0x7f0ae0]);
dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
dev->SetTextureStageState(1, D3DTSS_COLOROP/ALPHAOP, D3DTOP_DISABLE);
dev->SetTexture(1..3, nil);
dev->SetVertexShader(nil); dev->SetFVF(0x104); dev->SetPixelShader(nil);
dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, 0x18);       // +0x14c
ctx->SetColourWrite(true,true,true,false);
dev->SetRenderTarget(0, savedRT);  savedRT->Release();
RestoreRenderStates();
```

**The composite is `frame_rgb *= shadowbuffer_alpha`, fixed function, one triangle strip.** That
is the whole of "how the static shadows get onto the screen" on PC. **[V for every state above]**

Two things I could not pin down and that a re-implementation should treat as open:

1. **The polarity.** Taken literally, `Begin` clears the shadow buffer's alpha to 0 and the decals
   raise it towards 1 (their alpha is coverage, §1.4), while `End` multiplies the frame by that
   alpha — which would darken everything *except* the shadows. So either the `Clear` is meant to
   run under the alpha-only write mask and leave alpha at 1, or the `StretchRect` of the saved
   render target into the shadow texture (which is what the code reads as) seeds the buffer with
   the frame's own alpha channel — the frame-buffer alpha *is* a mask the rest of the frame
   maintains (`notes/displaylist.md` §1 step 3 clears it, almost every pass protects it with
   `SetColourWrite(1,1,1,0)`). I read the surface ping-pong as `savedRT -> shadowTex`, which only
   makes sense with that second reading. **[?]**
2. Whether `washShader` (`+0xc4`, blend `PDDI_BLEND_MODULATE`) and `washColour` (`+0xc8`) are used
   by `End()` at all — the quad above is drawn through raw D3D9 calls and neither appears in it.
   They are almost certainly for the stencil-volume path (§4), where `Display_List` *does* call
   `SetWashColour`. **[?]**

---

## 3. 0x08800008 `renderer::ShadowLoader` → `ShadowRenderable`

Chunk layout and the class layout are in `notes/renderables.md` §6. What follows is the loader
and the two virtuals, reversed for this note.

### 3.1 `ShadowLoader::LoadObject` — 0x00475930 **[V]**

```c
name         = f->GetString();
shadow       = new ShadowRenderable();          // 0x474cf0: typeMask 0x100, doFade = false
shadow->SetName(name);
compName     = f->GetString();
composite    = inventory->Find<CompositeDrawable>(compName);
isBuildingShadow = f->GetU32() != 0;            // +0x84
flagB            = f->GetU32() != 0;            // +0x86   (always 0 in z04)
f0               = f->GetFloat();               // +0x88   (always 0.0)
f1               = f->GetFloat();               // +0x8c   200.0 world / 15.0 NPC / 100.0 car

if (isBuildingShadow) { doDistanceTest = false; SetNumElements(1); }   // flags80 &= ~1
else                  {                         SetNumElements(2); }

bool anyShadowMesh = false;
if (composite) for (ActivePrimitive p : composite->primList) for (elem : p.drawable->elements) {
    DrawablePrimitive *prim = elem.prim;
    if (prim->GetSomeMask() == 0x20) anyShadowMesh = true;          // prim vslot +0x1c
    if (isBuildingShadow) {
        Light *l = GetShadowLight(scene);        // 0x46b4d0(renderMgr->[0x1c]) -> 0x4700d0
        if (l) { prim->+0x4c = 0; prim->+0x5c = l->+0x54; prim->+0x60 = l->+0x58;
                 prim->+0x64 = l->+0x5c; }       // the light DIRECTION, for the extrusion
    }
    prim->+0x68 = isBuildingShadow;
    prim->SetLayer(2);                           // 0x703300  -> display lists 61..64
}
shadow->+0x90 = composite->GetPose();  AddRef    // vslot 13
shadow->+0x94 = composite;             AddRef
float d = anyShadowMesh ? 25.0f : 50.0f;
shadow->SetElement(composite, 0, !isBuildingShadow);
shadow->SetElementDrawDist(0, 0.0f, d, d*0.2f);  // 0x7495ec == 0.2f
if (!isBuildingShadow) { ...element 1 = a built-in blob Geometry, +0xb0/+0xb4 from its bounds... }
*pUID = GetHash(name);
```

Notes worth having:

* **`doDistanceTest` is cleared for building shadows**, so the 25 m band written just above is
  never tested: the composite is submitted from any distance and the *display list* does the
  culling (`IsNodeVisible` per node, plus the 600 m cut in the volume pass, §4). The chunk's
  `f1 = 200.0` is *not* that band either — it is never read by the loader. **[V]**
* every shadow primitive is forced to **layer 2**, the only layer that reaches lists 61..64. The
  layer-2 case of `AddContainerElement` (**0x0045d66f**) is, in full **[V]**:

  ```c
  case 2: {
      DrawablePrimitive *prim = nd->elem->prim;
      prim->+0x48 = 6.0f;                     // SHR's tShadow::SetVolumeLength (shadow.hpp);
                                              // 0 there means "guess from the extruded bbox"
      if (prim->+0x68)                        //  <- isBuildingShadow, written by ShadowLoader
          listID = 61;
      else if (prim->GetSomeMask() == 0x20)   // a ShadowMesh / ShadowSkin primitive
          listID = (owner->flags81 & 1) ? 62 : 63;      // 1 = the owner is inside a room
      else                                    // the car/NPC blob quads
          listID = (owner->flags81 & 1) ? 62 : 64;
  }
  ```

  so **a building shadow goes to list 61**, not 63, and the split is on the *primitive*, never on
  the shader. All three lists are drawn by the same two-pass renderer (§4).
* `GamePlayScene::AddRenderable` additionally calls `0x475760` for a building shadow, which walks
  the scene and re-applies `SetLayer(2)` — belt and braces. **[V]**

### 3.2 `ShadowRenderable::Update` — vslot 9, 0x00474cd0 **[V]**

```c
if (isBuildingShadow && !g_byte[0x007c0b46]) 0x473cc0(this);   // g[0x7c0b46] == 1 -> nothing
```

### 3.3 `ShadowRenderable::Display` — vslot 10, 0x004751c0 **[V]**

```c
if (this->+0x90 == nil) return;                 // no pose -> nothing to draw
if (isBuildingShadow) {
    if (g_byte[0x007c0b46]) Renderable::Display();   // 0x4740f0, the ordinary base path
    else                    this->Hide();            // vslot 14
    isMatrixDirty = false;
    return;
}
// ... 1.3 KB of blob-shadow code: camera, frustum + occluder test (0x460980), the +0xb0/+0xb4
// half sizes, the projected quad. Only cars and NPCs get here.
```

So a **building shadow is an ordinary `Renderable`**: base `Display`, no distance test, one
element (the composite), and its primitives land in display list 63.

---

## 4. The stencil shadow-volume pass (lists 61, 63, 64) — 0x0045a010 **[V]**

```c
if (lists[61].head || lists[63].head || lists[64].head) {
    ext = ctx->GetExtension(0x108);
    ext->SetWashColour(this->shadowVolumeColour);        // +0x30, Display_List+0xc8 = 0xff191919
    if (g_byte[0x007bfa0c]) ext->BeginStencilVolumes();  // +0x24 = 0x65cfd0
    else                    ext->BeginVolumes(true);     // +0x0c = ret 4, empty
    Camera *cam = GetCurrentCamera();
    for (pass = 0; pass < 2; pass++) {
        ctx->SetCullMode(pass == 0 ? 2 : 1);             // +0xf8: front faces, then back faces
        ctx->SetStencilOp(0, 0, pass == 0 ? 3 : 4);      // +0x148: INCR then DECR
        for (nd : lists[61]) { ... }                     // the three lists, in order
    }
    ... lists[63], lists[64] ...
    if (g_byte[0x007bfa0c]) ext->EndStencilVolumes();    // +0x28
    else                    ext->EndVolumes(true);       // +0x14 = ret 4
}
```

with, per node (`edi` = node, `esi` = `node->elem->prim`):

* `if (prim->+0x70 == 0) { Light *l = GetShadowLight(scene); if (l && l->+0xd == 1) { prim->+0x4c = 0;
  prim->+0x5c..+0x64 = l->+0x54..+0x5c; prim->Display(); } }` — the light direction is pushed into
  the primitive immediately before the draw, i.e. **the volume is extruded per frame from the
  current sun**; **[V]**
* a distance cut against `g_float[0x007bfa08]` (**600.0** in the image, and the code lazily
  writes 200.0 into it when it is still unset), measured to the centre of the primitive's
  bounding box scaled by `0x7644ec == 0.5f`; **[V]**
* `prim->+0x48` is used as a per-primitive "already extruded this frame" marker (0.0/1.0).

`ext->BeginStencilVolumes` (0x0065cfd0) is the D3D9 setup: `GetRenderState(...)`, `SetZCompare(5)`,
`SetZWrite(false)`, `+0x120(1)`, `+0x128(1)`, `+0x140(-1)`, `+0x138(-1)`,
`SetColourWrite(0,0,0,0)` and `dev->Clear(0, nil, D3DCLEAR_STENCIL, 0xffffffff, 1.0f, 0)`, i.e.
**colour write completely off and the stencil buffer cleared** before the two volume passes; the
matching end (0x65dac0 → 0x65d6d0) paints the wash. **[V for the states, [?] for the wash quad]**

Also relevant: `Display_List` embeds a `pure3d::ShadowGenerator` at **+0xac** (vtable 0x00737718,
4 slots — this is SHR's `tShadowGenerator`, `libs/pure3d/p3d/shadow.hpp`, with
`Begin/End/SetWashColour/PreRender`), and `RenderProjectedShadows62` (0x0045bce0) uses *it*
instead of the pddi extension when `g_byte[0x7bfa0c]` is set: `SetWashColour(+0xb4, 0xffc8c8c8)`
= (200,200,200), `0x67f4c0(1)`, `0x67f200(+0xb4)`. **[V]**

### What the data behind list 63 is

The 44 `0x08800008` chunks name `0x00023000` composites (`DevilsCay_02_shadow` →
`dcy02shd_mesh_1..9` plus eight unnamed elements) whose drawables are **`0x0001001a`
`pure3d::ShadowMesh`** chunks: a `0x00010005` `POSITIONLIST` plus a `0x0001001b` `TOPOLOGY`
(edge/face adjacency) and a `0x00122000` sort key. That is SHR's `tShadowMesh`
(`libs/pure3d/p3d/shadow.cpp`, `shadow/shadow_common.cpp`): a closed shell whose silhouette is
found per frame against the light and extruded into a shadow volume. There is no texture and no
"shadow map" anywhere in this path.

---

## 5. Why the decals cut hard — measured **[V]**

Scan of all 220 `assets/packages/z04/*.p3d` for prim groups whose shader name contains
`shadowdecal` (`re/` scratch script, geometry names are `CollapsedMesh_<n>_<worldgeo>`):

```
  256  RegionShader_r_tree_shadow.tga_shadowdecal_blmd1_mcbv0_lit0_alpha1_atst0_alum0_2sid0
   72  RegionShader_r_tree_shadow2.tga_shadowdecal_...
   49  LocalShader_r_tree_shadow.tga_shadowdecal_...
   34  RegionShader_r_sb_tree_shadow.tga_shadowdecal_...
   27  LocalShader_l_lh_shadow_decal.tga_shadowdecal_...
   21  RegionShader_r_nb_tree_shadow.tga_shadowdecal_...
   20  GlobalShader_g_i_tree_shadow.tga_shadowdecal_...
   12  LocalShader_shadowdecal.TGA_shadowdecal_...
    6  LocalShader_r_nb_tree_shadow.tga_shadowdecal_...
    3  LocalShader_lmp_table_001.tga_shadowdecal_...
    3  LocalShader_r_lt_tree_shadow_01.TGA_shadowdecal_...

408 geometries carry one, in 47 of 220 packages:
  397 of them are  details_*   world geo
   11 of them are  shells_*    world geo
```

Consequences, in the order they matter:

1. **The 120 m details band.** `WorldGeoRenderable::Display` (notes/renderspine.md §4.5) gives
   `details_*`/`cbvlitdecals_*` geo `maxD = g[0x7c0a48] = 120.0f` with a `fadeBand` of 20 m,
   `shells_*` 1500 m and `skyline_*` 3000 m. 97 % of the shadow decals are therefore visible only
   within **120 m of the camera** and they disappear one whole details mesh at a time. On a flat
   road that boundary is a circle of black blobs that ends in mid-air — it is by far the most
   visible LOD pop in the game's data, much more visible than the building details that fade with
   them, because a hard-edged dark blob against a light road has the highest contrast in the
   scene.
2. **The 20 m fade is not free.** Retail cross-fades those 20 m (the node moves from list 7 to
   list 8 and `RenderShadowDecals_7_8_77` pushes `container->GetFadeAmount()` into the primitive,
   §1.2). The viewer had **no fade at all**: `PrimGroup::Display` was
   `if (mFade >= 1.0f) return;` with a `TODO`, so a fading primitive was drawn at full strength
   until it vanished. That turned retail's 20 m cross-fade into a one-frame pop — this is the
   second half of the "hard cuts", and it is the part that was a viewer bug rather than data.
3. **Coverage is sparse.** 173 of the 220 packages have no shadow decals at all, and inside a
   package only a few details meshes carry one, so whole blocks of a region have shadows and the
   next block has none. Nothing can fix that; it is how the map was authored.
4. Not a cause: the blend. `blmd1` + a black texture with alpha = coverage is exactly what the
   retail mask arithmetic amounts to for a single decal (§0), and the decal geometry is authored
   far enough above the ground that it does not z-fight.
5. Ordering *is* a real second-order problem for the plain-decal approximation: the pass runs in
   group 10, before the lit world geo (list 49) and before all the fading buckets, and with
   z-write off but z-test on a later coplanar ground polygon can paint over a decal. Retail does
   not care because it accumulates a mask instead of painting pixels. In practice the ground under
   the decals is in lists 52/53/13/15/21/27/28 (groups 3, 4, 7), all of which are drawn *before*
   group 10, so this does not bite in Little Havana or North Beach.

---

## 6. What the viewer implements (2026-09-17)

`renderer/shadow.{h,cpp}`: `renderer::ShadowLoader` + `renderer::ShadowRenderable` per §3 —
the chunk, the composite, layer 2 and `isBuildingShadow` on every shadow primitive,
`doDistanceTest = false` for building shadows, element 0 = the composite, `Display` = the base
`Renderable::Display` gated on `g_buildingShadowsEnabled` (retail `g_byte[0x7c0b46]`, = 1), and
the layer-2 case of `AddContainerElement` fixed to retail's three-way split (61 for a building
shadow). With no `ShadowMeshLoader` (chunk 0x0001001a is not loaded) the composites resolve to
nothing — 0 of the 55 drawables of `havana_02_shadow`, and nothing at all reaches lists 61..64 —
so **the building shadows of §4 are loaded but not drawn**. Drawing them needs, in order: a
`ShadowMesh` loader (`0x00010005` + `0x0001001b`), a silhouette+extrusion step per frame from
`LightManager`'s sun with the 6 m volume length, and the two-pass stencil + wash of §4. None of
that exists yet.

The shadow decals of §1 *are* drawn: lists 7/8/77 are on by default,
`Display_List::RenderShadowDecals_7_8_77` follows §1.2 (z-write off, cull 7/8, container fade on
8/77), and the retail alpha-mask + full-screen multiply of §2 is approximated by letting the
decals blend straight onto the ground with their own alpha, which is arithmetically the same for
a single decal layer. The per-primitive fade of §5.2 is implemented for the shadow decal shader
(`pddiShader::SetFade`, `gl/glshader.cpp`), so the 20 m band at 120 m cross-fades like retail
instead of popping.
