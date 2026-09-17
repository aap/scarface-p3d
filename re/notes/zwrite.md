# Depth writes and alpha punch-through (PC)

Where the depth buffer is written on PC, which blended geometry is exempt, and the two
things in the viewer that got it wrong. Addresses are unpacked-image VAs (`re/dis.sh`).
**[V]** = read out of the disassembly, **[?]** = inferred.

---

## 1. There is exactly one per-draw writer of `D3DRS_ZWRITEENABLE` **[V]**

`pure3d::d3dContext::SetZWrite` — `0x0064b990`, `pddiRenderContext` vtable slot **`+0x118`**
(vtable `0x00767a3c`, so `0x00767b54` = `0x64b990`; the same table's `+0x3c..+0x64` are the
matrix slots `notes/displaylist.md §0` identifies, which pins the numbering):

```asm
0064b990  push ebx ; mov ebx,[esp+8] ; call 0x659bf0        ; base: cache the flag
0064b99e  mov esi,[esi+0x230] ; movzx ecx,bl
0064b9ac  push 0xe ; call [eax+0xe4]                        ; D3DRS_ZWRITEENABLE = 14
```

A regex sweep of the whole image for `push 0xe` immediately followed by a
`call dword [reg+0xe4]` (the generic `SetRenderState`) finds **three** sites: `0x64b9aa`
(this function), `0x65c157` (inside `d3dSimpleShader::SetPass`, but there `0xe` is the
*value* `D3DBLEND_BLENDFACTOR` for state `0xcf` `D3DRS_SRCBLENDALPHA`, not a z-write) and
`0x6a06cb` (a whole-state restore block, not per draw). So **z-write is display-list state
and shader state, nothing else touches it**, and in particular:

* `d3dSimpleShader::SetPass` (`0x65be70`) — the shader every world-geo and foliage
  primitive uses — **never writes z-write**. `notes/shaderstate.md` is right that the
  alpha test comes only from the shader's own `ATST`; the depth state does not come from
  the shader at all.
* the `ZWRT` / `ZTST` shader params **do not exist in the PC executable**. The
  `d3dSimpleShader` parameter descriptor table (`0x7f0700 .. 0x7f08fc`, 24-byte entries:
  `ACTH FADE UVMD FIMD SHMD LIT BLMD ATST ACMP 2SID MCBV ALUM ECMD SCMD TEX`) has no
  entry for either, and the four-CCs `ZWRT`/`ZTST` appear nowhere in the image. They *are*
  in the data — 122 `LocalShader__simple_blmd0_mcbv0_lit0_alpha0_atst0_alum0_2sid0`
  shaders in 119 `art/ecoprops/*_ecoprops.p3d` files carry `ZWRT 0` and `ZTST 0` as
  `0x11003` int params — and retail **drops them on the floor**. The viewer's
  `SetIntDummy` for `PDDI_SP_ZWRITE` / `PDDI_SP_ZTEST` in `gl/glshader.cpp` is therefore
  correct as it stands; honouring them would be *less* faithful, not more.

### 1.1 The two shaders that do own their z-write **[V]**

Slot `+0x34` / `+0x38` / `+0x3c` of a `pddiBaseShader` vtable are `PreRender` / `PostRender`
/ `SetPass` (`d3dShader`'s table `0x767c84` has nullsub, nullsub, `_purecall`, and SHR's
`libs/pure3d/pddi/base/baseshader.hpp` declares them in that order).

| shader | vtable | `PostRender` `+0x38` | `SetPass` `+0x3c` |
|---|---|---|---|
| `d3dShadowDecalShader` | `0x774e1c` | `0x7090f0`: `ctx->SetZWrite(true)` | `0x7091b0`, tail-jumps `ctx->SetZWrite(false)` at `0x709342` |
| `d3dDecalShader` | `0x774ea4` | `0x709430` → `0x65bc10` (no depth state) | `0x709380`, no depth state |

So the **shadow decal** shader really does bracket its own draws with z-write off/on —
aap's `glShadowDecalShader::PreRender`/`PostRender` was right, only the `// TODO: these are
wrong` comment was wrong — while the **decal** shader does not, and the viewer's
`glDecalShader::PreRender`/`PostRender` `glDepthMask` pair was an invention. (Retail's
decal alpha test, for the record, is `D3DRS_ALPHATESTENABLE = 1`,
`ALPHAFUNC = table[0x7ec920] = D3DCMP_NOTEQUAL`, `ALPHAREF = 0` at `0x7093a0..0x7093e9`,
only when `blendMode == PDDI_BLEND_ALPHA`; the viewer's `GREATER 0` is equivalent for
alpha in `[0,1]`. It also forces `D3DRS_FOGENABLE = 0`.)

---

## 2. Which lists retail draws with z-write off **[V]**

`Display_List::Render` (`0x45e680`) calls slot `+0x118` at these sites; the argument is the
`push 0`/`push 1` right before it:

| off | brackets | lists |
|---|---|---|
| `0x45e994` / `0x45e9ab` | around `0x45b590` `RenderDecals_3_17_18_4_75` | 3, 17, 18, 4, 75 |
| `0x45eafb` / `0x45eb12` | around `0x45b870` `RenderDecalsFading_5_19_20_6` | 5, 19, 20, 6 |
| `0x45eb5c` / `0x45eb78` | around `0x45db80` `RenderNightLights11` | 11 |
| `0x45ec26` / `0x45ec3d` | around `0x45da30` `RenderCardsNight12` | 12 |
| `0x45ee03` / `0x45ee30` | the *indoors* copy of the decal group | 3, 17, 18, 4, 75 |
| `0x45eee0` / `0x45eef7` | around `0x45c590` `RenderAdditive_39_30_24` | 39, 30, 24 |
| `0x45ef2f` / `0x45ef4c` | around `renderMgr->[0x38]->vslot11()` (the tracer pool) | — |

plus, inside the list renderers themselves:

| function | lists | what |
|---|---|---|
| `0x459a00` `RenderSky` | 46, 47 | `SetZWrite(false)` `0x459a54`, restored `0x459bc0` (z-test off too) |
| `0x45caf0` `RenderShadowDecals_7_8_77` | 7, 8, 77 | `0x45cb07` / `0x45ccc1` |
| `0x45dcf0` `RenderEnv_9_10` | 9, 10 | `0x45dd16`, restored by the tail-jump `0x45de9d` |

**Everything else — including 37, 38, 41, 45, 29, 50, 21..28, 33, 34, 51..56, 65, 66, 72,
73, 74 — is drawn with z-write ON.** `renderer/display_list.cpp` already matched this list
for list and needed no change.

Sorting for the blended buckets is also already right. `SortGroup_37_38` (`0x4594b0`) **[V]**:

```asm
004594b3  cmp byte [esi+0x61], 0          ; listDirty[0x3c + 37]
004594b9  push 0x25 ; call 0x459120       ; ComputeDepthKeys(37)
004594c3  push 0x458ff0                   ; CmpKeyThenDepth
004594c8  add ecx, 0x1bc                  ; &lists[37]   (37*12)
004594ce  call 0x6e8a20                   ; LinkedList::Sort
004594d7  cmp byte [esi+0x62], 0          ; listDirty[0x3c + 38]
004594dd  push 0x26 ; call 0x459120       ; ComputeDepthKeys(38)
004594e9  push 0x458ff0 ; add ecx,0x1c8 ; call 0x6e8a20
```

i.e. view-space depth keys and then `CmpKeyThenDepth`, which is descending `sortKey2` =
**far to near**, exactly what `SORTZ(37); SORTZ(38)` in the viewer does.

---

## 3. The invisible box at the boathouse: the low-LOD hull in list 2 **[V]**

At Tony's mansion (`boathouse_01_detail.p3d`, native bbox
`x -2242.8..-2187.2, y 0.5..19.3, z -1806.4..-1739.2`) every tree on the hillside behind the
boathouse was reduced to a bare trunk: the crowns were gone, and so were the hedges and the
mangroves, in a big volume that followed the shape of the island. `P3D_HIDELIST=2` brought
all of it back.

**List 2** is `layer 33` with `shaderType == VERTEXFADE` (`notes/renderspine.md §3.3`), i.e.
the world geo whose kind is `LOW_LOD` (`renderer/worldgeo.cpp`) drawn with the `vertexfade`
pddi shader — the coarse island/city hull in `assets/packages/z04/islands_LOD.p3d`
(`pure3dVertexFadeShader1`, `islands_LOD_pure3dVertexFadeShader1`, `RockShader`, all
`BLMD 1 LIT 1`, bbox of one node alone `-8046..-1460 x`, `-4162..-1376 z`). Retail draws it
at step 13 of `Render()` (`0x45d1f0` `RenderVertexFade2`) with **z-write on**, and the vertex
shader fades it out *towards* the camera, so from close up it is a completely transparent
hull sitting over the whole island.

The thing that stops it filling the depth buffer is an **alpha test the shader turns on
unconditionally**. `d3dVertexFadeShader::SetPass` (`0x709a30`):

```asm
00709bb8  call d3dState::SetAlphaBlend       ; table_7ec848[1] = enable, ADD, SRCALPHA, INVSRCALPHA
00709bc4  cmp byte [edi+0x5c], 1             ; cached D3DRS_ALPHATESTENABLE
00709bcf  push 0xf ; push 1                  ; D3DRS_ALPHATESTENABLE = TRUE
00709bf0  mov ecx, [0x7ec918]                ; = 7 = D3DCMP_GREATEREQUAL
00709bfe  push 0x19 ; ...                    ; D3DRS_ALPHAFUNC
00709c17  push 0x14 ; push 0x18              ; D3DRS_ALPHAREF = 20
00709c2c  ... ctx->SetVertexShaderConstant(0x4a, {200.0, 250.0, 0.1, 1/k})
```

(`0x7ec904` is the pddi→D3D compare table: `NONE→NEVER, ALWAYS→8, LESS→2, LESSEQUAL→4,
GREATER→5, GREATEREQUAL→7, EQUAL→3, NOTEQUAL→6`, which is exactly the `pddiCompareMode`
order in `pddi.h`. The `200.0 / 250.0` are the fade band the viewer already had.)

So: retail's low-LOD hull is **blended, z-write on, and alpha-tested at GREATEREQUAL
20/255** — below 8% alpha the pixel is discarded and never reaches the depth test.
`gl/glshader.cpp`'s `glVertexFadeShader::SetPass` set the vertex fade but no alpha test, so
the invisible hull wrote depth over the whole island and everything drawn after list 2
vanished inside it: the eco-prop foliage (list 38, drawn at step 23), the hedges, the
mangroves, the fading world (51/54/55/56), the water (65/66). Fixed by adding retail's
alpha test.

---

## 4. Foliage: retail draws the blended pass with z-write OFF **[V]**

The leaves use `..._simple_blmd1_..._atst0_alum1_2sid1` — alpha blend, **no** alpha test —
and `d3dSimpleShader::SetPass` neither adds a test nor touches z-write, so on the face of
it retail would punch the whole leaf quad into the depth buffer. It does not, because the
instanced eco props never go through a plain list walk.

`InstancePrimitive` takes **layer 40/41/42** (`0x46f2e0`, `notes/renderspine.md §5.2`) →
lists **72/73/74**, and those list renderers hand the work to the pddi instancing extension
(`GetExtension(0x200200)`, `d3dExtInstancing`, vtable `0x767944`):

```
0x45a4c0 RenderInstanced72:
    instExt->[+0x04]()            ; 0x649ad0: g[0x830a18] = 1   "instancing on"
    ext10b->[+0xd4](1)
    ext10b->[+0x6c](0)            ; 0x65ed60: g[0x830a30] = 1, g[0x830a29] = 1
      per node: 0x46f030 PreDisplay, prim->Display(), 0x46f0f0
    ext10b->[+0x70](0)
    ext10b->[+0x74]()             ; 0x65ee00: g[0x830a30] = 0
      per node: the same again
    ext10b->[+0x78]()
    ext10b->[+0xd4](0) ; instExt->[+0x08]()   ; 0x649af0: g[0x830a18] = 0
```

(`ext10b` is `GetExtension(0x10b)`, vtable base **`0x768874`** — the complete-object-locator
pointer sits at `0x768870` — which is what fixes the `+0x64 .. +0xd4` numbering in
`notes/displaylist.md §4`. `+0x7c` = `0x65ed90` and `+0x84` = `0x65ee00` are list 73's pair.)

`prim->Display()` ends in `d3dExtInstancing`'s **`[+0x10]` = `0x650810`**, and that function
draws every instanced shape **twice** whenever `g[0x830a30]` is set (`0x6508d2`, and the
identical loop at `0x650aa6` for the one-shape case):

```c
for (int pass = 1; pass >= 0; pass--) {
    if (pass != 0) {                          // ---- the COLOUR pass
        ctx->SetZWrite(false);                //      0x650901
        ctx->SetColourWrite(1,1,1,0);         //      0x65090f, alpha masked out
    } else {                                  // ---- the DEPTH pass
        g_byte_830a31 = 1;                    //      0x6508f2
        ctx->SetColourWrite(0,0,0,1);         //      alpha only
    }
    DrawShape(LODShape);  DrawShape(InstanceShape);      // 0x64d3f0
    if (pass != 0) ctx->SetZWrite(true);      //      0x650982
    else           g_byte_830a31 = 0;         //      0x650976
    ctx->SetColourWrite(1,1,1,1);
}
```

and `g[0x830a31]` is the flag `d3dSimpleShader::SetPass` reads at `0x65c043`:

```c
if (g_byte_830a18 && shader->blendMode == 1 /*ALPHA*/ && shader->isLit /*+0x48*/ == 0
                  && g_byte_830a31 && g_byte_830a30) {
    SetAlphaTest (1, 5 /*GREATEREQUAL*/, (int)f[0x7f0908]);      // 0x65c072..0x65c0a0, ref = 228
    SetAlphaBlend(1, 1, 0xb, 0);                                 // 0x65c0af
    SetBlendFactor(replicate((int)(instFade*255)));              // 0x65c0de
    /* and skip the 0x830a41 separate-alpha-blend path */
}
```

(`+0x48` is `isLit`: it is the first argument `d3dState::SetMaterial` is called with in
every `SetPass` — `0x65bfaf`, `0x7092fa`, `0x709a6e` — and `d3dVertexFadeShader::ctor`
writes `1` into it at `0x7099c9`, matching its `LIT 1` shaders.)

**Conclusion — aap's guess is right.** Retail's foliage colour is drawn **blended with
z-write off**; the depth it contributes comes from a second pass that draws *the same
geometry alpha-tested at GREATEREQUAL 228/255* (`f[0x7f0908] = 228.0`) and writes no colour
but the alpha mask. The transparent 90% of a leaf quad therefore never enters the depth
buffer, while the near-opaque core still occludes properly.

### 4.1 What the viewer did, and what it does now

The viewer has no instancing extension, so `renderer/instance.cpp`'s `SetInstanceLayers`
deliberately runs the instance prims through `SetPrimLayerByShader` and sorts them like
world geo. The blended leaves are `simple` + `BLMD 1` + `lit0` + ALUM → **layer 8, `isALUM`
→ list 38**; in a tree-heavy zone list 38 is 1303 of 1312 nodes `<model>InstanceShape` /
`<model>LODShape` (the other nine are world geo), and `RenderUnlit_38_37` drew it once with
z-write on. Each crown is a single draw call of many leaf quads in buffer order, so the
transparent part of an early quad z-rejected every quad behind it *inside the same tree* —
which is the "leaves cutting holes into each other" that made every tree look half-empty.

`Display_List::RenderUnlit_38_37` now reproduces retail's two passes on list 38: pass A
with `SetZWrite(false)`, pass B with `SetColourWrite(false,false,false,false)` (we do not
use the frame buffer's alpha as a mask, so it is a pure depth fill) and
`pddiInstancedDepthPass = true`, which is the viewer's `g[0x830a30] && g[0x830a31]` and
makes `glShader::SetPass` swap the blend for `GREATEREQUAL 228/255`, exactly as
`0x65c072` does.

### 4.2 Residual, and why

* **The nine world-geo prims in list 38 get the two passes too.** Retail would draw them
  once with z-write on. Separating them would mean giving the instance prims their own
  layers (40..42), i.e. undoing the documented deviation and moving the foliage from step
  23 of the frame to steps 3/5/15 — and lists 72..74 are never sorted, so without the
  instancing extension's own LOD cross-fade the leaves would blend in submit order.
* **`tiLightAInstanceShape`** (the lamp glows) is `lit1`, so it is layer 12 → **list 29**,
  drawn single-pass with z-write on. Retail's alpha-test override explicitly requires
  `!isLit` (`0x65c059`), so retail would not alpha-test it either, but retail *would* still
  draw it inside the extension's z-write-off colour pass. 33 quads at the mansion.
* **Soft leaf edges still write no depth at all** (alpha < 228/255 fails pass B), so the
  water (65/66) and the additive lights (39/30/24), which are drawn after list 38, blend
  through the fringe of a crown instead of being hidden by it. That is retail behaviour,
  not a viewer bug.
* **Within one crown, pass A now composites in buffer order**, because it has no z-write
  and `CmpKeyThenDepth` only orders whole nodes. Retail is the same (its pass A is
  per-`InstancePrimitive`, and the quads inside one packet are unsorted).
* The low-LOD hull still writes depth where its alpha is between 20/255 and 1, i.e. in the
  outer part of the 200..250 m fade band. Also retail.
