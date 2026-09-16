# How much of Scarface PC do we have source for, and could it be built again?

Generated 2026-09-15 by `re/coverage.py` + `re/coverage_report.py`.
Per-function table: `re/coverage_funcs.txt` (`addr size bucket subsystem class how`).
Raw tables: `re/notes/coverage_tables.txt`.

---

## 0. Headline

Of **3,226,284 bytes of code in 24,258 functions** in the unpacked retail image
(`Scarface.exe`, PC 2006):

| bucket | bytes | % of exe | functions | meaning |
|---|---:|---:|---:|---|
| **LEAK** | 1,380,614 | **42.8 %** | 10,976 | class is defined in `scarface_src` (the `code/gameobject` layer) |
| **MISSING-ENGINE** | 1,001,464 | **31.0 %** | 7,245 | `code/engine`, `code/game`, and ATG libs with no source anywhere |
| **SHR-DERIVED** | 449,594 | **13.9 %** | 2,956 | `pure3d::` / `core::` / `content::` / `math` / `container` / `movie` — a 2003 ancestor exists in the SHR tree |
| **THIRD-PARTY** | 394,612 | **12.2 %** | 3,081 | static CRT/STL, ActiveMark(SecuROM) wrapper, libpng 1.2.8 + zlib 1.2.1 |

Dropping third-party, i.e. **of the 2,831,672 bytes that are actually Radical's game**:

* **48.8 % we have** (leak),
* **15.9 % we have an ancestor of** (SHR 2003),
* **35.4 % we have nothing for** and would have to reverse.

Independent cross-check by class count rather than bytes: of the **2,016 distinct
RTTI vtable classes**, 961 (47.7 %) are LEAK, 771 (38.2 %) MISSING, 269 (13.3 %)
SHR-DERIVED, 15 third-party. The byte split and the class split agree to ~1 point,
and both agree with aap's earlier quick census (549/1220 ≈ 45 %).

**One-line answer:** the leak is *most* of the game's behaviour and *none* of its
engine. You have roughly half the shipped code and the half you are missing is the
half everything else depends on.

---

## 1. Method

`re/coverage.py` attributes every function in `idb_funcs.txt` to a C++ class, then
`re/coverage_report.py` buckets classes. Evidence, in confidence order:

1. **vtable slots.** 2,153 `??_7X@@6B@` symbols; each is walked dword-by-dword
   until a non-function-start or the next named address. A function appearing in
   several vtables is charged to the shallowest class (fewest bases per `??_R3`).
   → 502 KB.
2. **symbol names.** Only 308 functions carry a mangled name and most are FLIRT
   junk: IDA labels 86 game destructors `??1stdiobuf@@UAE@XZ_N` (they actually
   write game vtables, see `0x410d50`). Names occurring more than once are dropped.
   Plus `idb_usernames.txt` (aap's 1,296 hand names). → 68 KB.
3. **ctor/dtor:** function containing a data xref to a vtable address → that class
   (deepest, since a ctor writes its own vtable). → 298 KB.
4. **call-graph propagation**, 3 rounds: an unattributed function whose attributed
   callers all name one class inherits it. → 911 KB.
5. **contiguity**: MSVC keeps each `.obj` contiguous, so unattributed functions
   *between* two functions of the same class (or same namespace) join them. → 253 KB.
6. **linker layout**, last resort: the remaining functions take the module of their
   nearest attributed neighbour. → 776 KB, marked `layout`/`layout-weak`.

Third-party is identified structurally, not by propagation: the whole
`0x860000–0x9ce000` segment is the ActiveMark wrapper (`AmException`,
`AmContentSoftware::KillException`, `amsdk.dll`) plus the static CRT/STL/EH
(`??_R0?AVbad_cast@std@@@8`, `CatchIt`, `_Locimp`); libpng/zlib are located by
their own error strings and taken as an address hull.

**Evidence strength over the whole image:** strong 44.6 %, medium (call-graph)
30.5 %, weak (layout only) 24.9 %. Per bucket the weak share is similar
(LEAK 29 %, MISSING 30 %, SHR 21 %), so the weak evidence is not systematically
biasing one bucket.

### The tree layout is confirmed by the binary

Twelve `__FILE__` strings survived in retail error paths and pin the source tree:

```
\scarface\code\gameobject\hud\SystemDialog\SystemDialogBox.cpp   <- the leak
\scarface\code\engine\memory\gamememoryallocator.cpp             <- missing
\scarface\code\engine\resource\src\drivemanager.cpp
\scarface\code\engine\savegame\SaveGameObject.cpp
\scarface\sdks\atg\runtime\code\pure3d\texture\src\imageconverter.cpp
\scarface\sdks\atg\runtime\code\pure3d\platform\win32\src\platform.cpp
\scarface\sdks\atg\runtime\code\ravenphysics\simulation\src\impulsesolver.cpp
\scarface\sdks\atg\runtime\code\audio\src\hal\device\win32\halwin32_software.cpp
\scarface\sdks\atg\runtime\code\movie\src\common\binkfile.cpp
```

So the build is `code/{gameobject,engine,game}` over `sdks/atg/runtime/code/{pure3d,
ravenphysics,audio,movie,core,container,math,content}`. The leak is exactly one of
those eight-ish directories.

---

## 2. Per-subsystem table (top 40 by bytes)

| bucket | subsystem | bytes | % exe | funcs |
|---|---|---:|---:|---:|
| SHR-DERIVED | atg/pure3d | 387,606 | 12.0 % | 2,270 |
| THIRD-PARTY | CRT+STL+ActiveMark | 340,993 | 10.6 % | 2,881 |
| LEAK | gameobject/ai | 256,003 | 7.9 % | 2,427 |
| LEAK | gameobject/hud | 235,073 | 7.3 % | 1,694 |
| LEAK | gameobject/character | 213,864 | 6.6 % | 1,224 |
| MISSING | **engine/render** (`renderer::`) | 133,150 | 4.1 % | 735 |
| LEAK | gameobject/aivehicle | 115,463 | 3.6 % | 517 |
| LEAK | gameobject/vehicle | 102,523 | 3.2 % | 782 |
| LEAK | gameobject/template | 94,852 | 2.9 % | 1,163 |
| LEAK | gameobject/spawnobject | 72,570 | 2.2 % | 612 |
| MISSING | **code/game** (CVManager, PresentationManager) | 71,541 | 2.2 % | 484 |
| MISSING | **lib/fight** (`fight::`, fight-tree runtime) | 71,534 | 2.2 % | 804 |
| MISSING | **engine/collision** | 70,679 | 2.2 % | 377 |
| LEAK | gameobject/stateprop | 67,191 | 2.1 % | 531 |
| MISSING | **atg/ravenphysics** | 64,549 | 2.0 % | 186 |
| LEAK | gameobject/fighttree | 63,536 | 2.0 % | 650 |
| MISSING | **engine/vehicle** (Locomotion) | 59,395 | 1.8 % | 335 |
| MISSING | **atg/audio** | 54,200 | 1.7 % | 601 |
| MISSING | engine/UNCLASSIFIED | 51,459 | 1.6 % | 448 |
| THIRD-PARTY | libpng+zlib | 50,595 | 1.6 % | 132 |
| MISSING | **engine/sound** | 49,555 | 1.5 % | 381 |
| SHR-DERIVED | atg/core | 37,179 | 1.2 % | 437 |
| MISSING | **engine/om** (reflection) | 35,702 | 1.1 % | 221 |
| LEAK | gameobject/camera | 31,524 | 1.0 % | 178 |
| MISSING | **engine/resource** | 31,150 | 1.0 % | 192 |
| MISSING | **engine/database** (DataBroker) | 31,059 | 1.0 % | 141 |
| MISSING | **engine/controller** | 30,261 | 0.9 % | 207 |
| MISSING | **engine/stream** | 26,193 | 0.8 % | 161 |
| LEAK | gameobject/render | 24,841 | 0.8 % | 210 |
| LEAK | gameobject/worldgameplay | 24,616 | 0.8 % | 173 |
| LEAK | gameobject/pause | 24,281 | 0.8 % | 246 |
| MISSING | **engine/object** (GameObject/Set/Group) | 23,966 | 0.7 % | 335 |
| MISSING | **engine/character** | 23,819 | 0.7 % | 184 |
| MISSING | **engine/script** (Torque VM) | 23,377 | 0.7 % | 330 |
| MISSING | **engine/pathfind** | 23,196 | 0.7 % | 81 |
| MISSING | **engine/nis** | 21,107 | 0.7 % | 162 |
| MISSING | **engine/stateprop** | 18,847 | 0.6 % | 112 |
| MISSING | **engine/flow** | 17,799 | 0.6 % | 147 |
| MISSING | **engine/frontend** | 15,546 | 0.5 % | 203 |
| SHR-DERIVED | atg/movie (Bink) | 5,968 | 0.2 % | 55 |

The 25 largest missing subsystems in order: `engine/render`, `game`, `lib/fight`,
`engine/collision`, `atg/ravenphysics`, `engine/vehicle`, `atg/audio`,
`engine/sound`, `engine/om`, `engine/resource`, `engine/database`,
`engine/controller`, `engine/stream`, `engine/object`, `engine/character`,
`engine/script`, `engine/pathfind`, `engine/nis`, `engine/stateprop`,
`engine/flow`, `engine/frontend`, `engine/subtitle`, `engine/memory`,
`engine/trigger`, `engine/weapon`.

Biggest individual missing classes: `CVManager` (30 KB, `game/cvmanager.hpp`),
`BoatLocomotion` (20 KB), `renderer::RenderFlowClient` (19 KB),
`SoundManager::SoundFlowClient` (17 KB), `PathManager::PathManagerFlowClient`
(17 KB), `ScriptFileHandler` (16 KB), `CollisionManager::CollisionManagerFlowClient`
(16 KB), `renderer::GamePlayScene` (13 KB), `ravenphysics::BroadPhaseDetectorSweepAndPrune`
(12 KB), `om::MetaAttribScalar<T>` (12 KB), `AirplaneLocomotion` (11 KB),
`NISManager` (11 KB), `StreamManagerFlowClient` (11 KB), `DataBroker` (11 KB),
`renderer::Display_List` (10 KB), `AnimationAction` (10 KB).

---

## 3. What the leak's `#include` list demands

5,693 include directives in `scarface_src`; 3,088 of them (54 %) name a header we
do not have. **307 distinct missing headers**, by family:

| family | distinct headers | include sites |
|---|---:|---:|
| `engine/` | 205 | 2,514 |
| `pure3d/` | 33 | 127 |
| `game/` | 14 | 147 |
| `container/` | 11 | 58 |
| `ravenphysics/` | 10 | 45 |
| `math/` | 9 | 107 |
| `core/` | 8 | 62 |
| `fight/` | 7 | 13 |
| `content/` | 4 | 8 |
| `script/`, `object/`, `audio/` | 6 | 7 |

`engine/` by subdir (include sites): script 418, object 333, util 261, assert 175,
collision 131, character 115, frontend 101, database 87, overlay 86, vehicle 85,
sound 84, flow 84, render 81, memory 72, aigadgets 64, controller 46, pathfind 43,
stream 42, resource 42, debug 41, stateprop 33, nis 18, trigger 12, fighttree 11.

Top 20 single headers to recreate, by how many leak files break without them:

```
147 engine/assert/assertmsg.hpp          42 engine/memory/memoryblockallocator.hpp
145 engine/script/scriptinterface.hpp    39 engine/object/gameset.hpp
108 engine/util/stringutil.hpp           38 engine/character/character.hpp
105 engine/object/scriptobject.hpp       37 engine/aigadgets/aiMacros.hpp
 95 engine/script/jdscript/console.h     34 container/arraydynamic.hpp
 90 engine/script/registeredfunction.hpp 33 engine/resource/resourcemanager.hpp
 87 engine/util/scriptobjectpointer.hpp  33 engine/database/dbutil.hpp
 86 engine/overlay/overlaymanager.hpp    31 engine/util/mathutil.hpp
 73 engine/sound/soundmanager.hpp        30 engine/memory/memorymanager.hpp
 68 game/cvmanager.hpp                   29 engine/script/script.hpp
 68 engine/frontend/fegameobject.hpp     29 engine/object/template.hpp
 64 engine/render/renderer.hpp           29 engine/object/gamesimobject.hpp
 63 engine/collision/collisionmanager.hpp 28 engine/assert/programmingbycontract.hpp
 59 engine/flow/flowmanager.hpp          28 game/presentationmanager.hpp
 56 engine/object/gamegroup.hpp          26 engine/pathfind/pathmanager.hpp
 45 math/vector.hpp                      26 engine/collision/rayintersectionresult.hpp
```

Distinct symbols the leak uses from each missing namespace: `renderer::` 105,
`math::` 71, `pure3d::` 41, `core::` 32, `fight::` 18, `ravenphysics::` 16,
`Con::` 15, `container::` 10, `content::` 5, `om::` 3. Plus ~255 forward-declared
global classes that the leak never defines.

---

## 4. The single most useful discovery: the script layer is Torque

`engine/script/` is a fork of **Torque Game Engine 1.x**, whose source is public
(and Torque3D's MIT release contains the same console). Evidence, all from the
retail exe and the leak:

* leak includes `engine/script/T2Script/consoleobject.h`,
  `engine/script/jdscript/{console.h,consoleobject.h,stringTable.h,stringtableentry.h}`;
* exe RTTI: `.?AVConsoleObject@@`, `.?AVAbstractClassRep@@`,
  `.?AV?$ConcreteClassRep@VGameSimObject@@@@`, `.?AVGameSimObject@@`,
  `.?AV?$SemiConcreteClassRep@…@@`;
* exe strings: `$Con::Prompt`, `Con::printLevel`, `Con::logBufferEnabled`, `.dso`,
  `%s: Unable to instantiate non-SimObject class %s.`,
  `Error: classLinkNamespaces() - %s is already a child of %s - circular link error!`;
* leak uses `ConsoleFunction(name, retType, minArgs, maxArgs, usage)` 162× and
  `ConsoleMethod` 407× — Torque's exact macro signatures — plus `Con::evaluatef`
  339×, `Con::evaluate` 299×, `Con::executef` 141×.

`ConcreteClassRep<T>` alone has **361 distinct instantiations** in the exe's RTTI
and accounts for **91,271 bytes in 1,228 functions**; those are template
instantiations that the compiler regenerates for free once the header exists, so
they are charged to `T` in the table above, not to `engine/script`.

`engine/object/{gameobject,gamesimobject,gamegroup,gameset,gamesetobject,template,
scriptobject}.hpp` are almost certainly Radical's renames of Torque's
`SimObject`/`SimGroup`/`SimSet`/`SimObjectPtr` family, so Torque covers that too.

---

## 5. Buildability assessment

### What "buildable" should mean
Not "reproduce `Scarface.exe`". That is out of reach: no build system in the leak,
no debug info in the exe, 35 % of the code has no source, and MSVC-2003-era
codegen is unreproducible anyway. The realistic target is
**"`code/gameobject` compiles and links against a reimplemented engine, and the
result runs"** — i.e. the same shape of project as an OpenRW / re3.

### Effort ranking of the missing pieces

**Tier 0 — mechanical, days.** Pure declaration work; almost no behaviour.
* `engine/assert/{assertmsg,programmingbycontract}.hpp` (175 include sites, ~5
  macros: `rAssert`, `rAssertMsg`, `rWarning`, `rTuneAssert`). SHR's
  `radcore/inc/raddebug.hpp` has the same macro family.
* `container/{arraydynamic,arrayfixed,deque,bitfield,array,set,map,list,
  slistbase,allocator}.hpp` — STL-shaped; write thin wrappers over `std::`.
* `engine/util/{stringutil,mathutil,choreoutil}.hpp`.
* `core/types.hpp`, `core/debug.hpp`.

**Tier 1 — lift from SHR with renames, weeks.** `notes/shr_vs_scarface.md` §2 is
already the rename table.
* `math/` ← `libs/radmath/radmath/{vector,matrix,quaternion,trig,random,util,
  spline,geometry}.hpp`. 71 symbols used; essentially a rename.
* `core/{time,memory,string,key}.hpp` ← `radcore/inc/{radtime,radmemory,radstring,
  radkey}.hpp`. Note `core::GetHash` is *not* `radMakeCaseInsensitiveKey32` — it is
  x65599 with a 31-bit mask; already solved in this repo.
* `content/load/*` ← `radcontent/src/radload/*` (inventory, manager, request,
  stream, hashtable). aap already has a working `content::`-shaped loader.
* `pure3d/` (33 headers: camera, anim/skeleton, anim/pose, entity, refcounted,
  text/p3dstring, utility) ← SHR `libs/pure3d/p3d`. Refactored, not rewritten.
  This repo already reimplements a good chunk of it.
* `engine/controller/*` ← `radcore/inc/{radcontroller,directinputcontroller}.hpp`.

**Tier 2 — lift from Torque, weeks.** `engine/script/*` (~23 KB of exe code but
a very wide API: 418 include sites, 2,336 `REGISTER_MEMBER`, 407 `ConsoleMethod`)
and `engine/object/*` (24 KB, 333 include sites). The risk is not the VM, it is the
*Radical additions* — `REGISTER_MEMBER`/`REGISTER_METHOD`/`REGISTER_ENUM`/
`scriptobjectpointer`/`ScriptFileHandler` (16 KB) and the `.dso`/`0x09900190`
script-object chunk format, which aap has already partly decoded
(`re/p3dblock.py`, `renderer/instance.cpp`).

**Tier 3 — must be reversed, months each.** No ancestor, no substitute:
1. `renderer::` — 133 KB, 735 functions, ~70 `Renderable`/`Primitive` classes.
   The biggest single job, but the best documented: `notes/renderables.md`,
   `notes/architecture.md` §2.3–2.5, and working code in
   `renderer/{display_list,renderable,worldgeo,instance,zonepkg}.cpp`.
2. `engine/collision` + `atg/ravenphysics` — 135 KB together. Behaviour can be
   *substituted* (any modern physics lib) at the cost of not matching the game.
3. `lib/fight` — 72 KB. The melee fight-tree runtime; the leak has only the game
   glue (`fighttree/`, 64 KB) and needs `fight/{actioncontroller,node,branch,
   sequencer,paramvalue}.hpp`.
4. `engine/{sound,stream,resource,database,flow,nis,stateprop,frontend,overlay,
   pathfind,character,memory}` + `atg/audio` — ~330 KB of plumbing. Individually
   small, collectively the long tail. `engine/flow` (FlowClient/FlowManager) and
   `engine/stream` (StreamManager/StreamPackage/StreamSlot) are load-bearing:
   §3.4 of `notes/architecture.md` shows object lifetime *is* package residency.
5. `code/game` — `CVManager` (30 KB!), `PresentationManager`, `GameObjectManager`,
   `PlayerProfileManager`. 72 KB, and `cvmanager.hpp` alone has 68 include sites.
6. `engine/om` — 36 KB reflection system (`om::MetaType`, `om::MetaAttrib*`,
   `om::Entity`, `om::Stream`). Only 3 symbols used directly from the leak, so it
   can probably be replaced with something simpler rather than reproduced.

### Practical route

1. **Header-first, not code-first.** Generate the 307 missing headers as
   declarations only, mechanically, from how the leak uses them (a script over
   `scarface_src` can emit skeletons; the exe's vtables give the virtual-method
   counts and the RTTI gives the hierarchies). Getting `code/gameobject` to
   *parse* is the first real milestone and it tells you whether the leak is
   internally self-consistent — which is the single biggest unknown right now.
2. **Stub, then link.** Every stub aborts. A linking no-op binary proves the API
   surface is closed.
3. **Fill bottom-up:** assert/container/math/core → content + pure3d (this repo
   already has most of it) → script/object (Torque) → flow/stream/resource.
4. **Then the renderer**, which is where the existing p3dview work already lives —
   it is the one Tier-3 subsystem with a head start.
5. **Never chase byte-exactness.** Substitute ravenphysics, audio, AI tuning.

Rough scale: the leak is **271,150 lines across 925 real source files**
(550 `.cpp` + 375 `.hpp`; the "1903 files" figure counts the macOS AppleDouble
`._*` duplicates that came with the archive). The missing engine is
~35 % of the binary but the leak's dependency on it is far heavier than 35 % of
its API surface — the 307 headers are on the critical path of essentially every
translation unit. A plausible order of magnitude is 150–250 kLOC to write, of
which maybe a third can be lifted (SHR + Torque) and two thirds reversed.
That is a multi-year project for one person, and a tractable one for a small
group *given* how much of the data format is already pinned down in `notes/`.

---

## 6. Honest uncertainty

* **25 % of the bytes are attributed by linker adjacency alone.** Those are safe
  at module granularity (MSVC keeps `.obj`s contiguous and the map in
  `re/cov4.py` output is very clean) but unsafe at class granularity. Numbers at
  the *bucket* level are solid; numbers for any individual class are not.
* **"LEAK" means a class of that name is defined in `scarface_src`.** It does not
  prove the leaked revision is the revision that shipped. 62 % of the leak's 931
  defined classes appear in the exe's RTTI; the other 352 are mostly
  non-polymorphic or use the reflection system instead of C++ RTTI (the whole
  `ai/DecisionTree` family registers via `SemiConcreteClassRep`, and
  `DecisionTreeFileHandler`/`AIActionTreeCreatingTask` *are* in the exe), so
  absence is not evidence of divergence — but presence is not proof of identity
  either. Nobody has diffed a leak function against its retail body yet. **That
  is the highest-value next experiment:** pick five leak `.cpp` files with
  distinctive constants, find the retail functions, and check they match.
* **Inlining blurs the boundary.** A leak class's logic may be inlined into an
  engine caller and charged to the engine, and vice versa. Byte attribution
  cannot see this; expect a few points of error in both directions.
* **`ConcreteClassRep<X>` and friends are charged to `X`** (91,271 B). Defensible —
  the compiler emits them into `X`'s TU — but it shifts bytes from
  MISSING(engine/script) into LEAK. Charging them to `engine/script` instead
  moves LEAK 42.8 % → 40.0 % and MISSING 31.0 % → 33.9 %. That is the size of the
  largest single judgement call in this table.
* **`engine/UNCLASSIFIED` is 51 KB (1.6 %)** of MISSING that I could not place in
  a subdir; it is missing either way, so the bucket total is unaffected.
* The libpng/zlib hull is an address range, so a few neighbouring pure3d
  functions may be miscounted as third-party (bounded by ~10 KB).
* `atg/pure3d` is bucketed SHR-DERIVED wholesale at 388 KB. Per
  `notes/architecture.md`, the *pddi* half is close to 2003 and the *drawable*
  half was substantially reshaped, so "we have an ancestor" is doing real work in
  some places and very little in others. Treat 13.9 % as an upper bound on the
  help SHR gives.
