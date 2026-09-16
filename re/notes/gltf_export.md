# `re/p3d2gltf.py` — Scarface (PC) map → glTF 2.0

A standalone Python exporter that turns the `assets/packages/z04/*.p3d` packages into a
single `.glb` (or `.gltf` + `.bin`) that Blender, Godot, Unreal, three.js … can open.
It reuses the chunk walker of `re/p3dwalk.py` and the placement decoder of `re/instloc.py`
and needs nothing but `numpy`.

```
python3 re/p3d2gltf.py --out world.glb [--region sbeachn | --files a.p3d b.p3d | --all]
                       [--no-instances] [--lod] [--flip-x] [--no-support] [-v]
```

| flag | meaning |
|---|---|
| `--region <name>` | every `<name>_*.p3d` plus the `<prefix>_region*.p3d` whose prefix `<name>` starts with (so `sbeachn` also pulls `sbeach_region*.p3d`) |
| `--files …` | explicit package list (bare names are resolved in `--pkgdir`) |
| `--all` | every `.p3d` in `--pkgdir` (default `assets/packages/z04`) |
| `--no-instances` | skip the eco-prop / `instanceobject` placements |
| `--lod` | also export the type 3 `low_LOD_` world geos *and* the city-wide backdrop in `miami_lod.p3d` / `islands_LOD.p3d` |
| `--flip-x` | negate world X and reverse the winding, so the map is **not** mirrored — see *Handedness* |
| `--no-support` | do not auto-load the shared shader/model libraries |

Support packages (`Common.p3d`, `*_region*.p3d`, `*_LOD*.p3d`, `miami_lod*.p3d`, 21 MB in
total) are always loaded because the `GlobalShader_…` / `RegionShader_…` shaders, their
textures and all `…InstanceShape` / `…LODShape` meshes live there. Their *own* world geos
and placements are not exported (they would duplicate across regions).

---

## 1. What is exported

* **World geometry.** One glTF node per package → one node per
  `renderer::WorldGeoRenderable` (`0x08800003`) → one node per element of its
  `pure3d::CompositeDrawable` (`0x00123000`), referencing a shared glTF mesh.
  One glTF mesh per `pure3d::Geometry` (`0x00010000`), one primitive per prim group
  (`0x00010020`). Composite elements are placed with the **world matrix of the joint**
  the element names (`0x00023000`/`0x00023001` skeleton, `world[i] = local[i]·world[parent]`
  as in `skeleton.cpp`); in practice the map skeletons are identity, but characters/props
  are not, so it is honoured.
  `extras` on the world geo node: `worldGeoType`, `worldGeoTypeName`, `composite`,
  `package`, `flags` (`isDetails` / `isSkyline` / `drawFirst` / `isLowLOD`, from the name
  prefix tests of the retail loader) and, when the zone package
  (`0x08800004`/`0x08800009`) has an entry, `drawDistMin`, `drawDistMax`, `drawDistFade`
  and `otherPosition`.
* **Instance placements.** One node per `modelname`, holding one node per `0x09900194`
  record with the placement matrix; each placement node has `<model>InstanceShape` and
  `<model>LODShape` as children so the meshes are shared — Blender turns this into linked
  duplicates (one mesh datablock, thousands of objects).
  Both shapes are exported because the `InstanceShape` is only the wind-swayed crown and
  the whole tree (trunk included) is in the `LODShape` — retail draws both from distance 0
  (`re/notes/renderspine.md` §5.4, `re/notes/instances.md` §6.3).
  `extras`: `model`, `tint`, `alpha`, `state` (resolved through the state-name table),
  `object` (the script object name) and `package`.
* **Materials.** One per `pure3d::Shader` (`0x00011000`), named after the shader, with the
  base texture's PNG embedded in the buffer as an `image/png` buffer view. Textures and
  images are shared, never duplicated. `doubleSided` from `2SID`, `alphaMode` from
  `BLMD` / `ATST` / the header's `hasTranslucency` (`BLEND` for `blmd1`, `MASK` +
  `alphaCutoff` from `ACTH` for `atst1`, else `OPAQUE`), `metallicFactor = 0`,
  `roughnessFactor = 1`. The complete parameter list (`LIT`, `ALUM`, `BLMD`, `MCBV`,
  `TCI`, colours …) is kept verbatim in `extras.params`, plus `pddiShader`
  (`simple`, `cbvlit`, `layered`, `decal`, …).

## 2. What is **not** exported

* No lights: neither the `0x08800007` `LightingRenderable` groups nor the baked
  sun/ambient. The exported vertex colours are the raw baked CBV data; the engine's
  vertex shader renders `lighting * 2 * colour.bgr`, so in Blender everything looks about
  half as bright and much less warm than in `p3dview` (whose `glShader` also adds
  ambient (51,43,27) and light (97,95,70)). Rebuilding that tint is a job for the bake.
* No sky (`0x08800002` `SkyRenderable`), no water surfaces (`0x08800200`…`0x08800203`),
  no shadows (`0x08800008`), no occluders (`0x0880000a`).
* No state props: the `pure3d::prop::StatePropData` (`0x0802000d`) family and the
  `0x08800005` `StatePropRenderable`s are ignored; only the two instancing meshes of a
  model are exported. Damage states / open-close states therefore do not come across.
* No characters or vehicles (`0x08800000` / `0x08800001`), no skinning: `0x00010001`
  skins, matrix palettes and weight lists are skipped, only `0x00010000` meshes are read.
* No collision (`0x0701xxxx`), no traffic data, no scripts, no animation.
* Second colour sets: `0x0001001c` MULTICOLOURLIST channel 0 is used as `COLOR_0` when
  there is no plain `0x00010008` list; further channels are dropped. Tangents/binormals
  and the third-and-later UV set are dropped. Point/line prim groups are dropped.

## 3. Conventions

### Coordinates and handedness — **the export is mirrored by default**

Pure3D is Y-up and so is glTF, so the exporter writes positions verbatim, *as instructed*:
a vertex at `(513, 40, -1697)` in the game is at `(513, 40, -1697)` in the `.glb`, which
makes it trivial to cross-check against `instloc.py`, `p3dview` and the notes.

But the native data is **left handed**. Verified visually: export `sbeachs`, put the
camera on the normal of a `signBusStop` and the sign reads `SUB`, `EMERGENCY CALLS` comes
out backwards. That is also why `p3dview` multiplies the world by `diag(-1,1,1)` and why
`glEnable(GL_CULL_FACE)` is commented out in `p3dview.cpp` — the flip inverts the winding.

So:

* **default (no flag):** native coordinates, world is a mirror image, text reads
  backwards. Triangle winding is CCW-front and agrees with the vertex normals
  (measured: 16033 triangles agree vs 26 that don't in `sbeachn_01_shell`), so nothing is
  inside-out and Blender shades it correctly — it is only mirrored.
* **`--flip-x`:** X is negated on positions, normals, the placement matrices and the joint
  matrices (`M' = S·M·S` with `S = diag(-1,1,1,1)`), and the triangle winding is reversed
  to compensate. The result is right handed and reads correctly — this is what the game
  shows and what you want for VR. Cost: world X is the negative of the game's X.

Use `--flip-x` for anything you are going to look at or walk around in; use the default
when you need coordinates that match the packages and the RE notes.

Blender's glTF importer converts Y-up to Z-up, i.e. a glTF point `(x,y,z)` lands at
Blender `(x, -z, y)`.

### UVs

The retail shader (and `gl/shaders/shader.frag`) samples at `vec2(u, -v)`, so the exporter
writes `TEXCOORD_n = (u, -v)`. Negated, not `1-v`: with `REPEAT` wrapping the two agree
modulo 1 but `-v` is exactly what the engine does. Verified — textures line up.
UV channels are renumbered densely (`TEXCOORD_0`, `TEXCOORD_1`); at most 2 sets are kept.
81 of ~7200 prim groups in the beach packages have a second set.

### Vertex colours

`COLOR_0` is `VEC4` unsigned byte, `normalized: true`. The bytes in the file are
**B, G, R, A**: SHR's `pddiColour` is `c = b | g<<8 | r<<16 | a<<24` (a D3DCOLOR,
`libs/pure3d/pddi/pdditype.hpp`), and `gl/shaders/shader.vert` compensates with
`in_color.bgr`. The repo's own `pddi.h` has the constructor the other way round — that is
the bug the `.bgr` swizzle papers over. Statistically confirmed too: over 146k vertices in
`sbeachs` the mean bytes are (156, 165, 172, 243) — warm sunlight under BGRA, cold blue
under RGBA.

### Other file-format details worth recording

Cross-checked against `geometry.cpp` / `primgroup.cpp` / `shader.cpp` / `texture.cpp` /
`compositedrawable.cpp` / `skeleton.cpp` and the real bytes; a few things differ from what
one would guess:

```
pstring          u8 len; char[len]   -- len INCLUDES the NUL and the padding that makes
                                       1+len a multiple of 4.
0x00010020 PrimGroup
                 u32 version; pstring shader; u32 primType, vertexFormat, numVerts,
                 numIndices, numMatrices, unk1, unk2, unk3, unk4    <- FOUR extra u32s
0x00010007 UVLIST      u32 n; u32 channel; f32[2n]                  <- has a channel index
0x0001001c MULTICOLOUR u32 n; u32 channel; u32[n]
0x00010008 COLOURLIST  u32 n; u32[n]                                (no channel)
0x0001000a INDEXLIST   u32 n; u32[n]                                (32 bit indices)
0x00019000 Texture     pstring name; u32 version,w,h,bpp,alphaDepth,numMips,type,usage
0x00019001 Image       pstring name; u32 version,w,h,bpp,palettized,hasAlpha,format(1=PNG)
0x00123001 CompositeElement  u32 unk; u32 makeCopy; pstring child; u32 type; i32 jointId
0x00023001 Joint       pstring name; u32 parent; f32 m[16]
```
`primType` is 0 (trilist) for 7142 of 7174 prim groups in the beach packages and 1
(tristrip) for 32; strips are converted to lists, dropping the degenerate stitching
triangles. Degenerate triangles in plain lists are dropped too.

Name lookup is `core::GetHash` (retail `0x006dc190`) throughout, so everything resolves
case-insensitively exactly like the engine's inventory (`modelname` `"dumpstera"` finds
the asset `dumpsterA`), and the first package to define a name wins.

## 4. Sizes and timings

Measured on this machine (python 3.13, numpy 2.5, Blender 5.0), packages are `mmap`ed and
the binary chunk is streamed to a temp file, so nothing is held twice.

| run | packages | world geos | placements | meshes / prims / tris | materials / textures | load+export | `.glb` |
|---|---|---|---|---|---|---|---|
| `--region sbeachn` | 13 + 34 support | 38 | 6756 (96 models) | 1103 / 4645 / 199 676 | 394 / 197 | 0.2 s + 0.5 s | 23.1 MB |
| `--region sbeachs` | 10 + 32 support | 29 | 4370 (73 models) | 552 / 2471 / 158 941 | 331 / 168 | 0.1 s + 0.3 s | 16.8 MB |
| `--all` | 220 | 397 | 45 698 (426 models) | 5614 / 24 639 / 1 983 844 | 2938 / 1546 | 0.6 s + 2.9 s | 181 MB |

`--all` peaks at ~570 MB RSS (mostly mapped pages) for 290 MB of packages. The 426
resolved models / 45 698 placements match the coverage figure in
`re/notes/instances.md` §6.4 exactly; the 29 unresolved models (729 placements, `jungle`,
`bushs`, `treetops`, `cloud`, …) have no `InstanceShape` anywhere in the extraction.

Blender import (5.0, headless): `sbeachn` → 18 264 objects, 1104 mesh datablocks, 396
materials, 198 images in ~6 s. `--all` → 119 816 objects, 5615 mesh datablocks, 2940
materials in ~250 s — it works, but the object count makes the viewport sluggish, which is
why a region is the default unit of work.

```
blender -b --python-expr "import bpy; bpy.ops.import_scene.gltf(filepath='/tmp/x.glb'); \
    print(len(bpy.data.objects), len(bpy.data.meshes), len(bpy.data.materials))"
```

## 5. Baking a lightmap in Blender

A region is the right unit of work: `--all` is 2 M triangles and 137 k objects, which is
painful to bake in one go.

```sh
python3 re/p3d2gltf.py --region sbeachn --flip-x --out /tmp/sbeachn.glb
```

1. **Import** (`File ▸ Import ▸ glTF 2.0`). Keep the per-package collections; the instance
   nodes come in as linked duplicates (one mesh datablock per shape).
2. **Realise the instances you want baked.** Linked duplicates share a mesh and therefore
   share UVs, so they cannot all hold a *different* lightmap. Either
   * bake only the world geo and leave the eco-props on their vertex colours, or
   * select the placements and `Object ▸ Relations ▸ Make Single User ▸ Object & Data`
     first (memory: 6756 placements × a palm is a lot — do it per model).
3. **Join by material** so the atlas has few islands: `Select ▸ Select All by Trait` or
   just `Ctrl+J` per package. The exporter already gives one primitive per shader, so a
   joined object has one material slot per shader used in that package.
4. **Second UV set**: `Object Data ▸ UV Maps ▸ +`, name it `Lightmap`, make it active.
   `U ▸ Lightmap Pack` (or Smart UV Project, island margin ≈ 0.02) with the object in
   Edit mode. Note `TEXCOORD_0` (and `TEXCOORD_1` where present) came from the game; the
   lightmap set has to be a new one.
5. **Target image**: new image (2048² or 4096², 32-bit float, non-color). Add an *Image
   Texture* node to every material, select it (do **not** connect it), and set its UV via
   a *UV Map* node pointing at `Lightmap`.
6. **Bake**: Cycles, `Render ▸ Bake`, bake type `Diffuse` with *Direct + Indirect* and
   *Color* off (that gives lighting only, which multiplies cleanly onto the albedo), or
   `Ambient Occlusion` for a cheap first pass. Add a sun (Miami noon is roughly 50°
   elevation) and a sky world; the game's own tint is ambient ≈ (51,43,27) and sun ≈
   (97,95,70) with a ×2 modulate, so start there if you want it to look like the game.
7. **Export the maps**: `Image ▸ Save As` per baked image, PNG, one per joined object;
   name them after the world geo so the viewer can find them.

### What the viewer would need to consume them

`p3dview`/`renderer` would need, per world geo:

* a **second UV set** on the prim group. `pddiNumUVSets(format) = format & 0xF` already
  supports up to 8, and `glPrimBufferStream::TexCoord2(s,t,channel)` and the
  `ATTRIB_TEXCOORDS0 + i` attributes are already wired for it — the only missing piece is
  a loader path that reads the lightmap UVs from a sidecar (the shipped `0x00010007`
  channel 1 is used by the `layered` shader, so do not overwrite it).
* a **lightmap texture** per world geo (not per shader — the atlas is per object), bound
  to a second sampler, and a fragment shader doing
  `colour = albedo * vertexColour * lightmap * 2`. Pure3D already has a `lightmap` shader
  type (`Shader::SetBlendMode` special-cases it next to `layered`), so the natural place
  is a new `LocalShader_…_lightmap_…` variant plus a `PDDI_SP_TOPTEX` ("TTEX") parameter
  holding the lightmap, which `shader.cpp` already parses.
* a sidecar manifest (`<worldgeo>.lm.png` + a text file mapping world geo name → image and
  UV offsets) so nothing in the shipped packages has to be rewritten.
