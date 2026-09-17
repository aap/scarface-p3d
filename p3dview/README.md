# `p3dview` — how to run it

A viewer for the world of *Scarface: The World Is Yours* (PC). It loads the game's data
straight out of `cement.rcf`, the one archive the game ships everything in, so nothing has
to be unpacked first.

## Build

```sh
make -f Makefile.gl -j8         # the engine + renderer -> p3d_gl.a
make -C p3dview -j8             # the viewer (needs SDL2 and imgui; IMGUI_DIR in p3dview/Makefile)
```

## Keys

`e` hides and shows the imgui windows. `c` prints the camera as a line you can paste back
(`P3D_CAMPOS="…" P3D_CAMTARGET="…"   # native x y z  zones …`) and puts it on the clipboard.
ctrl+click picks an object. WASD moves, the mouse looks.

## Debugging aids

`P3D_PICK="x y[,x y…]"` picks at those pixels on the screenshot frame and prints **every**
hit along the ray, nearest first. The pick is triangle accurate: bounding spheres only
choose the candidates (with spheres alone the low-LOD city hull and each whole-city-block
`details_` composite win every pick), then the ray is intersected with the prim groups'
triangles through the matrix the display list node carries. Each line is `t`, the world
point, the mesh, the prim group and its shader, the owning renderable and, when it applies,
`sub N` (the sub-primitive of a world geo composite) or `loc N` (an eco-prop placement).
Hidden and culled geometry is tested too and marked `NOT-DRAWN` / `hidden` / `fading`, so
"there IS ground here, it is just not drawn" is distinguishable from "there is nothing
here"; the `*` marks the nearest hit that is actually being drawn, which is what a
ctrl+click selects. Under the hits come up to eight `miss sphere` lines — meshes the ray
passed through without hitting a triangle, i.e. the ones that surround a hole.

`P3D_DEBUGWG=<substr>` prints every sub-primitive decision of the
matching world geos for a few frames. `P3D_TEXDUMP=<dir>` writes every uploaded texture as PNG.
`P3D_SKYHIDE=a,b` hides sky elements, `P3D_HIDELIST=n,m` display lists.

## Run

```sh
cd p3dview
./p3dview -rcf /path/to/Scarface/cement.rcf
```

`cement.rcf` sits next to `Scarface.exe` in the installed game (a 1.5 GB "ATG CORE CEMENT
LIBRARY"; the retail PC disc, the Steam and the GOG builds all have the same one). The
viewer only reads it, and only the files it needs.

Where it looks, in order:

1. `-rcf <path>`
2. `$P3D_RCF`
3. `./cement.rcf`, `../cement.rcf`
4. no archive: the extracted tree under `../assets` (`packages/z04/*.p3d`,
   `packages/azones/*.p3d`, `art/levels/z04/streamgraph.p3d`,
   `scriptc/missions/z04/azone_triggers.dso`), e.g. from
   `python3 re/rcf.py <cement.rcf> extract assets packages\\z04`

The two are interchangeable — with the same archive and the same tree the viewer renders
the same frame. A file that is not in the archive is still looked up on disk, so a single
edited `.p3d` can be dropped into `../assets/packages/z04/` to override the archive's.
`P3D_VERBOSE=1` prints where every file came from (`packages/z04/Common.p3d <- …`).

The path inside the archive is what the game uses, `packages\z04\Common.p3d` and
`art\levels\z04\streamgraph.p3d`; names are looked up by the archive's own case-insensitive
hash, so spelling and `/` vs `\` do not matter (`rcf.h`, `re/notes/ps2.md` §1).

## Controls

| | |
|---|---|
| `W` `A` `S` `D` | fly: forward / left / back / right (the speed ramps up while held) |
| left drag | look around |
| ctrl + left drag | move forward/back |
| middle drag | pan |
| alt + middle drag | orbit the target |
| ctrl + middle drag | zoom |
| ctrl + left click | pick what is under the cursor (Explorer ▸ Selection) |
| `P` | print the camera position on stdout (for `P3D_CAMPOS`) |
| `E` | hide / show the Explorer windows (clean screenshots) |

The Explorer's **World** tab is the stream graph: regions, their subzones and the packages
each keeps resident, with `go` (jump there), `pin` (keep loaded) and `glb` (export with
`re/p3d2gltf.py`). **Files** lists what is loaded, package by package, **Renderables**
every renderable in the scene, and **View** the sky, fog, ocean and display-list switches.

## The azone pockets

The map does not come only out of `streamgraph.p3d`. The 32 `packages/azones/*_pocket.p3d`
are loaded by a mission script — `scriptc/missions/z04/azone_triggers.dso` has one trigger
polygon per pocket and calls `LoadAzone('<pocket>')` when the player crosses it — and they
hold 46 world geos, a lot of which is *outdoor* ground: the tiled sidewalk, planters,
stairs and palms in front of the North Beach bank are `details_sbn02p` in
`sbeachn_02_pocket.p3d`. The viewer runs no scripts, so it reads those polygons out of the
compiled script's global string table and streams the pockets like any other package
(`re/notes/streaming.md` §3.1); View ▸ Streaming lists the ones you are standing in, and
they show up in the resident set as `azones/<name>.p3d`.

`azone_triggers.dso` lives only in `cement.rcf`, so an extracted tree needs
`python3 re/rcf.py <cement.rcf> extract assets azone_triggers` (plus
`packages\azones`) — without it the viewer says so and leaves the pockets out, which
means holes in the ground where the pocket geometry should be.

## Environment

The whole app is scriptable through the environment, which is how the screenshots in
`screens/` are made.

| | |
|---|---|
| `P3D_RCF=<path>` | the cement archive (same as `-rcf`) |
| `P3D_VERBOSE=1` | where each file came from, what each package loaded, light/shape details |
| `P3D_GUI=0` | start with the Explorer hidden |
| `P3D_CAMPOS="x y z"`, `P3D_CAMTARGET="x y z"` | the start camera |
| `P3D_CAMPOS2="x y z"` | jump there half way through a `P3D_SHOT` run (exercises streaming) |
| `P3D_SHOT=file.png`, `P3D_SHOTFRAME=n` | save a screenshot after n frames, then quit |
| `P3D_STREAM=0` | no streaming: load the whole static package list instead |
| `P3D_TIME=<hours>` (`P3D_TIMEOFDAY=<0..1>`) | the point on the time-of-day curve (noon by default) |
| `P3D_RAIN=1` | the `zone_rainlights` light group |
| `P3D_NOGAMELIGHTS=1`, `P3D_NOFOG=1`, `P3D_NOSKYFOG=1` | keep the game's lights / the distance fog / the fogged sky horizon out of it |
| `P3D_FLARES=1`, `P3D_SKYHIDE=a,b,c` | lens flares on; hide sky elements by name substring |
| `P3D_SEALEVEL=<y>`, `P3D_NOWAVES=1`, `P3D_NODETAIL=1`, `P3D_OCEANGRID=1` | the ocean |
| `P3D_HIDELIST=a,b,c` | hide display lists by number (see `renderer/README.md` § The 84 lists) |
| `P3D_DEBUGRENDER=notex,nolight,novcol,wire`, `P3D_DEBUGOVERLAY=1`, `P3D_SELECT=<name>` | debug drawing, the overlay, and an object selected at startup |
| `P3D_DEBUGMODEL=<name>`, `P3D_ONLYMODEL=<name>` | restrict eco-prop instance rendering to one model |

Example (a scripted screenshot, no GUI, straight from the archive):

```sh
cd p3dview
P3D_RCF=~/games/Scarface/cement.rcf P3D_GUI=0 \
P3D_CAMPOS="1829 40 -627" P3D_CAMTARGET="1700 60 -800" \
P3D_SHOT=../screens/shot.png P3D_SHOTFRAME=20 ./p3dview
```

## What reads what

* `rcf.h` / `rcf.cpp` — the archive (`content::RCFArchive`, `content::MountRCF`) and the
  name resolution every load goes through (`content::OpenContentFile`: the mounted archive
  first, then loose files under the content roots). `'P3DZ'` entries are LZR-decompressed
  transparently, though no shipped archive has one.
* `p3dview/streaming.cpp` — the stream graph and the resident set; the package index (the
  graph's lower-case names -> real file names) comes from the archive's name table, or from
  `readdir` of the extracted tree.
* `renderer/README.md` — what actually draws the frame.
