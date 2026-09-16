# RE toolkit for Scarface (PC + PS2)

Generated from /u/aap/lib/pure3d/scarface_pc/Scarface.idb with python-idb (venv/).
The on-disk Scarface_orig.exe is SecuROM-packed (encrypted .text) so it is useless for code;
the IDB was made from an unpacked memory dump whose segments are:
  00401000-0072f000 code (IDA calls it .rdata), 0072f000-007b7000 .data, 007b7000-0085e000 .bss,
  00860000-009ce000 more code/.text, 009ce000.. imports etc.   (see segments.txt)

Files
  scarface_unpacked.bin  flat image of 0x401000..0x9fc000 (gitignored, regenerate: venv/bin/python idbdump.py && venv/bin/python -c ...)
  idb_names.txt          all IDB names (addr name); idb_usernames.txt = only hand-given names
  idb_funcs.txt          IDB function ranges (start end name)
  idb_cmts.txt           IDB comments (mostly auto type comments)
  r2flags.r2             radare2 flag script (sym.<name> with non-alnum -> _)
  xrefs_to.txt           static xrefs: "target name <- src:kind(func) ..." kinds: call jmp ref data
  hist_z04.txt           chunk-id histogram over assets/packages/z04/*.p3d (count, parents, files)
  pc_rcf.txt ps2_rcf.txt cement.rcf directory listings (hash = h*31+c with blind tolower, one leading backslash skipped; see notes/ps2.md)

Helpers
  ./dis.sh <addr|sym.flag> [n]   disassemble function (pdf) or n instructions at addr (radare2)
  ./xr.sh <addr>                 who references addr
  ./func.sh <addr>               which IDB function contains addr
  python3 p3dwalk.py tree <file.p3d> [maxdepth]   dump chunk tree (with first p3d string)
  python3 p3dwalk.py hist <files...>              chunk-id histogram
  python3 p3dhex.py <file> <chunkid> <n>          hexdump first n chunks of that id
  python3 p3dblock.py <file> <classname|objname>  decode a 0x09900190 script-object block
  python3 rcf.py <cement.rcf> list|extract <outdir> [substr]
  python3 streamgraph.py [triggers|zones|regions|at X Z|json]
                                 decode art/levels/z04/streamgraph.p3d: the 297 polygon stream
                                 triggers that say which packages are resident where
                                 (notes/streaming.md = how the world is organised)
  python3 p3d2gltf.py --list     the world as the stream graph organises it (regions, subzones)
  python3 p3d2gltf.py --out world.glb [--zone sbeachn_01_shell|--region nbeach|--at X Z|--files ..|--all]
                      [--no-instances] [--lod] [--flip-x]
                                 export the map (world geo + eco-prop instances + materials
                                 with embedded PNGs) as glTF 2.0 -> Blender (notes/gltf_export.md)
  venv/bin/python coverage.py          attribute every function to a class (vtables, ctors,
                                       call graph, .obj contiguity) -> .cov_cache.pkl
  venv/bin/python coverage_report.py   bucket those into LEAK / SHR-DERIVED / MISSING-ENGINE /
                                       THIRD-PARTY, write coverage_funcs.txt, print the tables
                                       (see notes/coverage.md, notes/coverage_tables.txt)
  venv/bin/python strrefs.py <lo> <hi> strings referenced from functions in an address range
  venv/bin/python imagemap.py          per-32K map of the image: dominant class, unattributed bytes

Files (generated)
  coverage_funcs.txt     addr size bucket subsystem class how   (24258 rows)

Key facts so far
  - 0x09900190/91 = script object (ScriptObject) definitions: (scriptname, objectname, classname) e.g. objects.ds / instanceobject
    0x09900192 = property (name, value string), 0x09900194 = preprocessed instance location (3 floats pos + 9 u32)
    2470 instanceobjects with 47631 locations in z04 -> these are the trees/streetlights/props.
  - leaked game source (/u/aap/lib/pure3d/scarface_src) is the "gameobject" layer; the "engine" layer
    (renderer::, content::, pure3d::prop::, StatePropManager, ScriptObject) is NOT in the leak.
  - PS2 ELF (SLES_541.82) has no symbols but has demangled class-name strings ("renderer::WorldGeoRenderable") from a custom RTTI.
  - SHR-era Pure3D source: /u/aap/fun/ps2engines/extracted/pure3d (constants/chunkids.hpp has the chunk id ranges)

Implemented in the repo from these notes (2026-09-15, renderer:: restructured 2026-09-16)
  - core::GetHash (retail 0x6dc190) is now the inventory UID key; MakeKey/MakeKeyCI kept for joints.
  - renderer/zonepkg.*: 0x8800004/0x8800009 ZonePkg loader -> per-world-geo draw distances + otherPosition.
    Renderable::Display now honours drawDist min/max (distance clamped at 0 inside the sphere).
  - renderer/instance.*: 0x09900190/91/92/94 script-object loader for "instanceobject" (eco props):
    decodes the 48-byte location record, builds the matrix (rot -x,-y,+z; scale; y/100),
    binds each placement to the <model>InstanceShape mesh via a global shape registry filled by
    p3dview (RegisterInstanceShape), draws with a per-instance matrix through DisplayListPrimitive.
    2173 of 2255 models resolve (44193 placements). Default cull 300 (InstanceRenderable::defaultCullMax).
    The InstanceShape mesh is only the wind-swayed crown; the trunk exists only in <model>LODShape (whole
    tree). Retail draws both from distance 0 and only the LODShape past the near band (notes/renderspine.md
    §5.4); the viewer does the same with two DisplayListPrimitives per placement (near band = 0.6*cull + radius).
  - renderer/ now reads like the retail namespace (see renderer/README.md for the guided tour).
    Every non-obvious function carries a "// retail: renderer::Foo::Bar 0x4xxxxx" comment.
      display_list.*  Display_List with the real Node (matrix, sortKey, sortKey2, elem, container,
        shader, parent, list link, parent link, self), the 84 lists with per-list DIRTY flags,
        AddContainerElement with the complete layer->list table incl. the layer-2 (shadow) case and
        the fade/sort-key writes, per-node IsNodeVisible (frustum + an occlude::IsBoxVisible hook that
        always passes: no occluders loaded), SortAllLists with the four retail policies per list
        (CmpShader / CmpKeyThenDepth after ComputeDepthKeys / CmpKeyThenMaterial / CmpKey),
        RenderList(list, applyFade) and Render() as the 28 numbered groups of notes/displaylist.md §1
        with their SetZWrite/SetColourWrite state, FreeOrphanNodes at the end of the frame.
        pddiContext::SetColourWrite added (gl: glColorMask). aap's extra depth sort of the non-fading
        blended lists (P3D_SORTBLEND) is gone: the retail policies cover it.
      renderable.*  Renderable with the retail fields and flag names (typeMask/sceneId/uniqueId/
        elements/fade state), Renderable::Display per notes/renderspine.md §2.2 (distance ref pos with
        the WorldGeo override, min/max/fade band, frustum test against the CULLING camera, fade amount
        driving SetFading/SetFadeAmount), Tick/Update/Display/Hide/SetMatrix/GetPosition/
        GetDistanceRefPos/SetFadeDist(a TIME)/UpdateFade, and DisplayListPrimitive with the
        edge-triggered Display(bool) / SetVisible(bool) / RemoveFromList semantics.
      render_manager.*  RenderManager (4 scenes, 2 canvases, GetHeap stub, deferred-destroy queue),
        Scene/GamePlayScene, Canvas (fog + UpdateFog), RenderableHandle (weak ref + uniqueId check).
      view.*  Camera (position + the six frustum planes) and the culling/rendering camera pair;
        p3dview drives the frame through RenderManager::Update -> DestroyPendingRenderables -> Render.
      worldgeo.*  WorldGeoRenderable::Display 0x471640 is implemented: details_/cbvlitdecals_/
        skyline_/shells_/underwater_ geo does NOT use the base Display but culls and fades every
        sub-drawable of its composite on its own, through primitives[]/poseIDs[] and the composite's
        pose matrix table (notes/renderspine.md §4.5 has the reversed loop). Its band comes from
        three globals keyed on the kind (details 120, shells 1500, skyline 3000 at the highest of
        the three retail "DrawDistance" settings), not from the zone package.
    One deliberate deviation is left, marked in the code: node matrices are native and the viewer's
    x flip is re-applied by the list walks (which is exactly the mechanism RenderReflection uses).
    The base Renderable::Display also still measures to the element's bounding SPHERE instead of the
    reference point, which only matters for plain (unprefixed) world geo now.
    Debug envs: P3D_ONLYMODEL=<substr> (render only those instance models), P3D_HIDELIST=a,b,c (hide display
    lists), P3D_DEBUGMODEL=<modelname> (print placements).
  - renderer/sky.*: 0x08800002 SkyLoader -> SkyRenderable (notes/sky.md). Layers 28 (billboard
    quad groups -> list 76) / 38 / 39, a 2x scale, no distance test; Display_List::RenderSky and
    RenderCameraLocked76 translate their lists to the rendering camera like retail. billboard.*:
    the 0x00017005/6/7/9/a/b/c/d BillboardQuad(Group)/BillboardObject loader, a camera-facing
    quad stream through pddi, and the cut-off cones (BillboardQuad::Calculate, retail 0x696f80 +
    0x695a90) that fade a lens flare as it leaves the middle of the screen.
  - anim.* + the frame controllers: the generic pure3d::Animation (0x00121000, all nine channel
    types) and 0x00121201, with one controller per type: LITE (light.*), BQG (billboard.*),
    VRTX (geometry.*) and PTRN (anim.*, the composite's pose). SkyRenderable::Update plays every
    frame controller of the sky composite at numFrames*phase and then Hide()s, as retail does:
    PTRN_sky turns the "sun_grp" joint, which is what carries the sun, its flares and the moon
    across the sky (its rest pose is 755 m UNDERGROUND), BQG_sunShape colours the sun over the
    day, BQG_p3dBillboardQuadGroupShape3 hides the moon between 07:00 and 19:55, and the VRTX_*
    ones pick the sky box colour set through an INT channel of morph frame indices (NOT linearly
    in the phase). One clock for the lot: renderer::LightManager::timeOfDay (P3D_TIME in hours,
    P3D_TIMEOFDAY as a 0..1 fraction), so sky, sun and lights move together.
  - geometry.cpp/billboard.cpp read the 0x00122000 container sort key (retail 0x69ca7a, clamped
    to [0,1]). List 46 is sorted by it, so the horizon gradient and the two cloud layers are
    painted over the dome instead of under it; without it the sky is one flat colour.
  - primgroup.cpp: NORMALLIST was gated on PDDI_V_POSITION instead of PDDI_V_NORMAL (latent nil deref).
    Prim groups with no index list are drawn with glDrawArrays (the sky boxes are bare triangle
    strips), vertex-animated groups load their rest pose instead of being skipped, and an unlit
    pddi shader is no longer modulated with the ambient light.
  - p3dview/explorer.cpp: imgui Explorer window — Files tab (every loaded inventory, objects grouped by class),
    Renderables tab (visibility checkboxes), World tab (the stream graph: regions > subzones > packages,
    '>' = camera inside, go / pin / glb buttons; glb runs re/p3d2gltf.py --zone/--region in the background
    into out/), Selection tab (per-class details: world geo flags/draw distances,
    zone package members, instance placements with jump, composite primitives, container elements with layer
    and shader, shader state), View tab (camera, instance cull, display-list and shader toggles).
    ctrl+click in the view picks the nearest object (ray vs bounding spheres), selection is outlined in red,
    P3D_SELECT=<renderable name> selects at startup. View tab render options (pddiDebug in pddi.h, honoured by
    the GL shaders): no textures, no lighting, no vertex colours, wireframe; P3D_DEBUGRENDER=notex,nolight,novcol,wire.
  - p3dview: P3D_SKYHIDE=a,b,c hides sky composite elements by name (debugging), P3D_NOSKYFOG=1 keeps the
    below-horizon hemisphere's own colour. 'e' hides/shows the imgui windows (clean screenshots), P3D_GUI=0 starts hidden.
    P3D_SHOT=file.png [P3D_SHOTFRAME=n] screenshot-and-quit, P3D_CAMPOS/P3D_CAMTARGET="x y z",
    P3D_CAMPOS2="x y z" (jump there half way through a P3D_SHOT run, to exercise streaming),
    P3D_VERBOSE=1 prints per-world-geo distance decisions, unresolved models and package loads/unloads.
  - p3dview/streaming.cpp + renderer/streamgraph.*: the viewer streams the map the way the game does.
    renderer::StreamTriggerLoader reads the 0x08800101 triggers of assets/art/levels/z04/streamgraph.p3d
    (notes/streaming.md); every frame the triggers containing the camera (native coordinates) are looked up,
    their Shell + Detail packages are loaded (one per frame, 10-20 ms each; everything at once on the first
    frame), and a package no trigger has asked for in 3 s is unloaded (SetVisible(false) withdraws its display
    list nodes, Scene::RemoveRenderable, inventory released). Outside every trigger the nearest one is used.
    The region/global libraries stay resident. View tab > Streaming shows the current triggers and the
    resident packages, with the delay and loads-per-frame knobs. P3D_STREAM=0 (or no streamgraph.p3d) loads
    the whole static list instead, as before.
  - renderer/render_manager.* EnvManager + the pddi fog API: distance fog with the game's own
    per-time-of-day values (notes/fog.md). The twelve environment_{clear,rainy}_{4,9,12,18,21,24}
    EnvironmentObjects of scriptc/graphanims.cso and the TODObject of packages/z04/miami_lod.p3d;
    linear per-pixel fog by eye-space depth, exactly the four D3D states d3dContext::SetFog writes,
    off for the sky/camera-locked/night-light/decal lists as in retail. "FogDensity" never existed:
    Canvas+0x24 is EnvironmentObject::FogClamp and reaches no D3D state at all.
    notes/fog.md §1 also documents the .cso (compiled Torque script) format and the stx######## name
    hashing, which makes every script in cement.rcf readable.
  - light.* + renderer/lighting.*: the map is lit with the game's own lights (notes/lighting.md).
    pure3d::Light (0x13000 + the DIRECTION/POSITION/CONE/SHADOW/DECAY_RANGE children), LightGroup
    (0x2380), the LITE time-of-day animation (0x121000, channels 'CLR\0'/'DIR\0' -- Scarface pads the
    three-letter four-CCs with NUL where SHR uses a space) and LightAnimationController (0x121201),
    plus renderer::SFLightGroupLoader/LightingRenderable (0x08800007) and renderer::LightManager
    (retail g[0x8111cc]): groups filed by kind (0 zone_lights, 1 zone_rainlights, 2 exterior, 3
    interior, 4 template), the zone group's controllers played at a settable hour, the local lights
    picked by the retail rule (has a decay range, decay at the camera != 0, within 50 m), the
    building ambient excluded as retail does, and the result pushed through a new pddiContext light
    API (SetAmbientLight/SetLight/EnableLight, gl shader: ambient + 4 directional/point lights).
    At noon the sun is 99,97,72 from (-0.47,-0.74,0.47) and the ambient 63,52,31 -- within a few
    units of the values aap had eyeballed into glShader::SetPass. No pure3d::LightsChooser: one
    light set per frame chosen at the camera, not four directional lights per lit object.
    View tab > Lighting; P3D_TIME=<hours>, P3D_RAIN=1, P3D_NOGAMELIGHTS=1.

SHR full source tree (extracted 2026-09-15): /u/aap/fun/ps2engines/extracted/shr  (code/ + libs/, 1.7 GB, 16k files)
  libs/radcontent/src/radload   = ancestor of content:: (inventory.cpp, manager.cpp, request.cpp, stream.cpp, hashtable.cpp)
  libs/radcore/inc/radkey.hpp   = radMakeCaseInsensitiveKey32 == the RCF name hash (h*31 + blind tolower); Scarface's GetHash (x65599, 31-bit mask, top bit) is newer
  libs/radcore/src/radfile/common/cementer.cpp, cementLibrary.hpp = RCF reader
  code/render/{DSG,RenderManager,Loaders,Culling} = ancestors of renderer::*Renderable and the render layers
  code/stateprop = SHR StatePropData loader (v1 of the 0x0802000d family, see notes/statepropdata.md)
  libs/sim/simcollision = collision chunk 0x0701xxxx loaders
