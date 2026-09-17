# How the world is organised — the streaming graph

Verified 2026-09-16 from `art\levels\z04\streamgraph.p3d` (in cement.rcf, *not* under
`packages\`), the retail image (`StreamTriggerLoader`, chunk 0x08800101) and the leaked
game layer (`load/loadpackage.cpp`, `load/subzonetrigger.cpp`, `hud/HUDDebugInfo.cpp`).
Tool: `python3 re/streamgraph.py [triggers|zones|regions|at X Z|json]`.

## 1. The short version

The map is not one thing. It is a set of **packages** (the `.p3d` files) that the engine's
`StreamManager` swaps in and out of fixed **slots** while the player moves through
**polygon triggers**. A package on its own is meaningless: `sbeach_region.p3d` is a shader
library, `sbeachn_01_shell.p3d` is a piece of street, `sbeach_swanSong_dzone.p3d` is the
furniture of one shop. The only complete description of "what is at place P" is the
trigger that contains P, and those triggers live in `streamgraph.p3d` (297 of them), not in
any script.

```
slot          what goes in it                                   examples
Global_S/D    the city-wide backdrop + its eco-prop library      miami_lod / miami_lod_d, islands_lod / islands_lod_d
Region_S/D    a region's shaders+textures / eco-prop models      sbeach_region / sbeach_region_d, havana_region ...
Shell         a subzone's streets, sea floor, big buildings     sbeachn_01_shell, havana_02_shell, lobst_03_shell
Detail        the small stuff on top of a shell                 sbeachn_01_detail, havana_01_detailB, cgrove_01_detailC
ZoneAnimation packages/ZoneAnimations/<x>_anim                  bridge01_anim, industrial_anim
```

Each trigger (chunk 0x08800101) says: "while the player is inside this polygon, these
11 packages must be resident, in these slots". Triggers overlap; the union of what the
overlapping triggers ask for is what is loaded. There are usually 3-6 shells and 3-8
details resident at once, i.e. the subzone you stand in plus its neighbours.

The **subzone tag** (`bsand_02_shell`, `havana_01_shell`, ...) is the name the debug HUD
prints as `Subzone: <tag> / <region> / miami_lod` — the prerequisite chain of the shell's
StreamPackage.

Interiors are separate systems on top of that:

* **dzones** (`packages/dzones/*_dzone.p3d`, 135): the props/state props/collision of a
  business or mission interior; no world geometry (the walls are in the shell). Loaded by
  the polygon triggers in `scriptc/missions/z04/dzone_triggers.dso` (`LoadDzone('...')`),
  grouped `sbeach_dzTriggers`, `nbeach_dzTriggers`, `havana_dzTriggers`, `cgrove_dzTriggers`...
  The `01owned/02damaged/03destroyed/04condemned` variants are the business states.
* **azones / pockets** (`packages/azones/*_pocket.p3d`, 32): small interiors *with* world
  geometry (shops, the pawn shop, the bank...), loaded via `azone_triggers.dso`
  (`LoadAzone('...')`) into the `region_s` slot (`LoadPackage::ApplyChanges`: a pocket may
  go in the `dzone` or the `region_s` slot). "Interior" undersells them: 46 world geos live
  in the 32 pockets and a good deal of that is **outdoors** — see §3.1, and mind that
  nothing in `streamgraph.p3d` mentions them.
* **conditional subzones**: `tonymansion_01_shell_TS0..TS3` (the mansion as it is rebuilt),
  `cargoShip_01_shell_CS0/CS1`; retail picks one with `ChooseConditionalSubzone`.

## 2. The regions and where they are (native Pure3D X/Z; p3dview shows -X)

`python3 re/p3d2gltf.py --list` prints the live version of this table.

| region library | HUD district (`REGIONALHUDMAPID`) | subzone packages | where (x, z) |
|---|---|---|---|
| `nbeach` | RG_NORTHBEACH | `sbeachn_01..05`, luxuryhotel/penthouse, luxurybank_01, gentsclub, clounge, leopard | 570..1700, -970..380 |
| `sbeach` | RG_SOUTHBEACH | `sbeachs_01..04`, trailerpark, bridge_01..03, barge | 200..900, -1950..-1250 (bridges to -100, -1200) |
| `havana` | RG_LITTLEHAVANA | `havana_01..03`, stripmall, babylonclub, fidelrecords, diazmotors, drivein, shadygrove | -2300..-1100, -1130..330 |
| `downtown` | RG_DOWNTOWN | `cgrove_01..02`, construction, luxurybank_02, uginbar, marina | -1650..-500, -430..840 |
| `industrial` | RG_INDUSTRIAL | `ind_01..03`, indchopshop, railyard | -400..270, -150..750 |
| `tonyisland` / `tonymansion` | RG_TONYISLAND / RG_TONYMANSION | `tonyisland_01`, `tonymansion_01..05`, tonyOffice, boathouse | -2450..-1980, -1950..-1450 |
| `lobst` | RG_LOBSTERCAY | `lobst_01..06`, DevilsCay_01..03, sandbar | -2750..-1150, -10200..-8500 |
| `fountainrock` / `bsandtanker` | RG_FOUNTAINROCK / RG_TANKER | `FountainRock_01..03`, fountainCave, `bsand_01..06`, cargoShip | -5800..-3000, -10750..-7250 |
| `tranq` | RG_TRANQUILANDIA | `tranq_01..03`, sandbar_02 | -2350..-1250, -7000..-6100 |
| `bolivia` | RG_BOLIVIA / RG_SOSAMANSION | `sosaIsland_01`, `sosaMansion_01..02` | around -12000, 0 |
| (tutorial) | RG_TUTORIAL | `tutorial_01` | -9370, -8070 |
| `miami_lod` / `islands_lod` | — | the Global_S backdrops (type 3 low-LOD world geo, 11.7 km × 17 km) | |

So the game's "North Beach" is the `sbeachn_*` packages and "South Beach" the
`sbeachs_*` ones; "Downtown" is mostly the `cgrove_*` packages; `downtown_01_*`,
`sosa_01_*`, `swamp_01_*`, `sleepingMary_*`, `babylonclub_02_*`, `cgrove_03_*`,
`sbeachn_06_*`, `shippingLane_01_shell` are 200-byte stubs (cut content, still declared in
`scriptc/templates/zones_miami.cso`).

Region libraries hold no world geometry: `*_region.p3d` = shaders + textures (`havana_region`
160 shaders), `*_region_D.p3d` = the eco-prop meshes (`…InstanceShape`/`…LODShape`) the
subzones' placements reference (45-63 meshes each). `Common.p3d` = 371 global shaders +
57 meshes, the sky (0x08800002) and the light groups (0x08800007).

## 3. Chunk formats (streamgraph.p3d)

Strings here are *padded* pstrings: the length byte counts the NUL padding up to a 4-byte
multiple (`0c "loadTrigger\0"`, `10 "bsand_02_shell\0\0"`).

```
0x08800100  StreamPackage declaration (216 of them)
    pstring  name           lower case, without .p3d ("babylonclub_01_detail")
    u32      ?              (5, 8, 27, 53 ... 1198, 65535; per-package, not a slot id)

0x08800101  StreamTrigger (297; StreamTriggerLoader vtable 0x73be44, ctor 0x4b9440 stores the id)
    pstring  name           "loadTrigger", "loadTrigger_100" ...
    pstring  tag            the subzone this trigger belongs to ("bsand_02_shell")
    f32      height         prism height (13 for the Miami street level triggers)
    u32      numPoints
    f32[3]   points[numPoints]   x, y, z; a closed polygon, y is the base
    u32      numPackages
    pstring  package[numPackages]
    u32      numSlots       (== numPackages)
    pstring  slot[numSlots] "Global_S" "Global_D" "Region_S" "Region_D" "Shell" "Detail" "ZoneAnimation"
```

The leaked `SubZoneTriggerObject` (`load/subzonetrigger.cpp`, `#if 0`'d out) is the same
record as script attributes (`area`, `height`, `point0..N`, `AddLoadPackage(pkg, slot)`);
retail moved it into this chunk. The stream trigger polygons in `dzone_triggers.dso` /
`azone_triggers.dso` are the script-side equivalent for interiors.

## 3.1 The azone triggers, and why a viewer needs them [V]

The 25 azone triggers are **not** in any p3d: they are a compiled Torque script,
`scriptc/missions/z04/azone_triggers.dso` in `cement.rcf`. One does not have to interpret
the bytecode to get at them, because everything needed is in the file's **global string
table** (`u32 version | u32 globalStringTableSize | bytes …`, `re/notes/fog.md` "Reading
the compiled script"), in source order:

```
"AzoneTrigger5"                  the TriggerVolume's name  (sometimes a stx######## hash)
"997.0494204 0 -7.55970948"      point0 …
…                                (the hashed stx######## attribute names sit in between)
"LoadAzone('sbeachn_02_pocket');"
"UnloadAzone('sbeachn_02_pocket');"
```

So: walk the strings, collect the ones that are exactly three floats as polygon points
(x, y, z — y is 0 or -1 and unused), and when a `LoadAzone('<pkg>');` turns up, the points
collected so far are that pocket's polygon. Coordinates are native x/z, the same space the
`streamgraph.p3d` polygons use. 25 triggers, 4..30 points each; the seven pockets with no
trigger (`babylonclub_01`, `cargoShip_01`, `cargoShip_01_CS1`, `fidelrecords_01`,
`havana_02`, `pedropawn_owned`, `pedropawn_toast`) are loaded by mission scripts instead.

**Why it matters for rendering.** A pocket is not only a room. `sbeachn_02_pocket.p3d`
holds `details_sbn02p` + `skyline_sbn02p` (draw distances 600/500 in its ZonePkg record,
reference points 1159,-126 and 1130,-136), and that is the tiled **sidewalk, planters,
stairs, handrails and palms of the plaza in front of the North Beach bank** — outdoor
ground, right where the player walks. No `streamgraph.p3d` trigger asks for any of it, and
the surrounding `shells_sbn02s` / `details_sbn02s` meshes stop at its edge, so a renderer
that only follows the stream graph has a hole there: at native ≈(1197, 8, -171) you look
straight through the ground at the ocean plane and the low-LOD hull.

`AzoneTrigger5` (the `sbeachn_02_pocket` one) is a 19-point polygon spanning x 832..1384,
z -272..102, which contains the whole bank block. The pocket's world geo is `details_`
prefixed, so `WorldGeoRenderable::Display` culls it at the 120 m details band as usual —
i.e. from further away (or from outside the trigger) retail shows the same gap, filled by
the fogged low-LOD city.

## 4. What is inside a subzone package

From `p3d2gltf.py`'s census over `packages/z04` (world-space bounds, 2026-09-16):

* a `*_shell.p3d`: 1-7 `WorldGeoRenderable`s (0x08800003; the `shells_…` ones draw first,
  type 5/6/7/8 are the sea, decals, skyline...), a ZonePkg (0x08800004) with the draw
  distances, 100s of instance placements (0x09900190/94), occluders (0x0880000a in 31
  shells), a shadow (0x08800008), light group (0x08800007), collision (0x0701xxxx),
  script objects (`PlayerTriggerVolume`, mission triggers, `zone_setup.cso`).
* a `*_detail.p3d` / `_detailB` / `_detailC`: 1-3 more world geos and more placements;
  the split is a streaming budget thing (each package ≈ 30-45 k vertices).
* the whole of z04 (outdoors): 397 world geos, 45 698 placements of 426 models.

The bounds in the census show the shells at 100-1500 m across, the Miami land mass about
4 km × 3.5 km, the islands 9 km to the south.
