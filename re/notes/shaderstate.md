# pure3d::d3dSimpleShader state (PC) — alpha test / blend, and why foliage punches through

Verified from the unpacked image, 2026-09-15.

## Defaults (d3dShader::ctor 0x6524f0, d3dSimpleShader::ctor 0x65bd30)
```
+0x18 u32   blendMode          (PDDI_BLEND_*; SetBlendMode 0x652650 stores it, then calls 0x6525c0)
+0x1c u32   effectiveBlendMode (computed by 0x6525c0: blendMode, or ALPHA(1) when blendMode==NONE && fade>0)
+0x28 u8    alphaTest          = 0           (SetAlphaTest 0x6524c0, PDDI_SP_ALPHATEST "ATST")
+0x2c u32   alphaCompare       = 5 = PDDI_COMPARE_GREATEREQUAL (SetAlphaCompare 0x447ff0)
+0x30 f32   alphaThreshold     = 0.5         (SimpleShader::SetAlphaThreshold 0x652660, clamped 0..1)
+0x34 f32   effectiveThreshold = threshold * (255-fade)/255, clamped (recomputed by 0x6525c0)
+0x38 u32   fade/transparency  0..255 (0 = opaque); 0x6525c0 also copies +0x64/68/6c/70 -> +0x50/54/58/5c (colours)
+0x48 u8    ? (checked with blendMode==1 in the "blendFactor" path)
+0x7c u8    "alpha" param? (with alphaTest it enables D3DRS_SEPARATEALPHABLENDENABLE 0xce / BLENDOPALPHA 0xd1 /
            SRCBLENDALPHA 0xcf / DESTBLENDALPHA 0xd0 at 0x65c0f0..0x65c1b5 — alpha channel output for later passes)
```
SHR (libs/pure3d/pddi/dx8/shaders/shader.cpp) has the same defaults: alphaTest=false, GREATEREQUAL, ref 0.5.

## SetPass 0x65be70 — blend/alpha-test application
```
if (fade > 0 && blendMode == NONE) {                       // faded-out opaque object
    SetAlphaBlend(1, 1, 0xb, 0xc);                          // (enable, op, src, dst) — fade blend
    SetAlphaTest(alphaTest, alphaCompare, (int)(effThreshold*255));
} else if (g_byte_830a35) {                                 // global override (some special pass)
    SetAlphaBlend(0, 1, 1, 1);
    SetAlphaTest(0, GREATEREQUAL, 0x80);
} else {
    0x65b4a0: SetAlphaBlend(table_7ec848[effectiveBlendMode]);   // 16-byte entries {u8 enable, u32 op, u32 src, u32 dst}
              SetAlphaTest(alphaTest, alphaCompare, (int)(effThreshold*255));
}
```
So alpha test comes ONLY from the shader's own ATST param. The foliage shaders are
`..._simple_blmd1_..._alpha1_atst0_alum1_2sid1`: alpha BLEND, no alpha test. Retail therefore
draws leaves blended without a test, and avoids punch-through by how the Display_List draws the
blended lists (depth write / ordering) — see notes/renderspine.md. SHR did the same: tDisplayList
sorts translucent drawables by camera-space z ("for rendering translucent objects last",
p3d/displaylist.cpp) and StaticEntityDSG wraps translucent draws in SetZWrite(false).

Shader-name convention (LocalShader_<tex>_<pddi shader>_blmdN_mcbvN_litN_alphaN_atstN_alumN_2sidN):
blmd = blend mode, mcbv = multi colour-by-vertex, lit = lighting, alpha = ? (+0x7c), atst = alpha test,
alum = ALUM flag, 2sid = two sided.
