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

Implemented in the repo from these notes (2026-09-15)
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
  - renderer/display_list.cpp: retail depth-write states (SetZWrite(false) around lists 3,17,18,4,75,5,19,20,6,11,12;
    pddiContext::SetZWrite added) and a far-to-near depth sort of the fading lists retail sorts (51 54 65 66 70 71 83)
    plus the plain alpha-blended lists (which retail draws unsorted; P3D_SORTBLEND=0 to disable). Foliage shaders are
    blmd1/atst0 in retail too, so there is no alpha test to enable (notes/shaderstate.md).
    Debug envs: P3D_ONLYMODEL=<substr> (render only those instance models), P3D_HIDELIST=a,b,c (hide display
    lists), P3D_DEBUGMODEL=<modelname> (print placements).
  - primgroup.cpp: NORMALLIST was gated on PDDI_V_POSITION instead of PDDI_V_NORMAL (latent nil deref).
  - p3dview: P3D_SHOT=file.png [P3D_SHOTFRAME=n] screenshot-and-quit, P3D_CAMPOS/P3D_CAMTARGET="x y z",
    P3D_VERBOSE=1 prints per-world-geo distance decisions and unresolved models.

SHR full source tree (extracted 2026-09-15): /u/aap/fun/ps2engines/extracted/shr  (code/ + libs/, 1.7 GB, 16k files)
  libs/radcontent/src/radload   = ancestor of content:: (inventory.cpp, manager.cpp, request.cpp, stream.cpp, hashtable.cpp)
  libs/radcore/inc/radkey.hpp   = radMakeCaseInsensitiveKey32 == the RCF name hash (h*31 + blind tolower); Scarface's GetHash (x65599, 31-bit mask, top bit) is newer
  libs/radcore/src/radfile/common/cementer.cpp, cementLibrary.hpp = RCF reader
  code/render/{DSG,RenderManager,Loaders,Culling} = ancestors of renderer::*Renderable and the render layers
  code/stateprop = SHR StatePropData loader (v1 of the 0x0802000d family, see notes/statepropdata.md)
  libs/sim/simcollision = collision chunk 0x0701xxxx loaders
