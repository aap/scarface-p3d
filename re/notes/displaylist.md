# `renderer::Display_List` — the retained draw list, function by function

PC retail, addresses are unpacked-image VAs (`re/dis.sh`). Companion to `notes/renderspine.md`
(which covers `Renderable::Display`, the layer→list table and the frame spine) — this file covers
the *inside* of the Display_List translation unit, roughly `0x004589d0 .. 0x0045f3b0`.

**[V]** = read out of the disassembly. **[?]** = inferred, marked in the name files with a
trailing ` ?`.

---

## 0. Corrections to `notes/renderspine.md`

Four things in renderspine turn out to be wrong or incomplete; they matter for the reimplementation.

1. **`matrixStack->[+0x4c]` is `MultMatrix`, not `LoadMatrix`.** The "matrix stack" is not a
   separate object at all: `g[0x00831688]` is a `pddiRenderContext**` (a 4-byte wrapper built by
   `p3dContext::ctor` at `0x67e698`, `ctx+4`), and the slots are the ordinary pddi matrix API:

   | off | slot | name | proof |
   |---|---|---|---|
   | 0x3c | 15 | `IdentityMatrix(id)` | `0x658f00` |
   | 0x40 | 16 | `LoadMatrix(id, m)` | `0x658f30`, `rep movsd` 16 dwords |
   | 0x44 | 17 | `PushMatrix(id)` | `0x659fe0`, depth++ then copies top-1 → top |
   | 0x48 | 18 | `PopMatrix(id)` | `0x65a010` (IDB-named) |
   | **0x4c** | 19 | **`MultMatrix(id, m)`** | `0x658f70`, `Matrix::Multiply(tmp, m, top); top = tmp` |
   | 0x50 | 20 | `GetMatrix(id)` | `0x65a030` |
   | 0x5c/0x60/0x64 | 23/24/25 | `PushIdentityMatrix` / `PushLoadMatrix` / `PushMultMatrix` | `0x65a050` / `0x65a0b0` / `0x65a110` |

   This is why `0x459a00` can do `Push(0); Mult(0,T); Mult(0,M); Mult(0,node->matrix)` — it is a
   composition, not three overwrites. Everywhere in the render helpers the idiom is
   `PushMatrix(0); MultMatrix(0, &node->matrix); prim->Display(); PopMatrix(0);`, so the stack top
   is expected to be **identity** (the view matrix lives in matrix id 1/2, not 0).

2. **Almost every list is sorted every frame**, not just `51 54 65 66 70 71 83`. `0x45ad10` sorts
   **69 of the 84 lists** — everything except `15, 16, 23, 48, 57, 58, 60, 61, 62, 63, 64, 72, 73,
   74, 76` — with one of four policies (§5). `listNonEmpty[]` is really a **dirty flag**:
   `AddContainerElement` sets it, the sort pass sorts the list and **clears it again**, so an
   untouched list is not re-sorted. (Lists 69, 82, 70, 71, 83, 65 and 66 are sorted
   unconditionally, dirty or not, because their key depends on the camera.)

3. **Every list renderer frustum- and occluder-culls each node again at draw time**
   (`Display_List::IsNodeVisible`, `0x45b110`, called from 46 sites). The retained list is not
   "visible geometry"; it is "geometry that was visible when it was submitted".

4. **Lists never drawn on PC: 0, 1, 48, 57.** (renderspine said 1/48/57, and put 0 and 66 in the
   water pass. In fact list 66 *is* drawn, by `0x459f70` inside `Render()`, and list 65 by
   `0x459c80`. Lists 0 and 1 — the `FOAM` shader layer 29 — are sorted every frame and then never
   walked. Dead on PC.) Also list 4 *is* drawn, by `0x45b590`; renderspine had it right, but note
   list 4's byte offset is `0x30`, which collides with `this+0x30` in naive scans.

---

## 1. The retained model in one page

*(for a reader who knows OpenGL but not this engine)*

### What a frame looks like

```
GamePlayScene::Update(dt)                         once per frame, BEFORE rendering
   for every Renderable in the scene:
       Renderable::Display()                       decides visibility, NOT drawing
           per element: distance band -> frustum sphere -> occluders -> fade amount
           if visible and not already submitted:
               ctx->PushMatrix(0); ctx->MultMatrix(0, &renderable->matrix);
               DisplayListPrimitive::Display(true)      0x458f00
                   -> drawable->Display(g_displayList, this)
                       -> Display_List::AddContainer(container, m, prim)       vslot 7
                           -> Display_List::AddContainerElement(c, i, m, prim) vslot 8
               ctx->PopMatrix(0);
           if invisible and currently submitted:
               Display_List::RemovePrimitiveNodes(prim)   0x458a20
   Display_List::SortAllLists()                    0x45ad10   <- sort + occluder rebuild
GamePlayScene::Render()                            0x468aa0
   [Display_List::RenderReflection()]              0x45bb90   (mirrored sky + eco props)
   Display_List::Render()                          0x45e680   <- ~110 list walks, fixed order
       ...
       Display_List::FreeOrphanNodes()             0x45b0c0
```

### What persists across frames

A **node** (112 bytes, `Display_List::Node`) is a self-contained draw record:
`{ world matrix, PrimArrayEntry*, DrawableContainer*, Shader*, two sort keys, owner }`.
It is allocated once, at `Display_List` construction, into one flat array; the free list is
intrusive. Submitting = unlink from free list, link into `lists[listID]`. Un-submitting = the
reverse. **Nothing is allocated or freed per frame.**

A node stays in its list for as long as its owning `DisplayListPrimitive` says it is visible.
`DisplayListPrimitive::Display(bool)` (`0x458f00`) is edge-triggered:

```c
if (inList && !visible)  { RemovePrimitiveNodes(this); inList = false; }
if (flag4 && inList && !drawable->vslot9()) { RemovePrimitiveNodes(this); inList = false; }
if (visible && !inList)  { drawable->Display(g_displayList, this); inList = true; }
```

so a static, continuously-visible object is submitted **once, ever**, and every later frame just
walks the linked list. That is the whole point: at Scarface's scale (420 world-geo renderables /
27k prim groups / 47k instance placements per zone set) you cannot afford to rebuild draw lists.

The corollary is the thing that trips people up: **the world matrix is baked into the node at
submit time.** If a renderable moves, or its fade state changes, the node's cached matrix is stale,
so `Renderable::Display` calls `DisplayListPrimitive::RemoveFromList()` (`0x458ea0`) to force a
re-submit. That is what `flags81 & 0x02` (matrix dirty) and the `isFading` transitions are for.

### What is recomputed every frame

* `nd->sortKey2` (`+0x4c`) — view-space **z** of the primitive's bounding-sphere centre, for the
  lists that need depth order (`ComputeDepthKeys`, `0x459120`).
* the **order inside** each dirty list (`SortAllLists`, `0x45ad10`).
* per-node visibility at draw time (`IsNodeVisible`, `0x45b110`): sphere vs camera frustum, then
  AABB vs the occluder planes.
* the per-node fade value, read out of the *container* (`container->GetFadeAmount()`) and pushed
  into the *primitive* (`prim->SetFade()`) just before the draw and reset to 0 after.

### `parent == 0` — the immediate-mode escape hatch

`AddContainerElement`'s last argument is the owning `DisplayListPrimitive`. When it is non-null the
node is also linked into `prim->children` and lives until that prim is hidden. When it is **null**
the node is an **orphan**: it is drawn this frame and then recycled by `FreeOrphanNodes`
(`0x45b0c0`) at the very end of `Render()`. That is how immediate-mode callers (HUD, effects,
anything that calls `Drawable::Display(displayList, nil)`) share the same 84 buckets and the same
sorting machinery as the retained world.

### The child-link and the `self` pointer

Nodes are on two intrusive lists at once: `link` at `+0x00` (in `lists[id]`) and `linkParent` at
`+0x64` (in `prim->children`). `RemovePrimitiveNodes` walks `prim->children`, so it holds a pointer
to `node+0x64` and has to get back to `node` — hence **`node->self` at `+0x6c`**, written once in
the constructor (`0x45f33f: mov [eax+0x6c], eax`) and read at `0x458a40: mov esi, [edi+8]`
(`edi` = `node+0x64`, `+8` = `node+0x6c`). aap's `DisplayListDrawable::self` is exactly right and
not a hack.

### `DisplayListPrimitive` membership toggles

`+0x1c` holds three bits:

| bit | meaning | who sets it |
|---|---|---|
| 1 | `visible` | `SetVisible(bool)` `0x458ec0`, `Display(bool)` `0x458f00` |
| 2 | `inList` (some nodes of mine are in the display list) | `Display()` on submit, cleared on every removal |
| 4 | `flag4` (re-check the drawable each frame) | `SetDrawable(d, flag4)` `0x458e50`, copy ctor |

`SetVisible` only ever *removes*; only `Display` submits. `SetDrawable` and `operator=` both remove
first, because the nodes cache `&container->elements[i]`.

### What the 84 lists are FOR

The `layer` (0..44) baked into each `DrawablePrimitive` at load time is a **material class**
(shader type × lit × alpha-test × blend). `AddContainerElement` expands `(layer, isFading, isLit,
isALUM, shaderType, blendMode, mask)` into one of 84 **buckets**. A bucket is "a set of primitives
that want exactly the same pddi render state and the same place in the draw order". `Render()` then
walks the buckets in a hand-written order, setting the state once per bucket instead of per object.

Grouped by purpose:

| group | lists | what |
|---|---|---|
| **sky / camera-locked** | 46, 47, 76 | drawn first, z-test and z-write **off**, fog off, translated to the camera's XZ so they never move |
| **low-LOD / skyline** | 59, 2 | `UNTEXTURED`/`VERTEXFADE` distant city silhouette |
| **opaque world geo (unclassified layer 0/1)** | 49, 50, 52, 53, 54 (+ fading 51, 55, 56) | the bulk of the streamed world |
| **opaque lit / cbvlit / unlit buildings** | 21, 22, 23, 24, 26, 27, 28, 29, 30, 35, 36, 37, 38, 39, 43 (+ fading 25, 31, 32, 40, 41, 44, 45) | the material-class buckets proper |
| **layered (interiors)** | 42, 43 (+ 45, 44) | two-texture-layer shader |
| **interior floors** | 33, 34 | unlit alpha blend |
| **specular road/ground** | 13, 15 (+ fading 14, 16) | drawn twice: early, and again after the shadow volumes |
| **decals** | 3, 4, 17, 18, 75 (+ fading 5, 6, 19, 20) | z-write **off**, colour-write alpha **off** |
| **shadows** | 7, 8, 77 (blob/decal, z-write off, gated by the StaticShadowGen extension), 61, 62, 63, 64 (stencil volumes) | |
| **environment / reflection** | 9, 10 | stencil-tested (`COMPARE_EQUAL`, ref 1), z-write off, alpha-write off |
| **night lighting** | 11 (night lights), 12 (cards-night) | z-write off, fog forced off |
| **instanced eco props** | 72, 73, 74 | the pddi instancing extension (`0x200200`); 72 is drawn in 2–3 passes, 73 in 2 |
| **water** | 65 (below-surface), 66 (surface), 0, 1 (foam — **never drawn on PC**) | |
| **underwater** | 78, 79 (+ fading 80, 81) | |
| **special lit sets** | 67, 68, 69, 70, 71, 82, 83 | per-node light-set selection through `0x45fb30` |
| **depth-only** | 58 | `SetColourWrite(0,0,0,0)` — a pure z-fill |
| **singletons** | 60 | only `lists[60].head` is ever drawn |
| **unused** | 48, 57 | no writer, no reader |

### The exact draw order, as groups

Let

```c
bool indoors = renderMgr->[0x1c]->IsCameraIndoors()            // 0x46a520
            && ( 0x461d40() || (0x461d60() && 0x55f0b0(g[0x8251a8])) );
```

Group **(B)** below is emitted **early when `!indoors`** and **last when `indoors`** — i.e. when you
are inside a building the outside world is drawn after the interior.

1. `ext(0x400002)->vslot3()`; `SetCullMode(NORMAL)`; `SetColourWrite(r,g,b, a=0)`; fog off →
   **sky 46, 47** → fog restored.
2. **59** (low-LOD skyline).
3. *(B)* **36, 42** (unlit blend / layered); **73** (instanced, 2 passes); **52**;
   then the alpha-mask clear: `SetColourWrite(0,0,0,1)`, `SetClearColour(0x01000000)`,
   `Clear(COLOUR)` through the quad path (`ctx+0x1bc`) — this writes **alpha = 1** over the whole
   frame buffer; **53**.
4. **21, (26, 22), 27, 28, 35, 43** — the opaque building buckets.
5. **74** (instanced).
6. stencil on (`op 0,0,0`, `compare EQUAL`, `ref 1`), alpha-write off, fog off →
   **9, 10** (env/reflection, z-write off) → stencil off, fog on.
7. **13, 15** (specular).
8. **78, 79** (underwater).
9. *(B)* z-write off → **3, 17, 18, 4, 75** (decals) → z-write on; **33, 34**; **62** (projected
   shadows, via `ext(0x108)`).
10. if `g[0x7bfb55]` and lists 7/8 non-empty: `ext(0x108)->Begin()` → **7, 8, 77** (shadow decals,
    z-write off) → `ext(0x108)->End()`.
11. *(B)* **49**; **67**; **69**; **82**.
12. if `0x4636f0() >= 0`: **58** depth-only (`SetColourWrite(0,0,0,0)`), then `lists[60].head`.
13. **2**; **14, 16**; z-write off → **5, 19, 20, 6** → z-write on.
14. z-write off → **11** (night lights, fog forced off) → z-write on.
15. **72** (instanced, 2 passes in the main view, 1 in the reflection).
16. **44, 25, (26, 22), 32, 31, 40**; **80, 81**; **23**; **29, 50**.
17. z-write off → **12** (cards-night) → z-write on.
18. *(B)* **54**. Then **51**.
19. `SetColourWrite(1,1,1,0)`; `renderMgr->[0x34]->vslot11()`; `0x67ba30()`;
    `ctx->SetClearStencil(0x460a50, 0)` — actually `ctx+0x24` called with a **function pointer**
    `0x460a50` and 0; then `SetColourWrite(1,1,1,1)`.
20. *(B)* **68**, **83**.
21. **71**; **61, 63, 64** (stencil shadow volumes, tinted with `Display_List+0xc8 = 0xff191919`);
    **15, 13 a second time**.
22. *(if `indoors`)* the whole of group **(B)** here, preceded by `renderMgr->[0x24]->vslot11()`.
23. **41, 45**; **38, 37**; z-write off → **39, 30, 24** → z-write on.
24. `renderMgr->[0x30]->vslot11()`; **65** (water); **70**; **66** (water surface);
    z-write off → `renderMgr->[0x38]->vslot11()` → z-write on.
25. **55**, **56**.
26. fog off → **76** (camera-locked, gated on `g[0x83168c]->+0x3c`) → fog restored.
27. if `this->+0xa9`: `ext(0x103 FramebufferEffects)` post-process (`+0x38`, `+0x44`, `+0x10`).
28. `FreeOrphanNodes()`, then the self-timing feedback that produces `g[0x0081088c]` (§8).

Note the colour-write discipline: the frame-buffer **alpha channel is a mask**, filled with 1 in
step 3 and then protected (`SetColourWrite(1,1,1,0)`) by almost every pass; only the passes that
are meant to punch holes in it use `SetColourWrite(1,1,1,1)`.

---

## 2. Object layouts

```c
// ---- renderer::Display_List : pure3d::Entity     vtable 0x00737738, 0xcc+ bytes
//      ctor 0x0045f110(int nodeCount), dtor 0x0045d9a0
struct Display_List {                     // [V] all offsets from the ctor
  /* 0x00 */ void *vtable;                //     Entity base occupies 0x00..0x2f
  /* 0x30 */ LinkedList *lists;           //     malloc(0x3f0) = 84 * 12
  /* 0x34 */ LinkedList *listsEnd;        //     lists + 0x3f0
  /* 0x3c */ u8   listDirty[84];          //     0x3c..0x8f; set by Add, cleared by SortAllLists
  /* 0x90 */ LinkedList freeList;         //     12 bytes {head, tail, count}
  /* 0x9c */ Node *nodeStore;             //     one flat array of nodeCount * 0x70
  /* 0xa0 */ Node *nodeStoreEnd;
  /* 0xa8 */ bool  ?;                     //     ctor: 0
  /* 0xa9 */ bool  doPostProcess;         //     ctor: 1   -> the ext(0x103) tail of Render()   [?]
  /* 0xaa */ bool  cameraInsideVolume;    //     recomputed at the top of SortAllLists          [?]
  /* 0xac */ pure3d::ShadowGenerator shadowGen;   // embedded, vtable 0x00737718
  /* 0xc4 */ void *lightCache;            //     0x20-byte object + a 0x60-byte member          [?]
  /* 0xc8 */ u32   shadowVolumeColour;    //     0xff191919, used by RenderShadowVolumes         [V]
};

// ---- Display_List::Node                          stride 0x70 (112), NOT 0x6c
struct Node {                             // [V]
  /* 0x00 */ ListLink link;               //     next, prev — in lists[listID] or in freeList
  /* 0x08 */ Matrix   matrix;             //     world matrix captured at submit time
  /* 0x48 */ float    sortKey;            //     class key: container->+0x3c, or 0.0/0.5/1.0 per layer
  /* 0x4c */ float    sortKey2;           //     view-space z, recomputed by ComputeDepthKeys
  /* 0x50 */ PrimArrayEntry *elem;        //     &container->elements[idx]   (16-byte stride)
  /* 0x54 */ DrawableContainer *container;
  /* 0x58 */ Shader  *shader;             //     elem->prim->GetShader(), the batching key
  /* 0x5c */ DisplayListPrimitive *parent;//     0 == orphan, recycled by FreeOrphanNodes
  /* 0x60 */ LinkedList *myList;          //     &lists[listID]
  /* 0x64 */ ListLink linkParent;         //     in parent->children
  /* 0x6c */ Node    *self;               //     == this; lets linkParent be walked back to the node
};

// ---- renderer::DisplayListPrimitive : GameDrawableInfo   vtable 0x00737704, 0x20 bytes
//      ctor 0x00458c60, dtor 0x00458cc0
struct DisplayListPrimitive {             // [V]
  /* 0x00 */ void *vtable;                //     {AddRef, Release, GetRef, dtor} only
  /* 0x04 */ u32   ?;                     //     zeroed by ctor AND by the copy ctor (not copied) [?]
  /* 0x08 */ Renderable *owner;           //     copied by operator=; read as node->parent->owner
  /* 0x0c */ DrawableHierarchy *drawable; //     ref-counted
  /* 0x10 */ LinkedList children;         //     12 bytes, of Node::linkParent
  /* 0x1c */ u8 flags;                    //     1 = visible, 2 = inList, 4 = flag4
};

// ---- LinkedList (0x006e89e0 / 0x006e8970 / 0x006e8a20)
struct LinkedList { void *head; void *tail; int count; };   // 12 bytes, links are {next, prev}
```

Note `NUM_DISPLAY_LISTS = 84` is confirmed three times: `malloc(0x3f0)` in the ctor, the
`cmp ebx, 0x3f0` loop in `FreeOrphanNodes`, and `listDirty` spanning `+0x3c..+0x8f`.

---

## 3. Submission

### `renderer::Display_List::AddContainer` — `0x0045d360` (vslot 7) **[V]**

```c
void Display_List::AddContainer(DrawableContainer *c, Matrix *m, DisplayListPrimitive *p) {
    int n = (c->elementsEnd - c->elementsBegin) / 16;
    for (int i = 0; i < n; i++) this->AddContainerElement(c, i, m, p);   // vslot 8
}
```

### `renderer::Display_List::AddContainerElement` — `0x0045d3b0` (vslot 8) **[V]**

```c
void Display_List::AddContainerElement(DrawableContainer *c, int idx,
                                       Matrix *m, DisplayListPrimitive *p)
{
    bool isFading = c->IsFading();                  // container vslot [+0x4c]
    Node *nd = freeList.head;
    if (!nd) return;                                // silently dropped when the pool is empty

    Matrix *world = GetWorldMatrix(g[0x00831690]);  // 0x67e7e0
    if (m) Matrix::Multiply(&nd->matrix, m, world); // nd->matrix = m * world
    else   nd->matrix = *world;

    nd->container = c;
    nd->elem      = &c->elements[idx];              // c->+0x44 + idx*16
    nd->shader    = nd->elem->prim->GetShader();    // prim vslot [+0x24]
    nd->sortKey   = c->+0x3c;

    DrawablePrimitive *prim = nd->elem->prim;
    int layer  = prim->GetLayer();                  // prim->+0x0c
    Shader *sh = prim->GetShader();
    if (layer > 44) return;                         // switch default: dropped
    int listID = <the 45-case jump table at 0x0045d8dc — see renderspine.md §3.3>;

    if (isFading) nd->sortKey = 1.0f;               // common tail, layers 0/1/32/35
    if (listID < 0) return;                         // layers 9 and 43: dropped

    freeList.Remove(nd);
    if (p) { nd->parent = p; p->children.Insert(&nd->linkParent); }
    else     nd->parent = nil;                      // orphan
    lists[listID].Insert(nd);                       // insert at HEAD
    nd->myList = &lists[listID];
    listDirty[listID] = 1;
}
```

The sort-key writes renderspine flagged as "TODO: set shit" are:
`0x45d562` → `1.0f` (fading, common tail), `0x45d639` → `0.5f`, `0x45d645` → `0.0f`
(layer 1 with mask 2/4/8), `0x45d678` → `6.0f` **into `prim->+0x48`, not the node** (layer 2).

### `renderer::Display_List::RemovePrimitiveNodes` — `0x00458a20` **[V]**

```c
void Display_List::RemovePrimitiveNodes(DisplayListPrimitive *p) {
    for (ListLink *l = p->children.head, *next; l; l = next) {
        next = l->next;
        Node *nd = *(Node**)(l + 8);              // == l->self, i.e. node+0x6c
        nd->myList->Remove(nd);                   // 0x6e89e0
        freeList.Insert(nd);                      // 0x6e8970
        p->children.Remove(l);
        nd->parent = nil;
    }
}
```

### `renderer::Display_List::FreeOrphanNodes` — `0x0045b0c0` **[V]**

```c
void Display_List::FreeOrphanNodes(void) {
    for (int off = 0; off < 0x3f0; off += 12)             // all 84 lists
        for (Node *nd = *(Node**)((u8*)lists + off), *next; nd; nd = next) {
            next = nd->link.next;
            if (nd->parent == nil) {                      // orphan -> recycle
                ((LinkedList*)((u8*)lists + off))->Remove(nd);
                freeList.Insert(nd);
            }
        }
}
```
Called once, at the very end of `Render()`. Note it does **not** clear `listDirty[]`, and it does
not touch `nd->myList`.

---

## 4. Per-node visibility and the generic list walk

### `renderer::Display_List::IsNodeVisible` — `0x0045b110` **[V]**

```c
// __thiscall on Display_List, ret 0xc
bool Display_List::IsNodeVisible(Camera *cam, PrimArrayEntry *elem, const Matrix *m)
{
    DrawablePrimitive *prim = elem->prim;
    Sphere s = *(Sphere*)(prim + 0x28);          // {cx,cy,cz,r}
    s.centre += m->row3;                         // translation only — no rotation/scale!
    if (!cam->SphereVisible(&s))                 // camera vslot [+0x54], the Sphere* overload
        return false;
    Box b = *(Box*)(prim + 0x10);                // {min, max}
    b.min += m->row3;  b.max += m->row3;
    return occlude::IsBoxVisible(&b);            // 0x458bf0
}
```
Both tests use the **primitive's own** bounds, not the renderable's, and only the node matrix's
translation row — so a rotated or scaled instance gets a slightly wrong bound. That is retail
behaviour, not a transcription error.

### `occlude::IsBoxVisible` — `0x00458bf0`, `Occluder::TestBox` — `0x00458ba0`, `Plane::BoxInside` — `0x00458b20` **[V]**

```c
bool occlude::IsBoxVisible(const Box *b) {                     // 0x458bf0
    g[0x810cdc]++;                                             // stat: boxes tested
    for (i = 0; i < g[0x810d04]; i++) {
        Occluder *o = &occluders[i];                           // 0x810d10, stride 0x98
        if (o->disabled /*+0x29*/) continue;
        g[0x810cec]++;
        if (Occluder::TestBox(o->planes, o->numPlanes, b) == 2) { g[0x810ce4]++; return false; }
    }
    return true;
}
int Occluder::TestBox(const Plane *planes, int n, const Box *b) {   // 0x458ba0
    for (i = 0; i < n; i++) { g[0x810cf0]++; if (!Plane::BoxInside(&planes[i], b)) return 0; }
    return 2;                                                  // fully inside every plane == hidden
}
bool Plane::BoxInside(const Plane *p, const Box *b) {           // 0x458b20 — "positive vertex" test
    float x = (p->nx > 0) ? b->min.x : b->max.x;   // 0x72f994 == 0.0f
    float y = (p->ny > 0) ? b->min.y : b->max.y;
    float z = (p->nz > 0) ? b->min.z : b->max.z;
    return p->nx*x + p->ny*y + p->nz*z >= -p->d;
}
```

### `renderer::Display_List::RenderList(int listID, bool applyContainerFade)` — `0x00459910` **[V]**

The only generic walker; used for lists 19, 49, 51, 52, 53, 54, 55, 56, 75, 78, 79, 80, 81.
It does **not** cull.

```c
void Display_List::RenderList(int listID, bool applyContainerFade) {
    for (Node *nd = lists[listID].head; nd; nd = nd->link.next) {
        DrawablePrimitive *prim = nd->elem->prim;
        bool unlit = !prim->IsLit() && !prim->IsALUM();          // vslots [+0x2c], [+0x30]
        if (applyContainerFade) prim->SetFade  (nd->container->GetFade());   // [+0x38] <- [+0x50]
        if (unlit)              prim->SetTint  (nd->container->GetTint());   // [+0x3c] <- [+0x54]
        ctx->PushMatrix(0);  ctx->MultMatrix(0, &nd->matrix);
        PrimArrayEntry::Display(nd->elem);                       // 0x683340
        ctx->PopMatrix(0);
        if (unlit)              prim->SetTint(1.0f);
        if (applyContainerFade) prim->SetFade(0.0f);
    }
}
```

The ~35 specialised renderers in §7 are all the same shape, open-coded per list so the pddi state
and the shader-mode extension calls can be hoisted out of the loop:

```c
Camera *cam = GetCurrentCamera();                           // 0x461ad0
Node *nd = lists[N].head;
if (nd) {
    ShaderModeExt *ext = ctx->GetExtension(0x10b);
    ext->BeginMode_X();                                     // one of ~25 paired vslots
    do {
        DrawablePrimitive *prim = nd->elem->prim;
        if (IsNodeVisible(cam, nd->elem, &nd->matrix)) {
            [ float f = nd->container->GetFade(); prim->SetFade(f); ]
            ctx->PushMatrix(0);  ctx->MultMatrix(0, &nd->matrix);
            prim->Display();                                // prim vslot [+0x20], NOT PrimArrayEntry
            ctx->PopMatrix(0);
            [ prim->SetFade(0.0f); ]
        }
    } while ((nd = nd->link.next) != nil);
    ext->EndMode_X();
}
```

### The shader-mode extension, `renderContext->GetExtension(0x10b)` **[V for the mechanism, [?] for the name]**

`d3dContext::GetExtension` (`0x64be30`) maps extension ids to members:
`0x101`→`+0x4f8` GammaControl, `0x102`→`+0x4f4` HardwareSkinning, **`0x103`→`+0x50c`
`d3dExtFramebufferEffects`**, `0x104`→`+0x51c` FramebufferTexture, `0x106`→`+0x508`,
`0x107`→`+0x510`, **`0x108`→`+0x52c` `d3dExtStaticShadowGen`**, **`0x10b`→`+0x524`
`d3dExtWin32Control`** (per RTTI; the name is misleading), `0x200200`→`+0x520` instancing,
`0x200300`→`+0x518`, `0x400001`→`+0x534`, `0x400002`→`+0x514`, `0x500001/4/5/6`→
`+0x4fc/+0x500/+0x504/+0x528`.

The `0x10b` object has 55 vslots, almost all **paired Begin/End** methods that set global bytes in
`0x00830a25 .. 0x00830a37` — the same globals `d3dSimpleShader::SetPass` (`0x65be70`) reads (see
`notes/shaderstate.md`, `g_byte_830a35`). Each list group turns on the shader mode it wants and
turns it off again. Observed pairs:

| ext(0x10b) vslot pair | used around lists |
|---|---|
| `+0x08` / `+0x0c` | 46, 47 (both nullsub on PC) |
| `+0x10` / `+0x14` | 21, 25, 26, 22 |
| `+0x18` / `+0x1c` | 28, 31, 26, 22 |
| `+0x20` / `+0x24` | 29, 50 |
| `+0x30` / `+0x34` | 33, 34, 35, 36, 37, 38, 39, 40, 41, 42 |
| `+0x44` / `+0x48` | 13, 14 |
| `+0x4c` / `+0x50` | 15, 16 |
| `+0x54` | the second 15/13 pass (`0x45b240`) |
| `+0x64`/`+0x68`, `+0x6c`/`+0x70`, `+0x74`/`+0x78`, `+0xd4` | instanced list 72 (3 modes) |
| `+0x7c`/`+0x80`, `+0x84`/`+0x88` | instanced list 73 (2 modes) |
| `+0x8c` / `+0x90` | 23, 26/22, 78/79, 80/81 |
| `+0x9c` / `+0xa0` | 3, 5 |
| `+0xa4` / `+0xa8` | 4, 6 |
| `+0xac` / `+0xb0` | 2 |
| `+0xb4` / `+0xb8` | 17/18/19/20 |

A reimplementation can ignore all of these to first order — they are the fixed-function
multi-pass trickery for the 2006 D3D8 path. What you cannot ignore is the `SetZWrite` /
`SetColourWrite` / `EnableFog` / stencil bracketing in §1, which is real depth/blend behaviour.

---

## 5. The sort pass

### `renderer::Display_List::SortAllLists` — `0x0045ad10` **[V]**

Called from `GamePlayScene::Update` after every renderable has submitted, before `Render()`.

```c
void Display_List::SortAllLists(void *arg) {
    0x67ba30();                                   // -> g[0x831b2c]
    Camera *cam = View_GetRenderingCamera();      // 0x461ac0 -> g[0x8111d8]  (NOT 0x461ad0, the cull camera)
    Vector camPos; cam->GetPosition(&camPos);
    this->cameraInsideVolume /*+0xaa*/ = TestInteriorVolumes(g[0x8111cc], camPos);   // 0x45fad0

    #define SORT(list, cmp)   if (listDirty[list]) { lists[list].Sort(cmp); listDirty[list] = 0; }
    #define SORTZ(list, cmp)  { ComputeDepthKeys(list); lists[list].Sort(cmp); ... }

    SORT (46, CmpKey);          SORT (47, CmpKey);                       // sky
    SORT (59, CmpShader);
    SortGroup_36_41_42_45();                                             // 0x459780
    SORT (33, CmpShader);  SORT (34, CmpShader);
    SORT (13, CmpShader);  SORT (14, CmpShader);
    SortGroup_21_25_22_26_27_32_28_31_43_44_35_40();                     // 0x459560
    SortGroup_78_80_79_81();                                             // 0x4593c0
    SortGroup_4_3_18_17_75();                                            // 0x4592c0
    SortGroup_7_8_77();                                                  // 0x459500
    SORT (9,  CmpShader);  SORT (10, CmpShader);
    SORT (67, CmpShader);  SORT (68, CmpShader);
    lists[69].Sort(CmpKeyThenMaterial);                                  // unconditional
    lists[82].Sort(CmpKeyThenMaterial);                                  // unconditional
    SortGroup_52_53_56_55();                                             // 0x4596f0
    SORT (49, CmpShader);
    if (listDirty[51]) { ComputeDepthKeys(51); lists[51].Sort(CmpKeyThenDepth); listDirty[51]=0; }
    SORT (0,  CmpKeyThenMaterial);   SORT (1,  CmpKeyThenMaterial);      // foam — never drawn
    SortGroup_5_6_19_20();                                               // 0x459350
    SORT (2,  CmpShader);
    SortGroup_29_50();                                                   // 0x459460
    ComputeDepthKeys(71); lists[71].Sort(CmpKeyThenDepth);               // unconditional
    ComputeDepthKeys(70); lists[70].Sort(CmpKeyThenDepth);               // unconditional
    ComputeDepthKeys(83); lists[83].Sort(CmpKeyThenDepth);               // unconditional
    if (renderMgr->[0x1c]->+0x42) { SORT(11, CmpShader); SORT(12, CmpShader); }
    if (listDirty[54]) { ComputeDepthKeys(54); lists[54].Sort(CmpKeyThenDepth); listDirty[54]=0; }
    SortGroup_37_38();                                                   // 0x4594b0
    SortGroup_30_39_24();                                                // 0x459260
    ComputeDepthKeys(65);  lists[65].Sort(CmpKeyThenDepth);              // unconditional
    ComputeDepthKeysAtOrigin66(); lists[66].Sort(CmpKeyThenDepth);       // unconditional

    renderMgr->[0x38]->Prepare(camPos);   renderMgr->[0x30]->Prepare(camPos);
    renderMgr->[0x34]->Prepare(camPos);                                  // all vslot [+0x24]
    occlude::Build(GetCurrentCamera(), 150.0f, g[0x7bfad8]);             // 0x461270
}
```

Which lists get which comparator, complete (the `SortGroup_*` helpers are just inlined-by-hand
sub-blocks of the same thing):

| comparator | lists |
|---|---|
| `CmpKey` `0x4589e0` | 46, 47 |
| `CmpShader` `0x459060` | 2, 7, 8, 9, 10, 11, 12, 13, 14, 21, 22, 24, 27, 28, 30, 33, 34, 35, 36, 39, 42, 43, 49, 52, 53, 59, 67, 68, 75, 77 |
| `CmpKeyThenMaterial` `0x459090` | 0, 1, 3, 4, 5, 6, 17, 18, 19, 20, 69, 78, 79, 82 |
| `CmpKeyThenDepth` `0x458ff0` (preceded by `ComputeDepthKeys`) | 25, 26, 29, 31, 32, 37, 38, 40, 41, 44, 45, 50, 51, 54, 55, 56, 65, 66, 70, 71, 80, 81, 83 |

Never sorted (and therefore drawn in reverse submit order): 15, 16, 23, 48, 57, 58, 60, 61, 62,
63, 64, 72, 73, 74, 76. Lists **48 and 57 are not touched anywhere in the TU** — not sorted, not
drawn; lists **0 and 1** are sorted but never drawn.

### The comparators

```c
// 0x004589e0  CmpKey        — DESCENDING by node->sortKey, never returns 0
int CmpKey(const Node **a, const Node **b) { return ((*a)->sortKey < (*b)->sortKey) ? 1 : -1; }

// 0x00458ff0  CmpKeyThenDepth
int CmpKeyThenDepth(const Node **pa, const Node **pb) {
    const Node *a = *pa, *b = *pb;
    if (fabsf(a->sortKey - b->sortKey) > 0.001f)          // 0x73cb48
        return (a->sortKey < b->sortKey) ? 1 : -1;        // descending
    if (fabsf(a->sortKey2 - b->sortKey2) <= 0.0001f)      // 0x730db0
        return 0;
    return (a->sortKey2 < b->sortKey2) ? 1 : -1;          // descending view z == FAR TO NEAR
}

// 0x00459090  CmpKeyThenMaterial
int CmpKeyThenMaterial(const Node **pa, const Node **pb) {
    if (fabsf(a->sortKey - b->sortKey) > 0.001f) return (a->sortKey < b->sortKey) ? 1 : -1;
    if (!a->shader) return 0;
    if (!b->shader) return 1;
    int ka = a->shader->+0xc->vslot4();   // [+0x10] — the texture / material id
    int kb = b->shader->+0xc->vslot4();
    if (ka != kb) return ka < kb ? -1 : 1;
    return a->shader < b->shader ? -1 : 1;
}

// 0x00459060  CmpShader — pure material batching, ASCENDING
int CmpShader(const Node **pa, const Node **pb) {
    if (!a->shader) return 0;
    if (!b->shader) return 1;
    return (a->shader->+0x10 < b->shader->+0x10) ? -1 : 1;
}
```

Note the **descending** direction on the key comparators: with `qsort`, "a before b when a's key is
larger" means the largest key ends up first, and a *larger* `sortKey2` (view z) means *farther
away*. Combined with `LinkedList::Insert` pushing at the head, the drawn order comes out
**far → near**, which is what alpha blending needs. `sortKey` itself is the fade/priority class
(0.0, 0.5, 1.0, and 1.0 for anything fading), so fading geometry is always drawn before
non-fading geometry in the same bucket.

### `renderer::Display_List::ComputeDepthKeys(int listID)` — `0x00459120` **[V]**

```c
void Display_List::ComputeDepthKeys(int listID) {
    for (Node *nd = lists[listID].head; nd; nd = nd->link.next) {
        ctx->PushMultMatrix(0, &nd->matrix);              // [+0x64]
        Sphere s = *(Sphere*)(nd->elem->prim + 0x28);
        Vector out;
        Matrix::TransformPoint(ctx->GetMatrix(0), &s.centre, &out);   // 0x6610f0
        nd->sortKey2 = out.z;                             // view-space depth
        ctx->PopMatrix(0);
    }
}
```

`0x004591e0` is the same thing hard-wired to list 66 (the water surface) and transforming the
**local origin** `(0,0,0)` instead of the bounding-sphere centre — the water quads' spheres are
useless, their pivot is not.

### `LinkedList::Sort(int (*cmp)(const void*, const void*))` — `0x006e8a20` **[V]**

Collects `count` node pointers into a `malloc`'d array, `qsort`s it, relinks head/prev/next, frees.
No-op for `count < 2`.

---

## 6. The reflection pass

### `renderer::Display_List::RenderReflection` — `0x0045bb90` **[V]**

Tail-jumped into from `GamePlayScene::Render` (`0x468ab3`), i.e. it runs *before* `Render()`.

```c
void Display_List::RenderReflection(void) {
    struct PreMatrix { bool valid; Matrix m; } pre;        // the arg the sky/instance renderers take
    pre.valid = true;  Matrix::Identity(&pre.m);
    Matrix mirror;  Matrix::Identity(&mirror);
    mirror.e[?] = -1.0f;                                   // one axis flipped -> water reflection
    Matrix::SetPosition(&mirror, {0,0,0});

    ctx->PushMatrix(0);  ctx->MultMatrix(0, &mirror);
        bool fog = ctx->IsFogEnabled(); ctx->EnableFog(false);
        RenderSky(&pre);                                   // 0x459a00 -> lists 46, 47
        ctx->EnableFog(fog);
    ctx->PopMatrix(0);

    ctx->vslot106 [+0x1a8] (0, 1);                         // enable the reflection target        [?]
        pre.valid = false;
        fog = ctx->IsFogEnabled(); ctx->EnableFog(false);
        RenderSky(&pre);
        ctx->EnableFog(fog);
        RenderInstanced72(&pre);                           // 0x45a4c0, single-pass branch
    ctx->vslot106 [+0x1a8] (0, 0);
}
```

This explains the odd `struct { bool valid; Matrix m; }*` argument that `RenderSky`,
`RenderInstanced72` and most of the `0x45xxxx` helpers take: it is an **optional extra transform
prepended to every node's matrix in that pass**. In `Render()` it is always
`{ valid = false }`; only the reflection pass sets it.

---

## 7. Function inventory

Every function in `0x004589d0 .. 0x0045f3b0`. "lists" = which display lists it walks.

### Display_List lifetime

| addr | proposed name | signature | what |
|---|---|---|---|
| `0x0045f110` | `renderer::Display_List::Display_List` | `(int nodeCount)` | Entity ctor; `malloc(0x3f0)` for the 84 `LinkedList`s and inits them; clears `listDirty[84]`; allocates `nodeCount * 0x70` nodes in one block and threads them onto the free list, writing `node->self`; embeds a `pure3d::ShadowGenerator` at `+0xac`; `+0xc8 = 0xff191919`. Called once, from `0x00468ff5`. |
| `0x0045d9a0` | `renderer::Display_List::~Display_List` | `()` | frees the node block and the list array, runs the `ShadowGenerator` dtor, chains to `Entity::~Entity` (`0x6864e0`). |
| `0x0045f370` | `renderer::Display_List::'scalar deleting dtor'` | `(u8 flags)` | vslot 3. |
| `0x00458a90` | `renderer::Display_List::AllocNodeStore` ? | `(Node **store, u32 n) -> bool` | `n*0x70` bytes, writes `{begin,end}`. |
| `0x0045b200` | `renderer::Display_List::InitNodeStore` ? | `(Node *first, u32 n, const Node *proto)` | `rep movsd` 0x1c dwords per node, stride `0x70`. |
| `0x004589d0` | `renderer::SetGlobalDisplayList` | `(Display_List*)` | `g[0x008108a0] = arg`. Called from `0x468ea0` (with 0) and `0x469021`. |
| `0x00458a80` | `renderer::GetDetailScale` ? | `() -> float` | returns `g[0x0081088c]`, the adaptive-quality value produced at the end of `Render()` (§8). Tail-jumped from `0x461cf0`. |

### DisplayListPrimitive

| addr | proposed name | signature | what |
|---|---|---|---|
| `0x00458c60` | `renderer::DisplayListPrimitive::DisplayListPrimitive` | `()` | vtable `0x737704`, everything nil, `flags &= ~7`. |
| `0x00458cc0` | `renderer::DisplayListPrimitive::~DisplayListPrimitive` | `()` | removes its nodes, releases the drawable. |
| `0x00458d30` | `renderer::DisplayListPrimitive::DisplayListPrimitive_copy` | `(const DisplayListPrimitive&)` | copy ctor; copies `owner`, `drawable` (ref-counted), the `visible` and `flag4` bits, but **not** `inList` and not the children. |
| `0x00458de0` | `renderer::DisplayListPrimitive::operator=` | `(const DisplayListPrimitive&)` | same, and removes its own nodes first. |
| `0x00458e50` | `renderer::DisplayListPrimitive::SetDrawable` | `(DrawableHierarchy*, bool flag4)` | removes its nodes, swaps the ref-counted drawable, sets `flag4`. |
| `0x00458ea0` | `renderer::DisplayListPrimitive::RemoveFromList` | `()` | `if (g_displayList && inList) { RemovePrimitiveNodes(this); inList = false; }` |
| `0x00458ec0` | `renderer::DisplayListPrimitive::SetVisible` | `(bool)` | sets the visible bit; removes if it just became invisible. **Never submits.** Callers: `WorldGeoRenderable::SetVisible`, `Renderable::SetVisible`. |
| `0x00458f00` | `renderer::DisplayListPrimitive::Display` | `(bool visible)` | the edge-triggered submit/withdraw shown in §1. Callers: `Renderable::Display` (`0x47458a`, `0x474605`), `WorldGeoRenderable::Display`. |

### Submission and node management

`0x0045d360` `AddContainer` (vslot 7) · `0x0045d3b0` `AddContainerElement` (vslot 8) ·
`0x00458a20` `RemovePrimitiveNodes` · `0x0045b0c0` `FreeOrphanNodes` · `0x0045b110`
`IsNodeVisible` — all in §3/§4.

### Culling helpers (shared with `occlude::`)

`0x00458b20` `Plane::BoxInside` · `0x00458ba0` `Occluder::TestBox` · `0x00458bf0`
`occlude::IsBoxVisible` — §4.

### Shader predicates

| addr | proposed name | what |
|---|---|---|
| `0x00458ae0` | `pure3d::Shader::IsSortedBlendMode` ? | `blendMode (sh+0x14) ∈ {1 ALPHA, 2 ADD, 3 SUBTRACT, 7 SUBMODULATEALPHA}`. This is renderspine's `IsXXXBlendMode`, used for layers 32 and 34. |
| `0x00458b00` | `pure3d::Shader::IsModulateBlendMode` ? | `blendMode ∈ {2 ADD, 4 MODULATE, 5 MODULATE2, 6 ADDMODULATEALPHA}`. Used for the 0.5f sort key on layer 1. |

### Sorting

`0x0045ad10` `SortAllLists` · `0x00459120` `ComputeDepthKeys` · `0x004591e0`
`ComputeDepthKeysList66` · comparators `0x004589e0` `0x00458ff0` `0x00459060` `0x00459090` — §5.

The ten `SortGroup_*` sub-blocks, each `if (listDirty[n]) { [ComputeDepthKeys(n);] lists[n].Sort(cmp); listDirty[n] = 0; }`:

| addr | lists (in order) |
|---|---|
| `0x00459260` | 30, 39, 24 |
| `0x004592c0` | 4, 3, 18, 17, 75 |
| `0x00459350` | 5, 6, 19, 20 |
| `0x004593c0` | 78, 80, 79, 81 |
| `0x00459460` | 29, 50 |
| `0x004594b0` | 37, 38 |
| `0x00459500` | 7, 8, 77 |
| `0x00459560` | 21, 25, 22, 26, 27, 32, 28, 31, 43, 44, 35, 40 |
| `0x004596f0` | 52, 53, 56, 55 |
| `0x00459780` | 36, 41, 42, 45 |

### The list renderers

All are `__thiscall` on `Display_List`. "arg" = the optional `PreMatrix*` (§6); those without it
take no argument. `L` = lights re-bound via `0x46b930(renderMgr->[0x1c], 1)` on entry.

| addr | proposed name | lists | notable state |
|---|---|---|---|
| `0x00459a00` | `renderer::Display_List::RenderSky` | 46, 47 | `EnableZBuffer(false)`, `SetZWrite(false)`, matrix translated to `(camX, 0, camZ)`; list 47 additionally does container-fade → `prim->SetFade` |
| `0x0045d140` | `renderer::Display_List::RenderLowLOD59` | 59 | L; per-node cull |
| `0x0045ccd0` | `renderer::Display_List::RenderUnlitBlendAndLayered_36_42` | 36, 42 | ext `+0x30`/`+0x34` around 36 |
| `0x0045a680` | `renderer::Display_List::RenderInstanced73` | 73 (×2) | instancing ext `0x200200`; `InstancePrimitive::PreDisplay / Display / DrawInstanced`; ext(0x10b) `+0x84/+0x88` then `+0x7c/+0x80` |
| `0x0045c800` | `renderer::Display_List::RenderOpaqueBuildings_21_27_28_35_43` | 21, {26,22}, 27, 28, 35, 43 | L; calls `0x45c070(false)` for 26/22 |
| `0x0045c070` | `renderer::Display_List::RenderCbvAlphaTest_26_22` | 26, 22 | `(bool fadingFirst)`; L; ext `+0x8c/+0x90` outer, `+0x10/+0x14` and `+0x18/+0x1c` inner |
| `0x0045a620` | `renderer::Display_List::RenderInstanced74` | 74 | instancing ext only |
| `0x0045dcf0` | `renderer::Display_List::RenderEnv_9_10` | 9, 10 | L; `SetZWrite(false)`, `SetColourWrite(1,1,1,0)`; list 10 applies container fade |
| `0x0045deb0` | `renderer::Display_List::RenderSpecular_13_15` | 13, 15 | L; `GetExtension(0x200300)` + `__RTDynamicCast`, `ext->+0x28(false)`; ext(0x10b) `+0x44/+0x48`, `+0x4c/+0x50` |
| `0x0045ac10` | `renderer::Display_List::RenderUnderwater_78_79` | 78, 79 | L; `RenderList(78,false)`, ext `+0x8c`, `RenderList(79,false)`, ext `+0x90` |
| `0x0045b590` | `renderer::Display_List::RenderDecals_3_17_18_4_75` | 3, 17, 18, 4, 75 | L; fog off around 18 and 4; ends with `RenderList(75, true)` |
| `0x0045cfb0` | `renderer::Display_List::RenderInteriorFloors_33_34` | 33, 34 | ext `+0x30/+0x34`; 34 applies container fade |
| `0x0045bce0` | `renderer::Display_List::RenderProjectedShadows62` | 62 | `ext(0x108)->+0x30()`, `->+0xc(1)`, camera position, per-node colour unpack, `->+0x14(1)` on exit |
| `0x0045caf0` | `renderer::Display_List::RenderShadowDecals_7_8_77` | 7, 8, 77 | `SetZWrite(false)` for the whole function; 8 and 77 apply container fade; 77 goes through `PrimArrayEntry::Display` |
| `0x0045aa50` | `renderer::Display_List::RenderLightSet67` | 67 | reads `renderMgr->[0x1c]->+0x42`; per node `SelectLightSet(node->parent->owner, 0, indoors)` = `0x45fb30`, then `prim->SetTint(flags81&1 ? 0 : 1.0f)` |
| `0x0045a890` | `renderer::Display_List::RenderLightSet69` | 69 | same shape |
| `0x0045a770` | `renderer::Display_List::RenderLightSet82` | 82 | same shape |
| `0x004636f0` | *(outside the TU)* `renderer::GetPlayerShadowSlot` ? | — | `g[0x8111e0]` visible → `0x470bc0()` → `0x689980()`, else −1; gates lists 58/60 |
| `0x00459be0` | `renderer::Display_List::RenderDepthOnly58` | 58 | `SetColourWrite(0,0,0,0)` … `SetColourWrite(1,1,1,1)`; a pure z-fill |
| `0x0045d1f0` | `renderer::Display_List::RenderVertexFade2` | 2 | L; `0x200300` ext; ext(0x10b) `+0xac`, tail-jumps `+0xb0` |
| `0x0045e030` | `renderer::Display_List::RenderSpecularFading_14_16` | 14, 16 | L; ext `+0x44/+0x48`, `+0x4c/+0x50`; container fade |
| `0x0045b870` | `renderer::Display_List::RenderDecalsFading_5_19_20_6` | 5, {19}, 20, 6 | L; `RenderList(19, true)` in the middle; fog off around 20 and 6 |
| `0x0045db80` | `renderer::Display_List::RenderNightLights11` | 11 | `0x469e40` (env colour A), `0x200300` ext, fog forced off, container fade |
| `0x0045a4c0` | `renderer::Display_List::RenderInstanced72` | 72 (×1 or ×3) | `arg->valid` (reflection) → one pass with ext `+0x64/+0x68`; else `+0xd4(1)`, pass `+0x6c/+0x70`, pass `+0x74/+0x78`, `+0xd4(0)` |
| `0x0045e240` | `renderer::Display_List::RenderFadingBlend_44_25_32_31_40` | 44, 25, {26,22}, 32, 31, 40 | L; calls `0x45c070(true)`; every list applies container fade |
| `0x0045ac90` | `renderer::Display_List::RenderUnderwaterFading_80_81` | 80, 81 | L; `RenderList(80,true)`, ext `+0x8c`, `RenderList(81,true)`, ext `+0x90` |
| `0x0045c280` | `renderer::Display_List::RenderCbvDefault23` | 23 | L; ext `+0x8c/+0x90`; container fade |
| `0x0045bf30` | `renderer::Display_List::RenderLit_29_50` | 29, {50} | L; ext `+0x20/+0x24`; ends with `RenderList(50, true)` |
| `0x0045da30` | `renderer::Display_List::RenderCardsNight12` | 12 | `0x469e40`, `0x200300` ext; container fade |
| `0x0045ab20` | `renderer::Display_List::RenderLightSet68` | 68 | as 67, plus container fade |
| `0x0045a7f0` | `renderer::Display_List::RenderLightSet83` | 83 | as 82, plus container fade |
| `0x0045a910` | `renderer::Display_List::RenderLightSet71` | 71 | as 69, plus container fade |
| `0x0045a010` | `renderer::Display_List::RenderShadowVolumes_61_63_64` | 61, 63, 64 | `SetCullMode(...)`, `SetStencilOp(...)`, uses `Display_List+0xc8` (`0xff191919`) as the shadow tint; `0x46b4d0` per node |
| `0x0045b240` | `renderer::Display_List::RenderSpecularPass2_15_13` | 15, 13 | L; `0x200300` + `0x103` extensions; ext(0x10b) `+0x54`; `ctx+0xb8(0)` per node; the second specular pass |
| `0x0045ce00` | `renderer::Display_List::RenderFading_41_45` | 41, 45 | ext `+0x30/+0x34`; container fade |
| `0x0045c3c0` | `renderer::Display_List::RenderUnlit_38_37` | 38, 37 | ext `+0x30/+0x34` on both; container fade |
| `0x0045c590` | `renderer::Display_List::RenderAdditive_39_30_24` | 39, 30, 24 | ext `+0x30/+0x34` on 39; container fade |
| `0x00459c80` | `renderer::Display_List::RenderWater65` | 65 | `0x469e40`+`0x469e70` (two env colours), `0x200300` ext `+0x78`/`+0x7c`, per-node `0x6610f0` + `0x460a50`; container fade |
| `0x0045a9b0` | `renderer::Display_List::RenderLightSet70` | 70 | as 71 |
| `0x00459f70` | `renderer::Display_List::RenderWaterSurface66` | 66 | `0x469e40`+`0x469e70`, `0x200300` ext; draws through `PrimArrayEntry::Display` |
| `0x00459810` | `renderer::Display_List::RenderCameraLocked76` | 76 | `View_GetRenderingCamera()`, `0x200300` ext `+0x40(1)`, matrix translated to the camera, container fade, `+0x40(0)` |
| `0x0045bb90` | `renderer::Display_List::RenderReflection` | 46, 47, 72 | §6 |

### Out of scope but adjacent

`0x0045fad0` `TestInteriorVolumes ?` (walks `g[0x8111cc]->[0x28,0x2c)`, calls `0x45f3c0` per
volume) and `0x0045fb30` `SelectLightSet ?` (given a `Renderable` and the indoor flag, picks the
interior or exterior light set via `0x685de0`) belong to the lighting TU but are called from here.
`0x00458690` / `0x00458760` (immediately before `0x004589d0`) belong to `renderer::Canvas` /
`RenderFlowClient`, not to this TU.

---

## 8. The tail of `Render()`: adaptive detail

```c
    ...
    FreeOrphanNodes();
    // one-shot init of the timestamp base at g[0x8108c0]
    u64 now = ReadTimer();                                   // 0x6dc000
    float dt = (now - g[0x8108c0]) * 0.001f;                 // 0x73cb48 == 0.001
    g[0x7bf9fc] = g[0x7bf9fc] * 0.99..f + dt * 0.0..f;       // exponential smoothing
    float t = min(g[0x7bf9fc], g[0x737764] /* 33.3 ms */);
    g[0x0081088c] = 1.0f - (33.3f / t);                      // "how far over budget are we"
    g[0x7bf9f8]   = (1.0f - g[0x81088c]) * g[0x737760] /* 30.0 */;
    if (!g[0x7bfb54]) g[0x0081088c] = 0.0f;                  // the feature switch
    0x686630(1.0f - g[0x81088c] * k);                        // 0x7644ec
```

So `Display_List::Render` measures its own wall time and publishes a 0..1 "over budget" number at
`g[0x0081088c]`, readable through `0x00458a80`, which the LOD/draw-distance code consumes. If the
reimplementation ever wonders why retail's draw distances breathe, this is why.

---

## 9. What to change in `renderer/display_list.{h,cpp}`

Beyond renderspine §6:

1. `DisplayListDrawable` needs `float sortKey; float sortKey2;` (at `+0x48`/`+0x4c`) — aap's struct
   has the comment but not the fields, and `AddContainerElement`'s fade/sort-key writes have
   nowhere to go. Node size is `0x70`, `self` is at `+0x6c`, `linkParent` at `+0x64`.
2. `renderListBits[]` is a **dirty flag**, not "has content": set it in `AddContainerElement`,
   clear it when you sort. Do not use it to skip rendering.
3. Add `IsNodeVisible` to every list walk. Retail culls per node, per frame, with the primitive's
   own sphere/box translated by the node matrix. This is probably the single biggest missing piece
   for performance parity, and it also changes what you see (a submitted node whose renderable has
   since moved out of view still disappears).
4. Sort with the real policy: `CmpShader` for the opaque buckets (material batching), and
   `ComputeDepthKeys` + `CmpKeyThenDepth` (far → near) for the list set in §5. The current
   `SortListByDepth` sorts near-to-far distances and then relies on head-insertion to reverse it —
   equivalent in effect, but it sorts the wrong lists and ignores `sortKey`.
5. `Display()`'s order: use §1's numbered list. Concretely, versus the current `#else` block:
   drop `RenderList(0)` and `RenderList(1)` (never drawn), drop `57` and keep `48` out,
   move `11` to between the `5,19,20,6` group and `72`, add `4` to the `3,17,18,75` group,
   add the second `15`/`13` pass after `61,63,64`, and draw `65/70/66` where §1 step 24 puts them.
6. The `SetColourWrite` discipline matters even without the extension: retail keeps the frame
   buffer's **alpha** channel as a mask and most passes are `SetColourWrite(true,true,true,false)`.
   If your backend has no use for that, at least do not let the blended passes write alpha.
7. `RemovePrimitive` in the repo reads the node back through `*(DisplayListDrawable**)(link+1)`
   with a TODO — that *is* how retail does it (`node->self` at `+0x6c`). Keep it, and delete the
   TODO.
