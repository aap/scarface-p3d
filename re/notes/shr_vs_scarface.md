# SHR-era Pure3D (2003) vs Scarface Pure3D (2006)

Sources compared:

* `/u/aap/fun/ps2engines/extracted/pure3d` — SHR/`game/libs/pure3d` source drop (May–Jul 2003).
  Chunk ids: `constants/chunkids.hpp` (new-style) and `constants/chunks.h` (legacy `0x0000xxxx`).
* `/u/aap/src/scarface-p3d` — aap's reimplementation.
* `/u/aap/src/scarface-p3d/re/hist_z04.txt` — chunk-id census over `assets/packages/z04/*.p3d`.
* Scarface class names: `grep -oE '___7[A-Za-z0-9_]+' re/r2flags.r2` (2153 vtables, names are
  `Class_Outer_Namespace`), plus `re/idb_usernames.txt`.

---

## 1. File-by-file mapping

### 1a. What aap has ported

| aap file | SHR file(s) | Scarface class(es) | notes |
|---|---|---|---|
| `refcount.h` | `p3d/refcounted.hpp/.cpp` (`tRefCounted : radLoadObject`, `tRefCountedTemp`, `tNonCopyable`, `tPtrBase`) | `core::RefCount`, `core::SafeRefCount`, `core::Object`, `core::BaseObject`, `pure3d::NonCopyable` | SHR's `tRefCounted` derives from **`radLoadObject`** (radcontent). aap's `content::LoadObject` ≈ `radLoadObject`; `pure3d::NonCopyable` ≈ `tNonCopyable`. The SHR split (ref-counted-with-heap vs non-copyable) survives verbatim into Scarface. |
| `loadstream.h/.cpp` | `p3d/file.hpp/.cpp` (`tFile`, `tFileMem`), `p3d/fileftt.cpp` | `content::LoadStream` | SHR base is `radLoadStream` (`radcontent/inc/radload/utility/stream.hpp`). Scarface's async side is `core::File`, `core::Drive`, `core::FileReadRequest`/`FileOpenByKeyRequest`/… — a radcore `radFile` descendant. |
| `chunkfile.h/.cpp` | `p3d/chunkfile.hpp/.cpp` | `content::ChunkFile` | **Near-exact port.** Same `struct Chunk {id,dataLength,chunkLength,startPosition}`, `CHUNK_STACK_SIZE = 32`, `BeginChunk/EndChunk/BeginInset/EndInset`. Only the getter names changed (`GetUInt`→`GetU32` etc.). |
| `inventory.h/.cpp` | `p3d/inventory.hpp/.cpp` (`tInventory : tEntityStore : radLoadHashedStore`), `inventoryiterator.inl` | `content::LoadInventory` + `LoadInventory::DynamicCaster<T>` | The `DynamicCaster<T>` templates are visible in RTTI: `__DynamicCaster_VGeometry_pure3d___LoadInventory_content`, …`VShadowSkin`, `VNavMesh`, `VScriptFileObject`, `VCollisionObject_ravenphysics`. **That list is a complete index of every type Scarface stores in an inventory** — use it as a to-do list. |
| `loadmanager.h/.cpp` | `p3d/loadmanager.hpp/.cpp` (44 KB), `p3d/loaders.cpp` | `content::LoadManager`, `content::LoadRequest`, `content::P3DFileHandler`, `content::SimpleChunkHandler`, `content::Stats` | SHR: `tFileHandler : radLoadFileLoader`, `tChunkHandler : radLoadDataLoader`, `tSimpleChunkHandler`, `tP3DFileHandler`, `tLoadRequest : tRefCountedTemp`. aap's `FileLoader`/`ObjectLoader` = `radLoadFileLoader`/`radLoadDataLoader`. `loadmanager.cpp:880-910` has an id→name table worth reading. |
| `entity.h` | `p3d/entity.hpp/.cpp` (`tEntity`, `tName`, `tName::TextName`) | `pure3d::Entity`, `core::String`/`core::IString` | SHR `tName` already stores a hash (`tUID`) with the text optional (`P3D_USE_ENTITY_NAMES`). Scarface = `core::Key32` (`MakeKey`). Note **`om::Entity` is a different class** (the game-object/metadata system) — don't confuse it with `pure3d::Entity`. |
| `core.h/.cpp` (`MakeKey`) | `tName::MakeUID` in `p3d/entity.cpp`; radcore `radKey`/`radMakeKey32` | `core::Key32`, `core::MakeKey` | SHR's is 64-bit (`tUID`), Scarface's 32-bit. radcore has `inc/radkey.hpp` (13 KB) — *not extracted*, see §3. |
| `imagefactory.h/.cpp` | `p3d/imagefactory.hpp/.cpp` (24 KB, `tImageFactory` + nested `class Builder`), `p3d/image.cpp`, `p3d/imageconverter.cpp`, `p3d/dxtn.cpp`, `p3d/mipmapfilter.cpp`, `p3d/rawimage.cpp` | `pure3d::ImageBuilder`, `pure3d::Image32`, `pure3d::Image8`, `pure3d::ImageHandler`, `pure3d::ImageLoader`, `pure3d::DXTNHandler`, `pure3d::RawImageHandler`, `pure3d::HDRConverter`/`HDRHandler`, `pure3d::TargaHandler`, `pure3d::PNGHandler` | Scarface renamed `tImageFactory::Builder` → `pure3d::ImageBuilder`. New: HDR image support. |
| `texture.h/.cpp` | `p3d/texture.hpp/.cpp` (`tTexture`, `tTextureLoader`, `tSetLoader`) | `pure3d::Texture`, `pure3d::TextureLoader` (`LoadTexture`/`LoadImage`/`LoadVolumeImage`) | Same chunk ids 0x19000-4. SHR's `tSetLoader` (texture sets) has no Scarface counterpart in the RTTI list. |
| `shader.h/.cpp` | `p3d/shader.hpp/.cpp` (`tShader`, `tShaderLoader`, `tShaderDefinitionLoader`) | `pure3d::Shader`, `pure3d::ShaderLoader`, `pure3d::ShaderFloatBroadcast` | Same chunk ids 0x11000-7. Scarface's shader *types* (SIMPLE/ENV/VEHICLE/SPECULAR/CHARACTER/LAYERED/DECAL/FOAM/CBVLIT/SHADOWDECAL/NIGHTLIGHT/UNTEXTURED/VERTEXFADE) are new and are visible as `pure3d::d3d*Shader` vtables. SHR's broadcast helpers (`tShaderIntBroadcast` etc., in `drawable.hpp`) survive only as `ShaderFloatBroadcast`. |
| `drawable.h/.cpp` | `p3d/drawable.hpp/.cpp` (`tDrawable : tEntity`, `tDrawable::ShaderCallback`) | `pure3d::DrawableHierarchy`, `pure3d::DrawableContainer`, `pure3d::DrawablePrimitive` | **Biggest refactor.** SHR had one flat `tDrawable` with `virtual void Display()`. Scarface split it three ways: `DrawableHierarchy` (abstract, bounds + shader callback + fade) → `DrawableContainer` (array of `PrimEntry`) → and a separate `DrawablePrimitive` leaf that `PrimGroup` derives from. `tGeometry`/`tPolySkin`/`tCompositeDrawable` were all `tDrawable` subclasses in SHR; now `Geometry : DrawableContainer` and `CompositeDrawable : DrawableHierarchy`. |
| `geometry.h/.cpp` | `p3d/geometry.hpp/.cpp` (`tGeometry : tDrawable`, `tGeometryLoader`) | `pure3d::Geometry`, `pure3d::GeometryLoader` | |
| `primgroup.h/.cpp` | `p3d/primgroup.hpp/.cpp` (72 KB!), `p3d/vertexlist.cpp` | `pure3d::PrimGroup` + `PrimGroupOptimized`/`PrimGroupSkinnedOptimized`/`PrimGroupStreamed`/`PrimGroupSkinnedStreamed`/`PrimGroupSkinnedPC`/`VertexAnimPrimGroup`, `pure3d::VertexList`, `pure3d::MatrixPaletteList` | Class set is **identical** apart from the British→American `Optimised`→`Optimized` respelling and the new `VertexAnimPrimGroup`. `primgroup.cpp` is the single most valuable SHR file for aap: it contains the full per-platform vertex-format/packing logic. |
| `compositedrawable.h/.cpp` | `p3d/anim/compositedrawable.hpp/.cpp` (25 KB) | `pure3d::CompositeDrawable` + `ActivePrimitiveList`, `ActiveControllerList` | SHR had `tCompositeDrawable : tDrawablePose` with `DrawableElement`/`DrawablePropElement`/`DrawablePoseElement`/`DrawableEffectElement`. Scarface collapsed these into `ActivePrimitive`/`ActivePrimitiveList` + `ActiveControllerList` (aap's guess about `usageMap`/`primMap` matches the SHR "prop list"/"skin list" split: 0x123001 carries an `isVisible` flag, which is what `UpdateMaps()` compacts). |
| `skeleton.h/.cpp` | `p3d/anim/skeleton.hpp/.cpp` (`tSkeleton`, `tSkeleton::Joint`, `tSkeletonLoader`), `p3d/anim/pose.hpp/.cpp` (`tPose`), `p3d/anim/drawablepose.hpp` | `pure3d::Skeleton` + `Skeleton::Limb`, `Skeleton::Partition`; `pure3d::CharacterPose`; `pure3d::SkeletonLoader` | New in Scarface: `Skeleton::Partition` (skin partitioning for HW skinning) and `Skeleton::Limb`. `CharacterPose` is the Scarface name for `tPose`. |
| `displaylist.h` (stub) | `p3d/displaylist.hpp/.cpp` (`DisplayList`) | `pure3d::DisplayList`, `renderer::Display_List`, `renderer::DisplayListPrimitive` | See §1c — SHR's tiny 4 KB `displaylist.cpp` explains the missing `sortOrder` float and the z-sort. |
| `pddi.h/.cpp` | `pddi/pddi.hpp`, `pddi/pdditype.hpp`, `pddi/pddienum.hpp`, `pddi/pddishade.hpp`, `pddi/pddiext.hpp`, `pddi/base/basecontext.*`, `pddi/base/baseshader.*`, `pddi/base/pddiobj.cpp` | `pure3d::pddiBaseContext`, `pddiBaseShader`, `pddiRenderState`, `pddiViewState`, `pddiFogState`, `pddiLightingState`, `pddiStencilState`, `pddiMatrixStack`, `pddiExtension`, `d3dExt*` | pddi is essentially **unchanged** from 2003 to 2006 — the SHR `pddi/` tree is directly usable as documentation. Scarface added extensions: `d3dExtInstancing`, `d3dExtHardwareSkinning`, `d3dExtFramebufferEffects`, `d3dExtTODFactor`, `d3dExtUnLitColourTint`, `d3dExtVertexProgram`, `d3dExtAntialiasControl`, `d3dExtGammaControl`, `d3dExtReadPixels`, `d3dExtFrameBufferTexture`, `d3dExtWin32Visibility`, `d3dExtDisplayUserSettings`. |
| `gl/*` | `pddi/gl/glcon.cpp` (context), `gldev.cpp` (device), `glmat.cpp` (shaders/materials), `gltex.cpp`, `pddi/gl/display_linux` | (n/a — Scarface PC is `pure3d::d3d*`) | aap's `gl.cpp`/`glprim.cpp`/`glprog.cpp`/`glshader.cpp`/`gltex.cpp` map 1:1 onto `gldev`/`glcon`/`glmat`/`gltex`. SHR's GL backend is a *working reference implementation of the same pddi interface* — worth diffing against. |
| `renderer/display_list.*`, `renderer/renderable.*`, `renderer/worldgeo.*` | (no SHR equivalent — SHR's game-side is `game/code/render/DSG` + `RenderManager`) | `renderer::Display_List`, `DisplayListPrimitive`, `renderer::Renderable` family | See §2 and §3. |

### 1b. SHR files aap has NOT ported, that Scarface map data needs

Ordered by how much of the z04 census they unlock.

| chunk ids (count in z04) | what it is | SHR starting point | Scarface class |
|---|---|---|---|
| `0x00013000-07` (1830 lights) + legacy `0x00002380` (152) | Light / direction / position / shadow / decay range / decay-range-rotY; `0x2380` = `P3D_LIGHT_GROUP` (**legacy id, `constants/chunks.h:71`**, still handled in SHR by `tLightGroupLoader`) | `p3d/light.cpp/.hpp`, `p3d/lightloader.cpp` (8.5 KB — *the* chunk reader, incl. `tLightGroupLoader(P3D_LIGHT_GROUP)` at `lightloader.cpp:239`), `p3d/lightschooser.cpp` (42 KB, per-object light selection), `ambient/directional/point/spotlight.cpp` | `pure3d::Light`, `LightGroup`, `LightGroupLoader`, `LightsChooser`; `renderer::SFLightGroupLoader` (= chunk `0x08800007`, 151 in z04), `renderer::LightingRenderable` |
| `0x00014000/01` (60/4) | Locator (name, type u8, u8, params, position) | `p3d/locator.cpp/.hpp` (tiny, 1 KB) | `pure3d::LocatorLoader` |
| `0x00017005-0x0001700d` (353/116/469/353/39/2/41) | Billboard quad group. `0x17006` = group (name + shader name + counts), `0x17005` = quad (name, mode string `"AAX"`, colour, size), `0x17007` = per-quad transform/uv block, `0x17009` = matrix/uv, `0x1700a` = cut-off info (`"BOTH"` + 4 floats), `0x1700d` = 4 floats. **SHR ids were `0x17000-4`; Scarface renumbered to `0x17005+` and added cut-off quads.** | `p3d/billboardobject.cpp` (**116 KB**) / `.hpp` (26 KB) — contains the mode strings and the full quad-group loader; `p3d/anim/billboardobjectanimation.cpp` | `pure3d::BillboardObject`, `BillboardQuad`, `BillboardQuadGroup`, `BillboardCutOffQuad`, `BillboardCutOffQuadGroup`, `BillboardObjectLoader`, `BillboardQuadGroupLoader` |
| `0x0001580c` (126) → `0x00015900` (310) → `0x00015b00/01/02` (133/141/36) | Particle system. `0x1580c` = system factory (name, float, u32 count, vector), `0x15900` = emitter factory (name + shader name + ~15 params), `0x15b00/01/02` = particle / emitter / generator animation blocks (each holds `0x00121000` animations). **SHR: `0x15800`=SYSTEM_FACTORY … `0x15808/09/0a` = PARTICLE/EMITTER/GENERATOR_ANIMATION. Scarface moved factory to `0x1580c`, emitter to `0x15900`, animations to `0x15b0x`.** Bounds come in as `0x10003/0x10004` children. | `p3d/effects/particleloader.cpp` (17 KB — **the chunk reader, maps almost 1:1**), `particlesystem.cpp`, `particleemitter.cpp` (34 KB), `particlegenerator.cpp` (22 KB), `particlearray.cpp`, `particletype.hpp`, `constants/psenum.hpp` | `pure3d::ParticleController`, `ParticleSystemLoader`, `ParticleEmitterLoader`, `SpriteParticleEmitter` (+ `OverLifeAttributes`, `EmitterBlendState`), `ParticlePointGenerator`/`ParticlePlaneGenerator`/`ParticleSphereGenerator` (+ `GeneratorBlendState`), `DefaultParticleEmitterHandler`, `DefaultParticleGeneratorHandler`, `ParticleAllocatorTemplate<SpriteParticle>`; `renderer::ParticleEffectRenderable` |
| `0x00121000/01/02/06/07/08`, `0x00121100-0x00121110`, `0x00121402` (1499 animations, 17638 channel-key blocks) | Animation family. `0x121000` ANIMATION, `0x121002` GROUP_LIST, `0x121001` GROUP, `0x1211xx` channels (same ids as SHR!), `0x121110` INTERPOLATION_MODE. **New: `0x121006`/`0x121007` = channel descriptor list + descriptor (`0x121007` payload is `{u32 flags, u32 channelTypeChunkId, u32 count, u16…}` — e.g. `00 11 12 00` = `0x00121100`); `0x121008` = size/hint (SHR had `0x121004` SIZE).** | `p3d/anim/animate.cpp` (**50 KB** — `tAnimation`, `tAnimationGroup`, `tAnimationLoader`, frame controllers), `p3d/anim/channel.cpp` (**51 KB** — every channel type + their chunk readers) | `pure3d::Animation`, `AnimationGroup`, `AnimationLoader`, `ChannelLoader`, `DefaultChannelHandler`, `SimpleChannel`, `Float1Channel`, `Float2Channel`, `Vector1DOFChannel`/`2DOF`/`3DOF`, `VectorChannel`, `QuaternionChannel`, `RotationChannel`, `CompressedQuaternionChannel<CompressedQuaternion3/4/6/8>`, `ColourChannel`, `BoolChannel`, `IntChannel`, `StringChannel`, `EntityChannel` |
| `0x00121201` (298), `0x00121204` (72) | Frame controller (`0x121201` payload: `u32, name "TEX_sparkle_m", type "TEX", "ANIM", u32, animName, targetName`) and frame-controller list. **SHR id was `0x121200`.** | `p3d/anim/animate.cpp` (`tFrameControllerLoader`, `tSimpleFrameController`, `tBlendFrameController`, `tAnimationFrameController`), `p3d/anim/multicontroller.cpp` | `pure3d::FrameController`, `FrameControllerLoader`, `SimpleFrameController`, `BlendFrameController`, `AnimationFrameController`, `MultiController`, `DefaultFrameControllerHandler` |
| `0x00121305` (11) → `0x00121306` (43) → `0x00010f01` (37) / `0x00010f02` (19) | Vertex animation. `0x121305` = key-frame list header (`u32, nKeys, indices…`), `0x121306` = one key, `0x10f01` = named offset channel `"UV0"` (count + index/value pairs), `0x10f02` = `"CLR0"`. **New ids; SHR had `0x121300` COLOUR / `0x121301` VECTOR / `0x121302` VECTOR2 / `0x121303` INDEX / `0x121304` KEY.** Note Scarface put the *payload* lists back into the Mesh range (`0x10f0x`). | `p3d/anim/vertexanimkey.cpp` (11 KB — the loader), `vertexanimobject.cpp`, `vertexoffsetexpression.cpp` (41 KB), `expression.cpp` | `pure3d::VertexAnimKeyFrameList`, `VertexAnimPrimGroup`, `VertexOffsetExpressionMixer(Loader)`, `ExpressionGroup(Loader)`, `ExpressionLoader` |
| `0x00120100` → `0x0012010b` → `0x0012010c` → `0x0012010d` → `0x0012010f` (1 each, Common.p3d) | Scene graph. `0x120100` = "Scene", `0x12010b` ≈ ROOT, `0x12010c` = node "root", `0x12010d` = transform node "world" + matrix, `0x12010f` = drawable/attachment list naming particle systems (`fxSys_SmBoat_WakeFast`). **SHR: `0x120101` ROOT … `0x120107` DRAWABLE, `0x12010a` SORTORDER. Scarface renumbered `+0xa`.** Only one instance — low priority. | `p3d/scenegraph/scenegraph.cpp` (26 KB) | (no `sg::` classes in the Scarface RTTI dump — may be dead/tool-only) |
| `0x00010019` (1), `0x0001001a` (1071), `0x0001001b` (1072) | Shadow skin / shadow mesh / topology (**already SHR ids**, `chunkids.hpp` Mesh namespace) | `p3d/shadow.cpp` (**41 KB**), `p3d/shadow/shadow_common.cpp` (43 KB), `shadow_generic.cpp`, `p3d/shadow/implementation_common.hpp` | `pure3d::ShadowMesh`, `ShadowMeshImpl`, `ShadowMeshTopology`, `ShadowSkin`, `ShadowSkinImpl`, `ShadowSkinTopology`, `ShadowMeshLoader`, `ShadowSkinLoader`, `ShadowGenerator`; `renderer::ShadowRenderable`, `renderer::ShadowLoader` (chunk `0x08800008`, 44 in z04) |
| `0x0001001c` (1174) | MULTICOLOURLIST — **already an SHR id** (`chunkids.hpp` Mesh) | `p3d/geometry.cpp` / `p3d/primgroup.cpp` | `pure3d::PrimGroupLoader` |
| `0x00010f01/02` | see vertex anim above | | |

### 1c. New / changed chunk ids — best guesses + closest SHR file

| id | count | parents | reading of the data | closest SHR file |
|---|---|---|---|---|
| `0x00002380` | 152 | root | **Not new — legacy `P3D_LIGHT_GROUP`** (`constants/chunks.h:71`). Payload: name + `u32 n` + n light names. | `p3d/lightloader.cpp:239` |
| `0x00007000` | 152 | root | **Not new — legacy `P3D_HISTORY`** (`chunks.h:74`). SHR registers `tIgnoreLoader` for it (`loaders.cpp:106`). Safe to skip. | `p3d/ignoreloader.hpp` |
| `0x00007030/31/32` | 146/1022/1314 | root / 7030 | **Not new — legacy Maya-exporter `P3D_EXPORT_INFO` / `_NAMED_STRING` / `_NAMED_INT`** (`chunks.h:78-80`). Observed keys: `ExporterVersion`, `Exported By`, `Exported At`, `De-Index Meshes`. Pure metadata; skip (but add to an ignore-loader so the log stays clean). | `p3d/ignoreloader.hpp`, `p3d/loadmanager.cpp:899` |
| `0x00023000` / `0x00023001` | 1611 / 11086 | root / 23000 | **Skeleton / Skeleton joint** — confirmed: `0x23000` = `{name, u32 0, u32 nJoints, u32 0, u32 0}`, `0x23001` = `{name, u32 parent, u32 flags, 4x4 matrix}`. Scarface allocated the block SHR's `chunkids.hpp` marked `// next free 0x00023000-0x00023fff`; SHR itself still used legacy `P3D_SKELETON = 0x4500`. **aap already has this right** (`skeleton.h`). One skeleton per `0x123000` CompositeDrawable. | `p3d/anim/skeleton.cpp` |
| `0x00122000` | 8275 | 0x10000, 0x10019, 0x1001a, 0x1580c, 0x17006 | **Drawable sort order.** Payload is `{u32 0, float}` and the float is `0.5` in 24 of 27 cases, with a handful at `0.458/0.457/0.459`. That is exactly SHR's `DisplayList::Add(tDrawable*, const rmt::Matrix*, float sortOrder = 0.5f)` (`p3d/displaylist.hpp:22`). SHR carried sort order only inside the scene graph (`0x0012010a SORTORDER`); Scarface made it a generic per-drawable-root chunk. **This is aap's `DrawableContainer`'s `// float unknown` and `DisplayListDrawable`'s `// float containerUnk; // copied from container`.** | `p3d/displaylist.cpp` (4 KB — read `ZSortCompare`/`Sort`) |
| `0x0001001d` | 1645 | 0x10000 | Four u32, constant `(1, 3, 1, 0)` in every sample. Present on only ~24 % of meshes. Given `0x1001c` = MULTICOLOURLIST, this is the next free Mesh id; likely a small render-flags/LOD/instancing descriptor rather than aap's guessed "MESHSTATS". Low risk to ignore. | `p3d/geometry.cpp` |
| `0x00010021` | 6961 | 0x10000 | One per MESH (same count as `0x10000` and `0x10017` RENDERSTATUS). Payload: `u32 0` + 7×`u32 0x20` — seven per-stream bit widths, all 32 → uncompressed. **aap's `VERTEXCOMPRESSIONHINT` guess is almost certainly right**; on PS2/compressed assets these should vary. | `p3d/primgroup.cpp` (vertex packing) |
| `0x00010f01` / `0x00010f02` | 37 / 19 | 0x121306 | Named vertex-anim offset channels `"UV0"` / `"CLR0"`: `{u32, char[4] tag, u32 n, (u32 index, u32 value)×n}`. SHR equivalents were `0x121300` COLOUR / `0x121302` VECTOR2 offset lists. | `p3d/anim/vertexanimkey.cpp`, `vertexoffsetexpression.cpp` |
| `0x00121006` / `0x00121007` / `0x00121008` | 1499 / 4057 / 447 | 0x121000 | Channel descriptor list / descriptor / size. `0x121007` = `{u32 flags, u32 channelChunkId (e.g. 0x00121100), u32 nChannels, u16…}` — a precomputed index of which channels the animation contains (avoids a pre-pass at load). `0x121008` = 4 bytes ≈ SHR's `0x121004` SIZE. | `p3d/anim/animate.cpp`, `channel.cpp` |
| `0x01000001` | 193 | root | **Foundation range (`0x01000000-0x01ffffff`, per `chunkids.hpp` header comment).** Uses **u32-length-prefixed strings**, not p3d `u8`-length strings. Payload names `fx_electricbox_large01`, `explo`, `state_02`, then `SeqEventVarPosDist`, `SeqEventLogicOr`, `SeqEvent…` → this is the **audio/effect sequencer** data (`audio::SeqEvent*`, `audio::Sequence`, `audio::SeqTrack`, `audio::SeqBuss` in the RTTI dump; `om::MetaType`/`om::MetaAttrib` reflection). | **Nothing in SHR pure3d.** SHR's analogue is `game/libs/radsound` + `game/libs/choreo`. |
| `0x01005009` | 403 | root | Foundation: `{u32len "slotMachineA", 8 zero bytes, u32len "fx_electricbox_large01"}` — a **name→resource link** binding a state prop to a `0x01000001` sequence. | same |
| `0x07000003` | 75 | 0x0701000a | Collision mesh **vertices**: `u32 n` + n×3 floats (2164 verts in the sample; 25968/12 = 2164 ✓). | `game/libs/sim/simcollision` (**not extracted**) |
| `0x07000006` | 75 | 0x0701000a | Collision mesh **triangles**: `u32 n` + n×4 u16 (3 indices + 1 extra — material/flags). | same |
| `0x07000007` | 75 | 0x0701000a | Collision **adjacency**: same n, n×{3 u16 neighbour tri (`0xffff` = none), u16 `0x1615` edge flags}. | same |
| `0x07000008` | 75 | 0x0701000a | Collision **spatial index (BVH/kd)**: `u32 n` + packed 8-byte nodes `{u32 packed plane/float, u16 start, u16 end}` with monotonically increasing start/end ranges. | same |
| `0x0701000a` | 75 | 0x07010001 | Wrapper for the four above: `{u32 5, 8 zero bytes, 6 floats}` = type tag + AABB min/max. This is the Scarface **collision mesh volume** (SHR `Collision::VOLUME` children were only sphere/cylinder/obbox/bbox/wall). | `sim/simcollision/collisionvolume.cpp` |
| `0x07010025` | 75 | root | Top-level **world collision object**: `{name "devilscay_02_shell", u32 1}` then `0x07010001` VOLUME. A second flavour of SHR's `Collision::OBJECT = 0x07010000` (777 of those also present) — probably "static world" vs "dynamic object". | `sim/simcollision/collisionobject.cpp` |
| `0x07016000` / `0x07016010` / `0x07016012` | 69 / 110 / 184 | 0x0701000a / 0x07016000 | **Named per-triangle attribute sets** (Scarface-only). `0x07016000` = `{u32 0, u32 nSets}`; `0x07016010` = `{u32, name "VehicleIgnore", u32 n, n×u16 triIndex, n×u16 value}`; `0x07016012` = `{u32, name "surfaceTypeA", u32 n, n×u16}`. Matches `CollisionSurfaceType` in the RTTI dump. | no SHR equivalent; nearest is `sim/simcollision/collisionvolume.cpp` |
| `0x08011000` → `0x08011001/02/03/04`, `0x08011005` | 71 each; `08011005` 2 | root | **Streaming zone descriptor** (Scarface-only, in the SmartProp project range). `0x08011000` = `{name "devilscay_01_shell", u32 0, name again}`; `0x08011002` = 3 bytes `01 00 07`; `0x08011004` = `u32 0`; `0x08011001` = `u32 1`; `0x08011003` (41) = `u32 3418` + 3418 small bytes (0/3/7/0x55) — a per-cell visibility/LOD grid. `0x08011005` (only in `islands_LOD.p3d`/`miami_lod.p3d`) = `{u32 n, (name, u32 index)×n}` — a **zone-name → index table**. | no SHR equivalent; nearest is `game/code/render/Culling/WorldScene.cpp` + `game/code/worldsim` |
| `0x0802000d` → `0x0802000e` → `0x08020002/03/08/0b` | 1009 / 1612 / 3031 / 283 / 4403 / 1593 | root | **StateProp v2.** `0x0802000d` = `{u32 4, name "slotMachineA", name, u32 nStates, u32}`; `0x0802000e` = one state (`"idle"`, params); `0x08020002` VISIBILITY and `0x08020003` FRAMECONTROLLER keep **SHR's ids**; new `0x08020008` = string attribute pair (`"name"` → `"slotMachineA"`), new `0x0802000b` = effect attachment (`"Effect"` → `"epDebris_Dust"`, parent name, matrix). SHR only had `0x08020000-5`. | SHR has the ids in `constants/chunkids.hpp` but **no loader in the pure3d tree** — it lives in `game/code/stateprop` (52 KB) + `game/code/render/DSG/StatePropDSG.cpp` | `pure3d::prop::StatePropData`, `pure3d::prop::StatePropDataLoader`, `pure3d::prop::EffectQueue`, `renderer::StatePropRenderable`, `renderer::SFStatePropLoader` (chunk `0x08800005`, 1009 — same count as `0x0802000d` ✓) |

---

## 2. SHR → Scarface class renames / refactors

**Global:** SHR's `t`-prefix + global namespace → Scarface namespaces. `radLoad*` (radcontent) was
absorbed into **`content::`**; the game-side DSG/render layer became **`renderer::`**; radcore became
**`core::`**; and a new reflection/entity system **`om::`** (`om::Entity`, `om::MetaTypeBase`,
`om::MetaAttribBase`, `om::Stream`, `om::ContentStream`, `om::DynBehaviour`) appeared.

| SHR | Scarface |
|---|---|
| `tEntity` / `tName` (64-bit `tUID`) | `pure3d::Entity` / `core::Key32` (32-bit), `core::String`/`IString` |
| `tRefCounted : radLoadObject`, `tRefCountedTemp`, `tNonCopyable`, `tPtrBase` | `core::RefCount`, `core::SafeRefCount`, `core::Object`/`BaseObject`, `pure3d::NonCopyable` |
| `tInventory : tEntityStore : radLoadHashedStore` | `content::LoadInventory` (+ `LoadInventory::DynamicCaster<T>`) |
| `tLoadManager`, `tLoadRequest`, `tFileHandler`/`radLoadFileLoader`, `tChunkHandler`/`radLoadDataLoader`, `tSimpleChunkHandler`, `tP3DFileHandler` | `content::LoadManager`, `content::LoadRequest`, `content::P3DFileHandler`, `content::SimpleChunkHandler`, `content::Stats` |
| `tChunkFile : radLoadStream`, `tFile`, `tFileMem` | `content::ChunkFile`, `content::LoadStream`; async I/O moved to `core::File`/`core::Drive`/`core::*Request` (radcore `radFile`) |
| `tDrawable` (one class, `virtual Display()`) | **split:** `pure3d::DrawableHierarchy` → `pure3d::DrawableContainer`, plus leaf `pure3d::DrawablePrimitive` |
| `tGeometry : tDrawable` | `pure3d::Geometry : DrawableContainer` |
| `tCompositeDrawable : tDrawablePose` with `DrawableElement`/`DrawablePropElement`/`DrawablePoseElement`/`DrawableEffectElement` | `pure3d::CompositeDrawable : DrawableHierarchy` with `CompositeDrawable::ActivePrimitive`, `ActivePrimitiveList`, `ActiveControllerList` |
| `tPose`, `tPosable`, `tDrawablePose` | `pure3d::CharacterPose` |
| `tSkeleton`, `tSkeleton::Joint` | `pure3d::Skeleton`, `Skeleton::Limb`, `Skeleton::Partition` (new) |
| `tPrimGroupOptimised`/`SkinnedOptimised` | `PrimGroupOptimized`/`PrimGroupSkinnedOptimized` (spelling), + new `VertexAnimPrimGroup` |
| `tImageFactory::Builder` | `pure3d::ImageBuilder` |
| `DisplayList` (p3d, z-sorted, `Add(drawable, matrix, sortOrder=0.5f)`) | `pure3d::DisplayList` (abstract) + `renderer::Display_List` (84 render lists) + `renderer::DisplayListPrimitive` |
| `sg::Scenegraph`, `sg::Node`, `sg::Transform`, `sg::Drawable`, `sg::Loader` | effectively gone (1 scene-graph chunk in all of z04); replaced by `renderer::Scene`/`GamePlayScene`/`RenderManager` |
| game-side `IEntityDSG`, `StaticEntityDSG`, `InstStatEntityDSG`, `StatePropDSG`, `DynaPhysDSG`, `WorldSphereDSG`, `TriStripDSG`, `LensFlareDSG`, `RenderLayer`/`WorldRenderLayer`/`RenderManager` (`game/code/render`) | `renderer::WorldGeoRenderable`, `InstanceRenderable`/`InstancePrimitive`, `StatePropRenderable`, `CharacterRenderable`, `VehicleRenderable`/`VehiclePrimitive`/`VehicleContainer`, `SkyRenderable`, `ZonePkgRenderable`, `ShadowRenderable`, `OceanRenderable`/`OceanPrimitive`/`OceanContainer`, `WakeRenderable`/`WakePrimitive`/`WakeContainer`, `DecalPrimitive`/`DecalContainer`, `Skidmark*`, `TraceFireRenderable`, `RainRenderable`, `NISRenderable`, `MaskRenderable`, `PlugInRenderable`, `LightingRenderable`, `ParticleEffectRenderable`, `PropRenderable`, `renderer::RenderManager`, `renderer::Scene`/`GamePlayScene`, `renderer::Canvas`, `renderer::RenderFlowClient` |
| `tStateProp*` chunk ids only | `pure3d::prop::StatePropData(Loader)`, `pure3d::prop::EffectQueue` |
| `sim::`/`Simulation` collision (`game/libs/sim`) | `ravenphysics::CollisionObject`, `ravenphysics::CollisionObjectLoader`, `ravenphysics::BroadPhaseDetectorSweepAndPrune`, `CollisionSurfaceType`, `NavMesh` |
| radcore `radFile`/`radKey`/`radMemory`/`radController` | `core::File`, `core::Drive`, `core::Win32Drive`, `core::CementLibrary`/`ICementLibrary`, `core::MemoryDlAllocator`/`MemorySmallDlAllocator`/`MemoryPool`/`MemoryAllocatorMalloc`, `core::ControllerDirectInput*`, `core::Platform` |

**Loader-registration shape is unchanged:** SHR `p3d/loaders.cpp::InstallDefaultLoaders()` is
`p3d->AddHandler(new tXxxLoader)` where the loader carries its own chunk id; aap's
`p3dview.cpp:50-57` does the same with an explicit id. The full Scarface loader set can be read off
the RTTI list: `GeometryLoader`, `TextureLoader`, `ShaderLoader`, `SkeletonLoader`,
`CompositeDrawableLoader`, `PolySkinLoader`, `LocatorLoader`, `LightGroupLoader`,
`BillboardObjectLoader`, `BillboardQuadGroupLoader`, `ShadowMeshLoader`, `ShadowSkinLoader`,
`AnimationLoader`, `ChannelLoader`, `FrameControllerLoader`, `MultiControllerLoader`,
`ParticleSystemLoader`, `ParticleEmitterLoader`, `ExpressionGroupLoader`, `ExpressionLoader`,
`VertexOffsetExpressionMixerLoader`, `SpriteLoader`, `ImageLoader`, `TextureFontLoader`,
`ImageFontLoader`, `IgnoreLoader`, `frontend::ProjectLoader`, plus `renderer::{CharacterLoader,
VehicleLoader, SkyLoader, WorldGeoLoader, ZonePkgLoader, SFStatePropLoader, SFLightGroupLoader,
ShadowLoader}` and `ravenphysics::CollisionObjectLoader`.

---

## 3. What else to extract from the SHR tree

Sizes are `du`-style KB from `shr_du.txt`.

1. **`./game/libs/radcontent` — 11009 KB total, but the only interesting parts are
   `inc/radload` (65 KB, 5 headers) and `src/radload` (73 KB, 8 files).**
   *Highest value per byte in the whole tree.* These are `radLoadStream`, `radLoadObject`,
   `radLoadHashedStore`, `radLoadFileLoader`, `radLoadDataLoader`, `radLoadCallback`,
   `manager.cpp`, `request.cpp`, `queue.cpp`, `inventory.cpp`, `hashtable.cpp` — i.e. the **exact
   base classes that Scarface's whole `content::` namespace was built from**. Answers: what does
   `LoadRequest`'s state machine really look like, how does the async queue/callback work, what is
   the inventory's hash-table/parent-chain semantics, what does `LoadStream`'s virtual interface
   look like. aap's `loadmanager.cpp` (`ServiceOne`, "wrong") and `inventory.cpp` are guesses today.

2. **`./game/libs/radcore/inc` (363 KB, 28 headers) + `./game/libs/radcore/src/radfile` (877 KB),
   `src/radkey` (5 KB), `src/radmemory` (605 KB).**
   `inc/radkey.hpp` (13 KB) + `src/radkey` answer **the open question in `re/README.md`: the
   `cement.rcf` hash function** — `pc_rcf.txt`/`ps2_rcf.txt` names are still unmatched to hash
   entries. `src/radfile` contains the **RCF/cement archive reader** (`filecementer.exe`,
   `wincementer.exe` are in `radcore/bin`) and the `radFile`/`IRadFileCompletionCallback` async
   model that becomes `core::File`/`core::Drive`/`core::*Request`. `radmemory.hpp` explains
   `core::MemoryDlAllocator`/`MemoryPool`/`radMemoryAllocator` (the `allocator` field aap
   commented out in `LoadOptions`). Skip `radcore/bin`, `build`, `doc`.

3. **`./game/code/render` — 1740 KB.** Sub-dirs worth having, in priority order:
   * `render/DSG` (477 KB) — `IEntityDSG`, `StaticEntityDSG`, `InstStatEntityDSG`,
     `StatePropDSG`, `TriStripDSG`, `LensFlareDSG`, `WorldSphereDSG`, `animentitydsg`,
     `collisionentitydsg`, `breakableobjectdsg`. This is the direct ancestor of
     `renderer::WorldGeoRenderable` / `InstanceRenderable` / `StatePropRenderable`, i.e. of
     `renderer/renderable.cpp` and `renderer/worldgeo.cpp`. Answers: what is in a `Renderable`'s
     element array, how are per-element draw distances / fade used, how does `SetVisible`
     propagate.
   * `render/RenderManager` (229 KB) — `RenderManager.cpp`, `RenderLayer.cpp`,
     `WorldRenderLayer.cpp`. **Answers aap's stated open problem: render order / sorting.**
     `renderer::Display_List`'s 84 render lists are the Scarface descendant of the SHR layer system.
   * `render/Loaders` (336 KB) — the wrapped-loader pattern (`IWrappedLoader`,
     `GeometryWrappedLoader`, `InstStatEntityLoader`, `StaticEntityLoader`,
     `instparticlesystemloader`). Answers how a game-level renderable chunk (`0x08800003`
     WorldGeo, `0x08800005` StateProp) wraps a pure3d drawable chunk.
   * `render/Culling` (421 KB) — `WorldScene.cpp` (85 KB), `SpatialTree`, `OctTree*`,
     `ISpatialProxy`. Closest thing to Scarface's zone/`0x08011000` streaming grid and to the
     `0x07000008` collision BVH node layout.
   * `render/Particles` (92 KB), `render/RenderFlow` (22 KB → `renderer::RenderFlowClient`),
     `render/breakables` (38 KB).

4. **`./game/libs/sim` — 96719 KB total, but only `sim/simcollision` (456 KB) and
   `sim/simcommon` (409 KB) are source.** (`sim/build` 66 MB and `sim/lib` 25 MB are binaries —
   skip them.) `simcollision/collisionobject.cpp`, `collisionvolume.cpp`, `collisiondetector.cpp`,
   `subcollisiondetector.cpp` are the readers/consumers of the `0x0701xxxx` chunk family
   (`OBJECT`/`VOLUME`/`SPHERE`/`CYLINDER`/`OBBOX`/`BBOX`/`WALL`/`VECTOR`/`OWNER`/`ATTRIBUTE`),
   which Scarface still uses verbatim for `0x07010000-07`, `0x07010021-23`. Also
   `sim/simphysics`-equivalent for `0x07011000/01/02/20` (Physics OBJECT/IMAT/VECTOR/JOINT).
   Will not explain the *new* `0x07000003-8`, `0x0701000a`, `0x07010025`, `0x07016xxx` — but gives
   the surrounding structure and the naming.

5. **`./game/code/loading` — 269 KB (small, cheap).** `p3dfilehandler.cpp`,
   `cementfilehandler.cpp`, `filehandlerfactory.cpp`, `loadingmanager.cpp`, `locatorloader.cpp`
   (37 KB). Answers how the game layer registers file handlers per extension and drives
   `tLoadManager` — directly comparable to aap's `LoadManager::LoadFile`/`ServiceOne` and to
   `core::CementLibrary`/`CementLoader`/`CementLoadTracker`.

6. **`./game/code/stateprop` — 52 KB (tiny).** The `0x08020000-5` loader that Scarface extended into
   `0x0802000d/0e/08/0b` + `pure3d::prop::StatePropData`. Pair it with
   `render/DSG/StatePropDSG.cpp` (37 KB).

7. **`./game/code/worldsim` — 2339 KB.** Lower priority for rendering, but it is where SHR keeps the
   world/zone/instance management that Scarface replaced with the `0x08011xxx` zone chunks and
   `renderer::ZonePkgRenderable`/`ZonePkgLoader` (`0x08800004`). Extract only if the zone/LOD
   streaming becomes the focus.

8. *Not worth extracting:* `radmovie`, `radmusic`, `radsound`, `radscript`, `scrooby`, `choreo`,
   `poser`, and every `build/`, `lib/`, `bin/`, `dist/` directory. The one exception: if the
   Foundation chunks `0x01000001`/`0x01005009` (audio sequencer) ever matter, that logic is in
   `radsound` (168 MB) + `choreo` (57 MB) — but those chunks are game-audio, not rendering.

---

## Quick wins for aap

* `0x00122000` is the missing sort-order float — wire it into `DrawableContainer` and into
  `DisplayListDrawable::containerUnk`, and read `p3d/displaylist.cpp` for the z-sort.
* Register an ignore-loader for `0x00007000`, `0x00007030-32` (legacy `P3D_HISTORY` /
  `P3D_EXPORT_*`) exactly as `p3d/loaders.cpp:104-107` does.
* `re/r2flags.r2`'s `__DynamicCaster_V<Type>___LoadInventory_content` entries enumerate every type
  Scarface puts in a `LoadInventory` — a ready-made checklist of remaining loaders.
* `p3d/primgroup.cpp` (72 KB) and `p3d/anim/channel.cpp` (51 KB) are the two SHR files with the
  highest density of directly reusable format knowledge.
