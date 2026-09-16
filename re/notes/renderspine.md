# The retail render spine: what gets drawn, in what order, and how it is culled

PC retail, all addresses are unpacked-image VAs (`re/dis.sh`).
**[V]** = read out of the disassembly. **[?]** = partly traced / inferred.

---

## 0. TL;DR for `renderer/`

1. `Renderable::Display()` does **not** draw. It decides visibility + fade and then calls
   `DisplayListPrimitive::SetVisible(true)`, which calls `drawable->Display(g_displayList, prim)`,
   which calls `Display_List::AddContainer` → `AddContainerElement` per element. Drawing happens
   later, once, in `Display_List::func2` (= `Render()`), which walks the 84 lists in a fixed order.
2. The `layer` baked into a `DrawablePrimitive` by the loaders is **not** the list index. It is a
   0..44 *material class*; `AddContainerElement` maps `(layer, isFading, isLit, isALUM, shader)`
   → one of 84 lists. aap's switch in `display_list.cpp` is **correct** wherever it is filled in;
   the holes (layer 2, the fade-value writes) are filled in below.
3. Culling in `Renderable::Display` is: distance min/max per element → camera frustum (sphere) →
   occluders. `0x461ad0` returns the **camera** (`g_camera = 0x8111dc`), not a separate culler.
   `0x460980` is the **occluder** test, not the frustum test.
4. `LODShape2` is resolved, passed all the way down and then **dropped on the floor** on PC
   (`InstancePrimitive+0x40` is never assigned). Only `InstanceShape` + `LODShape` survive, and
   both are handed to the pddi instancing extension **together**, each with its own distance band
   that **starts at 0** — so the trunk (LODShape) and the swaying crown (InstanceShape) really are
   drawn on top of each other up close.

---

## 1. The frame spine

### 1.1 Globals **[V]**

| addr | what |
|---|---|
| `0x008111d0` | `renderer::RenderManager*` |
| `0x008111dc` | current camera used for culling (`GetCurrentCamera()` = **0x461ad0**, `return g`) |
| `0x008111d4` | the default camera (0x104 bytes, ctor 0x68b850, vtable **0x0076a684**); `0x8111dc` is set to it at 0x46535d and overridden by the setter **0x00464d00** |
| `0x008111d8` | a second camera (used by `0x461d60`) |
| `0x008108a0` | the `Display_List*` singleton that `DisplayListPrimitive::SetVisible` pushes into |
| `0x00831688` | `pure3d::MatrixStack**` (`->Push(0)/SetMatrix(0,m)/Pop(0)` = vslots 0x44/0x4c/0x48) |
| `0x00831684` | `renderContext` (pddi) |
| `0x007c0a10` / `0x007c0a11` | the two world-geo render toggles (see §4) |
| `0x007c0778` | global instance-LOD distance scale, 1.0 |

### 1.2 RenderManager layout (partial) **[V]**

```
renderer::RenderManager                       (0x104 bytes, ctor 0x467a00)
  +0x04  Scene* scenes[4]
  +0x10  Canvas*            (+0x0c = bool enabled, +0x04 = viewport/target)
  +0x1c  <interiors/stateprop manager>   (0x46a520 -> its byte +0x44 = "camera is indoors")
  +0x1c  ->+0x1d4 wind angle (deg), ->+0x1d8 wind ?
  +0x24  <optional extra pass object>
  +0x30/+0x34/+0x38  three objects that get a "reset" call each frame
  +0x3c  Renderable* (the player/ref renderable, 0x461d40 reads its flags[0x81]&1)
```

### 1.3 Scene layout **[V]**

```
renderer::Scene             vtable 0x00737a80,  GamePlayScene vtable 0x00737a9c
  +0x04  Renderable** begin
  +0x08  Renderable** end
  +0x10  bool enabled
  +0x18  u32 typeMaskFilter        // AND'ed with Renderable::typeMask (+0x54)
  +0x20  Display_List*
vslot 0 dtor | 1 AddRenderable | 2 RemoveRenderable? | 3 Render | 4 Update | 5 SetName(nop)
   Scene:         0x468f70 0x468ac0 0x468b20 0x468b80 0x468c00 0x438400
   GamePlayScene: 0x469050 0x468cc0 0x468d10 0x468aa0 0x468c50 0x468a90
```

### 1.4 The per-frame call chain **[V]**

```
RenderFlowClient (vtable 0x0073782c)
  slot 2  = 0x00465590                                     <-- the frame
    |
    +- 0x00467810  RenderManager::Update(dt)
    |     for s in scenes[0..3]:  s->vslot4(dt)            // GamePlayScene::Update = 0x468c50
    |     |     for r in scene->renderables:
    |     |         0x004740c0  r->Tick(timeinfo)          // r->+0x78 += dt ; fade timer
    |     |         if (!(r->flags80 & 0x10)) continue     // isVisible
    |     |         if (!(r->typeMask & scene->mask)) continue
    |     |         r->vslot9()                            // per-class pre-display
    |     |         r->vslot10()   ===  Renderable::Display()  0x004740f0   <-- §2
    |     |             -> per element: distance + frustum + occlusion + fade
    |     |             -> matrixStack->Push(0); SetMatrix(0, r->matrix)
    |     |             -> DisplayListPrimitive::SetVisible(true)   0x00458f00
    |     |                    -> drawable->vslot10(g_displayList /*0x8108a0*/, prim)
    |     |                        -> Display_List::Display(container, matrix, prim) 0x0045d360 (vslot 7)
    |     |                            for i in container elements:
    |     |                               Display_List::Add(container,i,matrix,prim) 0x0045d3b0 (vslot 8)  <-- §3
    |     |             -> matrixStack->Pop(0)
    |     +- 0x0045ad10   (GamePlayScene only) sort the transparent lists + the water/foam pass
    |     0x46c1d0 / 0x457fa0 / 0x460130   (skidmarks / decals / lights update)
    |
    +- 0x00458710, 0x00464ac0
    +- StatePropManager::ProcessPendingRenderableAdds  0x004b23b0
    +- 0x0046b930-family state resets
    +- 0x004689a0  RenderManager::Render
          for i in {0, 2, 3, 1}:                           // <-- that scene order, verified
              Canvas::RenderScene(scenes[i], x)  0x00458620
                  0x00458390(x)                            // set viewport/camera
                  scene->vslot3()                          // GamePlayScene::Render = 0x468aa0
                      displayList->vslot9()  ==  Display_List::func2() 0x0045e680   <-- §3.4
```

`Scene::Render` (the non-gameplay one, 0x468b80) instead calls `r->vslot11()` on every matching
renderable — immediate-mode drawing for the HUD/frontend scenes. **[V]**

`renderer::CreateInstanceRenderable` (0x467060) puts the new renderable into
`scenes[arg4]`, with `arg4` asserted 0..3 and always **0** from `StatePropManager`. **[V]**

---

## 2. `renderer::Renderable::Display()` — 0x004740f0 **[V]**

### 2.1 Fields it uses

```
Renderable
  +0x14 Matrix   (row3 = world position)
  +0x54 typeMask
  +0x58/+0x5c  DisplayListElement* begin/end        (stride 0x30)
  +0x68 float fadeTimeScale      <-- set by SetFadeDist()   (NOT a distance, see §2.4)
  +0x6c float fade               current global fade of the whole renderable (1 = invisible)
  +0x70 float fade2              a second fade channel; the two are max()'d
  +0x74 float fadeTarget
  +0x78 float timeSinceDrawn     (+= dt in Tick, = 0 at the end of Display)
  +0x80 flags:  0x01 doDistanceTest   0x02 doFade   0x04 fadeDirection
                0x10 isVisible        0x40 useLastElementMaxDist (multi-element LOD)
                0x80 useBoxBoundsFromPose (state props)
  +0x81 flags:  0x02 matrixDirty
DisplayListElement (0x30)
  +0x00 drawDist[0] min   +0x04 drawDist[1] max   +0x08 drawDist[2] fadeBand
  +0x0c DisplayListPrimitive (0x20)
  +0x2c bool isFading
```

### 2.2 Pseudo-C

```c
void Renderable::Display()
{
    Camera *cam = GetCurrentCamera();                       // 0x461ad0  -> g[0x8111dc]
    Drawable *d0 = elements[0].prim.GetDrawable();

    Sphere lsph;                                            // local bounding sphere
    Vector wsph;                                            // world sphere centre
    if (flags80 & 0x80) {                    // state props: build it from the pose bbox
        Matrix *pose = d0->vslot13()->+0x10;                // 0x4740fa..0x474200
        Box b = TransformBox(pose, d0->+0x10 /*local bbox*/);
        lsph = BoxToSphere(&b);              // 0x427080 union, 0x4202d0 box->sphere
        wsph = lsph.centre;
    } else {
        lsph = *(Sphere*)(d0 + 0x28);        // Drawable::boundingSphere {x,y,z,r}
        Matrix_TransformPoint(&this->matrix, &lsph.centre, &wsph);   // 0x6610f0
    }

    Vector camPos;  cam->GetPosition(&camPos);               // vslot24 = [vtbl+0x60] (0x69e690)

    Vector ref;
    if (!this->GetDistanceRefPos(&ref))                      // vslot15 = [vtbl+0x3c]
        ref = this->matrix.row3;                             // base impl 0x559000 returns false
                                                             // WorldGeo 0x4714e0 -> otherPosition,
                                                             //   returns (flags90 & 1)
    float dist  = length(ref - camPos);
    float scale = cam->+0x10;  if (scale > 1.0f) scale = 1.0f;     // clamp to <= 1
    dist *= scale;

    int n = (elements_end - elements_begin) / 0x30;
    int count = n;
    if ((flags80 & 0x40) || !(flags80 & 0x01))
        if (n > 1) count = 1;                                // normally only element 0 is considered
    if (count <= 0) return;

    for (int i = 0; i < count; i++) {
        DisplayListElement *el = &elements[i];
        Drawable *d = el->prim.GetDrawable();
        float alpha = 0.0f;

        if (flags80 & 0x01) {                                // ---- distance test ----
            float minD = el->drawDist[0];
            float maxD = el->drawDist[1];
            if ((flags80 & 0x40) && count > 1)
                maxD = elements[count-1].drawDist[1];        // share the last element's far plane
            float fade = el->drawDist[2];

            if (!(dist <  maxD))          goto hide;
            if (!(dist >= minD))          goto hide;
            if (!cam->SphereVisible(&wsph, lsph.r))  goto hide;   // vslot22 = [vtbl+0x58] (0x69e580)
            if (Occluded(wsph, lsph.r) == 2)         goto hide;   // 0x460980, 2 == fully occluded

            if (!(flags80 & 0x02)) goto draw;                // ---- fade band ----
            if (count > 1) {                                 // multi-element LOD chain
                if (i == 0) { alpha = 0.0f; }
                else {
                    float fadeOut = (dist - (maxD - fade)) / fade;
                    float fadeIn  = (minD + fade*0.5f - dist) / (fade*0.5f);
                    if      (fadeOut >= 0.0f && fadeOut <= 1.0f) alpha = fadeOut;
                    else if (fadeIn  >= 0.0f && fadeIn  <= 1.0f) alpha = fadeIn;
                    else                                          alpha = 0.0f;
                }
            } else {                                         // single element
                float inv = 1.0f / fade;
                float fadeOut = (dist - (maxD - fade)) * inv;
                if (fadeOut >= 0.0f && fadeOut <= 1.0f) alpha = fadeOut;
                if (minD > 0.0f) {
                    float fadeIn = (fade + minD - dist) * inv;
                    if (fadeIn >= 0.0f && fadeIn <= 1.0f) alpha = fadeIn;
                }
            }
            // combine with the renderable-wide fade
            float g = max(this->fade /*+0x6c*/, this->fade2 /*+0x70*/);
            float fadeAmount = alpha * (1.0f - g) + g;       // 0 = fully opaque, 1 = gone

            if (fadeAmount > 0.0f) {
                if (!el->isFading) {
                    el->isFading = true;
                    el->prim.RemoveFromList();               // 0x458ea0 - force a re-Add
                    d->SetFading(true);                      // Drawable vslot 17 = [vtbl+0x44]
                }
                d->SetFadeAmount(fadeAmount);                // Drawable vslot 18 = [vtbl+0x48]
            } else if (el->isFading) {
                el->isFading = false;
                el->prim.RemoveFromList();
                d->SetFading(false);
                d->SetFadeAmount(0.0f);
            }
        } else if (this->typeMask == 1 /*SkyRenderable*/ && (flags80 & 0x02)) {
            d->SetFadeAmount( max(this->fade, this->fade2) );
        }
    draw:
        if (flags81 & 0x02)  el->prim.RemoveFromList();      // matrix changed -> re-add
        this->timeSinceDrawn = 0.0f;
        matrixStack->Push(0);
        matrixStack->SetMatrix(0, &this->matrix);
        el->prim.SetVisible(true);                           // 0x458f00 -> feeds Display_List
        matrixStack->Pop(0);
        continue;
    hide:
        el->prim.SetVisible(false);                          // 0x458f00(prim, 0)
    }
    flags81 &= ~0x02;
}
```

Notes:
* `DisplayListPrimitive::SetVisible` (**0x00458f00**) is the only place that touches `Display_List`:
  `bit1` = "currently in the list". Setting visible when not in the list calls
  `drawable->vslot10(g_displayList, this)`; clearing it calls `0x458a20` (remove all my nodes).
  This is why `RemoveFromList()` before a fade/matrix change is needed — the node caches the matrix.
* `Renderable::Tick` = **0x004740c0**: `+0x78 += dt; if (flags80 & 2) UpdateFade(dt)`.

### 2.3 `Renderable::UpdateFade(float dt)` — 0x00473d90 **[V]**

```c
float Renderable::UpdateFade(float dt) {
    if (!(flags80 & 0x04)) {
        if (fade > fadeTarget) {
            fade -= (1000.0f / fadeTimeScale) * dt;         // 0x72f9b4 = 1000.0
            if (fade < fadeTarget) fade = (fade > 1.0f) ? fade : fadeTarget;  // clamp
        }
    } else if (fade < fadeTarget) {
        fade = min(fade + (1000.0f/fadeTimeScale)*dt, fadeTarget);   // 0x402990 = fmin
    }
    return max(fade, fade2);
}
```

### 2.4 `SetFadeDist` — 0x00473aa0, and what `SetFadeDist(3000|0)` means **[V]**

```c
void Renderable::SetFadeDist(float t) {
    flags80 &= ~0x04;      // fade OUT->IN direction
    fade       = 1.0f;     // start fully faded out
    fadeTarget = 0.0f;
    fadeTimeScale = t;     //  +0x68
}
```
`+0x68` is **not a distance** — `UpdateFade` uses `1000/+0x68` as the per-second rate. So
`SetFadeDist(3000)` = "fade in over 3.0 s"; `SetFadeDist(0)` = divide by zero → ±inf → the fade
completes on the very first tick, i.e. **appear instantly**. State props use `SetFadeDist(250)`
= 0.25 s. The world-geo loader picks 3000 or 0 from `g[0x8111d0]->0x41c110()` — i.e. streamed-in
world geo fades in over 3 s in one mode and pops in in the other.

---

## 3. `renderer::Display_List`

### 3.1 Object layout **[V]**

```
renderer::Display_List : pure3d::Entity          vtable 0x00737738
  +0x30  Node* lists[84]... actually: ptr to an array of 84 LinkedLists, 12 bytes each
  +0x3c  u8 listNonEmpty[84]          // +0x3c .. +0x8f  -> exactly 84, this is where the 84 comes from
  +0x90  Node* freeList               // intrusive, node link at +0x00
vslots: 0..6 = Entity (AddRef/Release/GetRef/dtor 0x45f370/0x652590/Clone/SetName)
        7 = 0x45d360  Display(DrawableContainer*, Matrix*, DisplayListPrimitive*)   "AddContainer"
        8 = 0x45d3b0  Add(DrawableContainer*, int idx, Matrix*, DisplayListPrimitive*)
        9 = 0x45e680  Render()      <-- func2, the big one
```
`0x45b0c0` confirms 84: `for (off = 0; off < 0x3f0 /*84*12*/; off += 12)`.

```
Display_List node (>= 0x6c bytes)
  +0x00  ListLink  link          // in lists[listID]
  +0x08  Matrix    matrix        // world matrix captured at Add time
  +0x48  float     sortKey       // = container->+0x3c, overwritten by some layers (1.0/0.5/0.0)
  +0x4c  float     sortKey2      // secondary sort key
  +0x50  PrimEntry* elem         // = &container->elements[idx]   (16-byte stride)
  +0x54  DrawableContainer* container
  +0x58  Shader*   shader        // = elem->prim->GetShader()
  +0x5c  DisplayListPrimitive* parent   (0 = orphan, freed at end of frame)
  +0x60  LinkedList* myList
  +0x64  ListLink  linkParent    // in parent->children
```

### 3.2 `Display_List::Display` (AddContainer) — 0x0045d360 **[V]**

```c
void Display_List::Display(DrawableContainer *c, Matrix *m, DisplayListPrimitive *p) {
    int n = (c->elementsEnd /*+0x48*/ - c->elementsBegin /*+0x44*/) / 16;
    for (int i = 0; i < n; i++) this->Add(c, i, m, p);      // vslot 8
}
```

### 3.3 `Display_List::Add` (func1) — 0x0045d3b0 **[V]**

```c
void Display_List::Add(DrawableContainer *c, int idx, Matrix *m, DisplayListPrimitive *p)
{
    bool isFading = c->IsFading();                          // container vslot 19 = [vtbl+0x4c]
    Node *nd = freeList.head;  if (!nd) return;             // silently dropped when full

    Matrix *world = matrixStack->GetMatrix();               // 0x67e7e0 on g[0x831690]
    nd->matrix = m ? (*m) * (*world) : *world;

    nd->container = c;
    nd->elem      = &c->elements[idx];
    nd->shader    = nd->elem->prim->GetShader();            // prim vslot 9  = [vtbl+0x24]
    nd->sortKey   = c->+0x3c;                               // the container's sort/fade value

    DrawablePrimitive *prim = nd->elem->prim;
    Shader *sh = prim->GetShader();
    int listID = MapLayerToList(prim->GetLayer(), isFading, prim, sh, p, nd);   // table below
    if (listID < 0) return;                                 // layers 9, 43 and default: dropped

    freeList.Remove(nd);
    if (p) { nd->parent = p; p->children.Insert(&nd->linkParent); }
    else     nd->parent = nil;
    lists[listID].Insert(nd);
    nd->myList = &lists[listID];
    listNonEmpty[listID] = 1;
}
```

`prim->GetLayer()` = `Get_0xC(prim)` = `prim->+0x0c` (what `SetLayer` 0x703300 writes).
`isLit` = prim vslot 11 = `[vtbl+0x2c]`, `isALUM` = prim vslot 12 = `[vtbl+0x30]`,
`GetSomeMask` = vslot 7 = `[vtbl+0x1c]`.

#### The full layer → list table **[V]** (jump table at `0x0045d8dc`)

`F` = isFading, `L` = isLit, `A` = isALUM.

| layer | list | extra |
|---|---|---|
| 0 | `F ? (L?51 : A?56:55) : (L?49 : A?53:52)` | |
| 1 | mask==2 → 66; mask==4\|\|8 → 65; else shType 0x0a → 77; 0x07 → 75; else `L?50:54` | see the *fade writes* below |
| **2** | `prim->+0x48 = 6.0f;` then: `prim->+0x68 != 0` → **61**; else if `mask==0x20` → `(parent->renderable->flags81 & 1) ? 62 : 63`; else → `(flags81 & 1) ? 62 : 64` | **this is the layer-2 hole in aap's switch** |
| 3 | 11 | |
| 4 | 12 | |
| 5 | `F?14:13` | |
| 6 | `F?16:15` | |
| 7 | `F ? (A?40:41) : (A?35:36)` | |
| 8 | `A?38:37` | |
| 9 | *dropped* | |
| 10 | 39 | |
| 11 | `F?32:27` | |
| 12 | 29 | |
| 13 | `F?31:28` | |
| 14 | 30 | |
| 15 | `F?25:21` | |
| 16 | 23 | |
| 17 | `F?26:22` | |
| 18 | 24 | |
| 19 | `F?45:42` | |
| 20 | `F?44:43` | |
| 21 | `F?6:4` | |
| 22 | `F?5:3` | |
| 23 | `F?20:18` | |
| 24 | `F?19:17` | |
| 25 | `F?34:33` | |
| 26 | `F?10:9` | |
| 27 | 60 | |
| 28 | 76 | |
| 29 | `F?1:0` | |
| 30 | 57 | |
| 31 | `F?68:67` | |
| 32 | `F ? (blend? 70 (+ node->sortKey=0) : 71) : (blend?70:69)` | `blend` = `IsXXXBlendMode(sh)` 0x458ae0 |
| 33 | `shType == 0x0d (VERTEXFADE) ? 2 : 59` | |
| 34 | `(shType==9 && blendMode==1) ? (F?81:79) : (F?80:78)` | |
| 35 | `(L \|\| A) ? 50 : 54` | then the common `if (F) node->sortKey = 1.0f` |
| 36 | 58 | |
| 37 | `F?8:7` | |
| 38 | 47 | |
| 39 | 46 | |
| 40 | 72 | instanced eco props |
| 41 | 73 | instanced eco props |
| 42 | 74 | instanced eco props |
| 43 | *dropped* | |
| 44 | `F?83:82` | |

Max list index = 83 → **84 lists**, confirmed twice.

**Fade / sort-key writes** (aap's `// TODO: set shit`):
* Common tail for layers 0, 1(non-mask), 32, 35: `if (isFading) node->sortKey = 1.0f;`
* Layer 1 with mask 2/4/8 (lists 65/66): `node->sortKey = IsXXXBlendMode(sh) ? 1.0f
  : (0x458b00(sh) ? 0.5f : 0.0f)`.
* Layer 1 with shType 0x0a/0x07 (77/75): common tail only.
* Layer 2: writes **6.0f into the primitive** (`prim->+0x48`), not into the node.
* Default `node->sortKey = container->+0x3c`.

### 3.4 Sorting **[V]**

Three comparators, all on `node->sortKey (+0x48)` / `sortKey2 (+0x4c)`:
* `0x00458ff0` — `|a.k - b.k| > 0.001` ? compare `k` : compare `k2`.
* `0x00459090` — same, but falls back to the `+0x58` (shader) pointer when the keys tie.
* `0x00459060` — pure shader-pointer ordering (`shader->+0x10`), i.e. batch by material.
* `0x004589e0` — plain `k` compare (used for lists 46/47).

`GamePlayScene::Update` finishes by calling **`0x0045ad10`** on the display list, which sorts a
long list of the transparent/blended lists with these comparators and then runs the water/foam
pass (list 0 via `0x459120`, list 66 via `0x4591e0`) — so the sort happens **after** all
renderables have added themselves and **before** `func2`.

### 3.5 `Display_List::RenderList(int listID, bool applyContainerFade)` — 0x00459910 **[V]**

```c
for (Node *nd = lists[listID].head; nd; nd = nd->next) {
    DrawablePrimitive *prim = nd->elem->prim;
    bool opaque = !prim->IsLit() && !prim->IsALUM();
    if (applyContainerFade) prim->vslot14( nd->container->vslot20() );   // [vtbl+0x38] <- [+0x50]
    if (opaque)             prim->vslot15( nd->container->vslot21() );   // [vtbl+0x3c] <- [+0x54]
    matrixStack->Push(0);  matrixStack->SetMatrix(0, &nd->matrix);
    PrimArrayEntry::Display(nd->elem);
    matrixStack->Pop(0);
    if (opaque) prim->vslot15(1.0f);
}
```

### 3.6 `Display_List::func2` = `Render()` — 0x0045e680: the verified list order **[V]**

There is one big conditional. Let

```c
bool indoors =  g_renderMgr->[0x1c]->byte_0x44                      // 0x46a520
             && (   g_renderMgr->[0x3c]->flags81 & 1                // 0x461d40
                 || (LightMgrTestCamera() && 0x55f0b0(g[0x8251a8])) );   // 0x461d60
```
The block **{36, 42, 73, 52, 53, 3, 17, 18, 4, 75, 62, 67, 69, 82, 33, 34, 49, 54, 68, 83}** is
emitted **early when `!indoors`** and **late (after everything else) when `indoors`** — i.e. when
the camera is inside, the outside world is drawn last. Everything else is unconditional.

**Outdoor order** (helper function → lists it renders, in order):

```
 0x459a00      46, 47
 0x45d140      59
 [!indoors]    0x45ccd0      36, 42
 [!indoors]    0x45a680      73                        (two passes over the same list)
 [!indoors]    RenderList(52); RenderList(53)
 0x45c800      21, (0x45c070: 26, 22), 27, 28, 35, 43
 0x45a620      74
 0x45dcf0      9, 10
 0x45deb0      13, 15
 0x45ac10      78, 79                                  <-- 78 BEFORE 79
 [!indoors]    0x45b590      3, 17, 18, 4, 75
 [!indoors]    0x45cfb0      33, 34
 [!indoors]    0x45bce0      62
 [if g[0x7bfb55] and lists 7/8 non-empty]  0x45caf0    7, 8, 77
 [!indoors]    RenderList(49); 0x45aa50 -> 67; 0x45a890 -> 69; 0x45a770 -> 82
 0x4636f0 ; 0x459be0  58 ; (inline) 60
 0x45d1f0      2
 0x45e030      14, 16
 0x45b870      5, 19, 20, 6
 0x45db80      11                                      <-- missing from aap's current order
 0x45a4c0      72
 0x45e240      44, 25, (0x45c070: 26, 22), 32, 31, 40
 0x45ac90      80, 81
 0x45c280      23
 0x45bf30      29, 50
 0x45da30      12
 [!indoors]    RenderList(54)
 RenderList(51)
 [!indoors]    0x45ab20 -> 68 ; 0x45a7f0 -> 83
 0x45a910      71
 0x45a010      61, 63, 64
 0x45b240      15                                      <-- 15 a SECOND time
 [indoors]     <the whole deferred block listed above>
 0x45ce00      41, 45
 0x45c3c0      38, 37
 0x45c590      39, 30, 24
 0x459c80      65
 0x45a9b0      70
 0x459f70      66
 RenderList(55); RenderList(56)
 0x459810      76
 0x45b0c0      FlushOrphans()   -- every node with parent==0 goes back to the free list
```

Lists never reached by any renderer in the image: **1, 48, 57** (and 0 / 66 are done in the
`0x45ad10` water pass, not in `func2`). aap's "missing ones" 19, 26, 22, 75, 78, 50 are all
accounted for above.

`0x45a4c0` (72), `0x45a680` (73) and `0x45a620` (74) are the **instancing** renderers: they turn
on the pddi extension `0x200200` and for every node do
`InstancePrimitive::PreDisplay(0x46f030); prim->Display(0x46f110); InstancePrimitive::Draw(0x46f0f0)`.

---

## 4. Frustum culling and occlusion

### 4.1 There is no separate "culler" object **[V]**

`0x00461ad0` is `return g[0x8111dc]` — the **current camera**, a `pure3d` camera-ish object with
vtable `0x0076a684`, size 0x104, ctor 0x68b850. Relevant slots:

| slot | `[vtbl+n]` | addr | meaning |
|---|---|---|---|
| 8 | 0x20 | 0x69e250 | returns two floats (draw-distance scale + LOD bias), used by `InstancePrimitive` |
| 19 | 0x4c | 0x69e400 | `TestSphereCameraSpace(Vector *p, float r)` — the actual frustum test |
| 22 | 0x58 | 0x69e580 | `SphereVisible(const Vector *worldCentre, float r)` |
| 24 | 0x60 | 0x69e690 | `GetPosition(Vector *out)` → `+0xbc..+0xc4` |

```c
bool Camera::SphereVisible(const Vector *c, float r) {         // 0x69e580
    if (!this->+0xcc) this->vslot30();                          // recompute the frustum if dirty
    Vector local; Matrix_TransformPointInverse(&this->+0x4c, c, &local);   // 0x6610f0
    return this->TestSphereCameraSpace(&local, r);
}

bool Camera::TestSphereCameraSpace(const Vector *p, float r) { // 0x69e400
    if (!(r + p->z >= near /*+0x1c*/))                 return false;
    if (!(p->z - r <=  far /*+0x20*/))                 return false;
    if (!(n0x*p->x + n0z*p->z <= r))                   return false;   // +0x2c,+0x30
    if (!(n1x*p->x + n1z*p->z <= r))                   return false;   // +0x34,+0x38
    if (!(n2y*p->y + n2z*p->z <= r))                   return false;   // +0x3c,+0x40
    if (!(n3y*p->y + n3z*p->z <= r))                   return false;   // +0x44,+0x48
    return true;
}
```
So: 6 planes (near/far + 4 sides), stored as 2-component normals in camera space; the sphere is
transformed into camera space and tested. The frustum is rebuilt lazily from the camera when
`+0xcc` (the dirty flag) is clear.

### 4.2 `0x00460980` is the **occluder** test, not the frustum **[V]**

```c
// int Occluded(Vector centre /*by value, 3 dwords*/, float radius)
int Occluded(Vector c, float r) {
    g[0x810cdc]++;                                   // stat: spheres tested
    if (g[0x810cfc] != 1) return 0;                  // occlusion globally off -> visible
    int result = 1;
    for (i = 0; i < g[0x810d04] /*numOccluders*/; i++) {
        Occluder *o = &occluders[i];                 // array at 0x810d0c, stride 0x98
        g[0x810ce8]++;                               // stat: occluder tests
        if (o->flagA && BoxIntersectsSphere(&o->box, &sphere))  continue;   // 0x6633c0
        int t = TestAgainstOccluder(o, ..., &sphere);                       // 0x460900
        if (t == 2) { g[0x810ce0]++; return 2; }     // fully occluded
        if (t == 0) result = 0;
    }
    return result;   // 2 == cull
}
```
These are the `0x0880000a` occluders from `renderables.md §7` (loaded per shell file), so the
occlusion volumes really are the shipped chunk data. Only a return of exactly **2** culls.

### 4.3 Where culling is applied **[V]**

* **Per Renderable element**, in `Renderable::Display` (§2.2): frustum then occlusion, on the
  element-0 drawable's bounding sphere transformed by the renderable matrix.
* **Per instance matrix packet**, in `InstancePrimitive::Display` (§5.3), during the *render*
  pass — and the result is written into the packet, not used to skip anything on the CPU.
* **Per world-geo sub-primitive**, in `WorldGeoRenderable::Display`'s custom path (§4.5).
* Nothing is culled in `Display_List::Add` or in `func2`.

### 4.4 The two toggles **[V]**

`g_byte[0x007c0a10]` and `g_byte[0x007c0a11]` are only read by `WorldGeoRenderable::SetVisible`
(0x471554/0x471564) and `WorldGeoRenderable::Display` (0x471653/0x47167b/0x471694). No writer
exists in the image outside of a debug/var table, so they are debug render switches:
`0x7c0a10` = "draw plain world geo", `0x7c0a11` = "draw details / skyline / shells / low-LOD".

### 4.5 `WorldGeoRenderable::Display` — 0x00471640 **[V for the dispatch, [?] for the inner loop]**

```c
void WorldGeoRenderable::Display() {
    u8 f = flags90;
    if (f & 0x10)          { g[0x7c0a11] ? Renderable::Display() : Hide(); return; }  // low_LOD_
    if (!(f & 0x0e))       { g[0x7c0a10] ? Renderable::Display() : Hide(); return; }  // plain
    if (!g[0x7c0a11])      { Hide(); return; }
    // ---- custom path: details_ / cbvlitdecals_ / skyline_ / shells_ / underwater_ ----
    Camera *cam = GetCurrentCamera();
    DrawableContainer *comp = GetElementDrawable(0);
    Matrix base = comp->vslot13()->+0x10;              // the composite's pose matrix table, base
    Sphere s = comp->+0x28;
    cam->GetPosition(&camPos);
    bool coarseVisible = cam->SphereVisible(&s.centre, s.r);
    float fade = this->UpdateFade(0.33f);              // fixed dt, const 0x3ea8f5c3
    matrixStack->Push(0); matrixStack->SetMatrix(0, &base);
    for (int i = 0; i < numPrimitives /*+0x9c*/; i++) {
        Drawable *d   = primitives[i].GetDrawable();   // +0x94, 0x20 stride
        u16 poseID    = poseIDs[i];                    // +0x98
        Matrix *pose  = &comp->vslot13()->+0x10 [poseID];   // 0x40 stride  (shl esi,6)
        matrixStack->vslot25(0, pose);                 // concat this sub-drawable's pose
        if (coarseVisible) {
            // per-primitive: transform the sub-drawable's own sphere, distance, cull, fade,
            // then primitives[i].SetVisible(true/false)
            ...
        }
    }
    matrixStack->Pop(0);
}
```
The point of the `primitives[]` / `poseIDs[]` arrays is exactly this: a `details_*` composite is
one Renderable but every sub-drawable is distance-tested, culled and faded **individually**,
using its own pose matrix out of the composite's matrix table. `SetVisible` (0x471570) forwards
to all `numPrimitives` primitives.

---

## 5. Instances / LOD (the tree question)

### 5.1 The chain **[V]**

```
StatePropManager::CreateInstanceRenderable      0x004b0250
    shape    = MakeKey32("InstanceShape", template->mModelUid)
    lodShape = (flags170 bit19) ? MakeKey32("LODShape",  uid) : 0
    lod2     = (flags170 bit20) ? MakeKey32("LODShape2", uid) : 0
    sway     = swayTable[(flags170 >> 21) & 7]              // 0x73bab0 {0,.5,1,4,50,200}
    renderer::CreateInstanceRenderable(shape, lodShape, lod2,
                                       template->mInventory /*+0x4c*/,
                                       0                     /*scene index, 0..3*/,
                                       template->+0x114      /*instancing bucket*/,
                                       sway)                                          // 0x467060
        g0 = inventory->Find<Geometry>(shape);   if (!g0) return 0
        g1 = shape lookup of lodShape (0x465500)
        g2 = shape lookup of lod2    (0x465500)
        new InstanceRenderable(g0, g1, g2, bucket, sway)        0x0046f8e0
            new InstanceContainer(g0, g1, g2, bucket, sway)     0x0046f840
                new InstancePrimitive(g0, g1, g2, bucket, sway) 0x0046f2e0
        scenes[arg4]->AddRenderable(r)          // vslot 1
        r->vslot10()
        new RenderableHandle(r)                 0x00461870
        renderer::SetCullDistance(r, template->mVisibilityEnd, (template->+0x50>>2)&1)  0x00463f80
```

### 5.2 `InstancePrimitive::ctor` — 0x0046f2e0 **[V]** — LODShape2 is discarded

```c
InstancePrimitive::InstancePrimitive(Geometry *g0, Geometry *g1, Geometry *g2,
                                     int bucket, float sway)
{
    DrawablePrimitive::ctor();
    flags74 |= 6;
    +0x38 = 0;  +0x3c = 0;  +0x40 = 0;
    +0x44 = 10.0f;   +0x48 = 0;   +0x4c = sway;   +0x54 = 0;   +0x78 = 0;

    // ---- layer ----
    if (bucket == 1)                                              layer = 40;
    else if (g0 && (g0->+0x38 & 1))                               layer = 40;
    else if (g1 && (g1->+0x38 & 1))                               layer = 40;
    else if (g0 && (g0->+0x38 & 2))                               layer = 40;
    else if (g1 && (g1->+0x38 & 2))                               layer = 40;
    else {
        layer = 41;
        if (g0->+0x08 is one of g[0x81137c..0x811388])            layer = 42;
    }
    // -> Display_List lists 72 / 73 / 74

    AddRef(g0); Release(+0x38); +0x38 = g0;      // the InstanceShape
    AddRef(g1); Release(+0x3c); +0x3c = g1;      // the LODShape
    //  g2 (LODShape2) is NEVER STORED.  +0x40 stays 0 forever.

    bbox = union of (+0x38)->bbox, (+0x3c)->bbox, (+0x40)->bbox;   // the third is a no-op
    g_pddiExtInstancing = renderContext->GetExtension(0x200200);
    matrixPacketList = new WindyMatrixPacketList(0x2c);            // +0x78
}
```

**Consequence:** `LODShape2` (the billboard card) is looked up, ref-counted and thrown away on PC.
The 3-LOD branch of the distance solver (§5.4) requires `+0x40 != 0` and is therefore dead code.
Retail PC runs at most **two** instance LODs: the `InstanceShape` and the `LODShape`.

### 5.3 `InstancePrimitive::Display` — 0x0046f110 **[V]**

```c
void InstancePrimitive::Display() {          // called from the LIST renderer, not from Renderable
    int n = packets->count;  if (n <= 0) return;
    Camera *cam = GetCurrentCamera();
    Vector camPos; cam->GetPosition(&camPos);                 // vslot24
    for (int i = 0; i < n; i++) {
        MatrixPacket *p = packets->GetPacket(i);   if (!p) continue;
        Sphere s = *(Sphere*)(p + 0x4c);
        if (!cam->SphereVisible(&s.centre, s.r)) { p->+0x60 = 1; continue; }   // +0x60 == CULLED
        p->+0x60 = (Occluded(s.centre, s.r) == 2);
    }
    if (this->sway /*+0x4c*/ > 0.0f) {
        packets->SetWindAngle(renderMgr->[0x1c]->+0x1d4 * DEG2RAD);   // 0x649c80
        packets->SetWindStrength(this->sway);                          // 0x649cc0
        packets->+0x20 = renderMgr->[0x1c]->+0x1d8;
    }
    ClampWind(packets, this->+0x54);                                   // 0x650ba0
    g_pddiExtInstancing->vslot4(packets);                              // [vtbl+0x10], "use this list"
}
```
Note `MatrixPacket+0x60` is **`culled`**, not `visible` — the earlier note in `instances.md §5.5`
has the polarity backwards.

### 5.4 Where the LOD split actually happens — `0x0046f030` + `0x0046edc0` **[V]**

The list renderer (`0x45a4c0` / `0x45a680` / `0x45a620`) calls, per node:

```
0x0046f030  InstancePrimitive::PreDisplay()
     flags74 |= 6
     grab the camera's view direction into g[0x811370..0x811378]      // 0x461ac0 -> vslot16
     ComputeLODBands(&start[3], &end[3], &c[3], &fadeWidth[3])        // 0x0046edc0
     g_pddiExtInstancing->vslot3(                                     // [vtbl+0x0c]
            +0x38 /*InstanceShape*/, start[0], end[0], c[0], fadeWidth[0],
            +0x3c /*LODShape*/,      start[1], end[1], c[1], fadeWidth[1],
            +0x40 /*always NULL*/,   start[2], end[2], c[2], fadeWidth[2],
            (+0x70 != 0) );
0x0046f110  InstancePrimitive::Display()       // per-instance cull + wind (above)
0x0046f0f0  g_pddiExtInstancing->vslot5()      // [vtbl+0x14] -- the actual instanced draw
```

So **all surviving shapes are submitted to the instancing extension in one call**, each with its
own distance band, and the extension does the per-instance LOD pick and cross-fade. The engine
never picks one drawable on the CPU.

`ComputeLODBands` = **0x0046edc0** (`ret 0x10`, 4 out-arrays of 3 floats each):

```c
cam->vslot8(&a, &b);                                   // [vtbl+0x20]
float camScale = (b > 1.0f) ? 1.0f : b;
float radius   = (shape && !lod) ? shape->+0x34 : lod->+0x34;     // Drawable bounding radius
float D        = g[0x7c0778] /*1.0*/ * this->+0x44 /*10.0*/ * (2.0f - b) - radius;

if (shape && !lod && !lod2) {            // --- one level ---
    start[0] = 0;
    end[0]   = radius + camScale;   this->+0x48 = end[0];
    c[0]     = 0;
    fadeWidth[0] = max( (end[0]-start[0]) * 0.4f, 40.0f );        // 0x731044, 0x730c88
}
else if (shape && lod && !lod2) {        // --- two levels: the ONLY live case on PC ---
    // level 1 = the LODShape (far)
    start[1] = 0;
    end[1]   = D + camScale;        this->+0x48 = end[1];
    c[1]     = 0;
    fadeWidth[1] = max( (end[1]-start[1]) * 0.4f, 40.0f );
    // level 0 = the InstanceShape (near)
    start[0] = 0;
    end[0]   = (end[1] - start[1] - D) * 0.6f + radius;           // 0x747680 = 0.6
    c[0]     = 0;
    fadeWidth[0] = max( (end[0]-start[0]) * 0.3f, 20.0f );        // 0x73050c, 0x746614
}
else if (shape && lod && lod2) {         // --- three levels: DEAD, +0x40 is always 0 ---
    ... end[0] = D*0.1 + radius ; end[1] = fadeWidth[0]*0.5 + radius ; end[2] = ... 0.2 ...
}
```

**Answer to the tree question:** both bands start at **0**. The `LODShape` (the whole tree with the
trunk) is drawn from distance 0 out to `end[1]`, and the `InstanceShape` (the wind-swayed crown
only) is drawn from 0 out to `end[0]` (< `end[1]`), fading out over `fadeWidth[0]`. So up close
you get **trunk + static crown from LODShape *plus* the swaying crown from InstanceShape drawn on
top of it**, and past `end[0]` only the LODShape remains. That is why the `InstanceShape` contains
only the crown and only 23 bark verts (the top of the trunk, so the swaying geometry has something
to blend into). **[V for the band maths, [?] for whether the extension additionally suppresses
level 1 inside level 0's range — that decision lives inside the pddi extension (vtable
`g[0x811368]`, created with `renderContext->GetExtension(0x200200)`), which I did not trace.]**

`StatePropManager` only supplies the outer cull radius (`mVisibilityEnd`, via
`renderer::SetCullDistance` 0x463f80) and the global scale (0x464040 → `g[0x7c0778]` / the camera's
`+0x10`). It never switches LODs. `SActiveRenderable::lod[0/1]` is the *undamaged / destroyed*
mesh pair, not a distance LOD.

---

## 6. What `renderer/` in the repo should change

Ordered roughly by how much it will change what you see on screen.

1. **`Renderable::Display` (renderer/renderable.cpp)** — replace the crude sphere-distance test with
   §2.2. Specifically:
   * distance is `|refPos - cameraPosition|`, where `refPos` comes from the *virtual*
     `GetDistanceRefPos` (WorldGeo → `otherPosition` when `flags90 & 1`), not from the bound sphere;
   * the frustum/occlusion test uses the **element-0 drawable's own sphere transformed by the
     renderable matrix**, not `refPos`;
   * order is min/max → frustum → occluders;
   * implement the fade band exactly (`alpha` from `fadeOut`/`fadeIn`, then
     `fadeAmount = alpha*(1-g) + g`), and drive `DrawableContainer::SetFading/SetFadeAmount` from it,
     calling `RemoveFromList()` on the transition so the node is re-added;
   * only element 0 is considered unless `flags80 & 1` and `!(flags80 & 0x40)`.
2. **`SetFadeDist` is a fade *time*, not a distance.** Rename it and implement `UpdateFade` (§2.3);
   `+0x68 = 3000` means 3 s (rate `1000/3000` per second), `0` means instant.
3. **`Display_List::AddContainerElement`** — fill the layer-2 case (shadows, lists 61..64, and the
   `prim->+0x48 = 6.0f` write), and add the sort-key writes for layers 0/1/32/35. Everything else in
   your switch matches the retail jump table byte for byte.
4. **`Display_List::Display`** — use the order in §3.6. Concrete fixes to the current `#else` block:
   `78` before `79`; add `11` (between `5,20,6` and `72`); add `19` inside the `5,20,6` group;
   add `26, 22` inside both the `21,27,28,35,43` group and the `44,25,...` group; add `75` at the
   end of the `3,17,18,4` group; add the second `15` after `61,63,64`; `1`, `48` and `57` are never
   rendered; `0` and `66` belong to the water pass (`0x45ad10`), not to `Render()`.
5. **Add the sort pass.** Before rendering, sort the transparent lists by `node->sortKey`
   (`= container->+0x3c`, overridden per layer) with `+0x4c` and then the shader pointer as
   tie-breakers — that is what `0x45ad10` does and it is why blended geometry does not flicker.
6. **Free orphan nodes at the end of the frame** (`0x45b0c0`): any node whose `parent == nil` goes
   back to the free list; nodes owned by a `DisplayListPrimitive` persist across frames and are
   only removed when the prim is hidden or its matrix changes. This is the whole point of the
   `RemoveFromList()` calls in `Renderable::Display`.
7. **Instances (renderer/instance.cpp)** — draw `LODShape` *and* `InstanceShape` together with the
   bands from §5.4 rather than only the `InstanceShape`; that alone will make the trees grow trunks.
   Ignore `LODShape2`. Cull per matrix packet in the *render* pass, and remember `+0x60` means
   *culled*.
8. **Scene split.** Renderables live in `Scene`s with a `typeMask` filter, the render order is
   scenes `0, 2, 3, 1`, and instances always go into scene 0. If you ever need HUD/frontend
   layering this is where it comes from.

---

## 7. Address table (proposed IDA names)

| addr | name |
|---|---|
| 0x00465590 | `renderer::RenderFlowClient::OnFrame` (vtable 0x73782c slot 2) |
| 0x00467810 | `renderer::RenderManager::Update` |
| 0x004689a0 | `renderer::RenderManager::Render` |
| 0x00458620 | `renderer::Canvas::RenderScene` |
| 0x00468aa0 | `renderer::GamePlayScene::Render` (vslot 3) |
| 0x00468c50 | `renderer::GamePlayScene::Update` (vslot 4) |
| 0x00468b80 | `renderer::Scene::Render` (vslot 3) |
| 0x00468c00 | `renderer::Scene::Update` (vslot 4) |
| 0x00468ac0 / 0x00468cc0 | `Scene/GamePlayScene::AddRenderable` (vslot 1) |
| 0x004740f0 | `renderer::Renderable::Display` |
| 0x004740c0 | `renderer::Renderable::Tick` |
| 0x00473d90 | `renderer::Renderable::UpdateFade` |
| 0x00473aa0 | `renderer::Renderable::SetFadeTime` (was "SetFadeDist") |
| 0x00559000 | `renderer::Renderable::GetDistanceRefPos` (base, returns false) |
| 0x004714e0 | `renderer::WorldGeoRenderable::GetDistanceRefPos` |
| 0x00471640 | `renderer::WorldGeoRenderable::Display` |
| 0x00458f00 | `renderer::DisplayListPrimitive::SetVisible` |
| 0x00458ea0 | `renderer::DisplayListPrimitive::RemoveFromList` |
| 0x00458a20 | `renderer::Display_List::RemovePrimitiveNodes` |
| 0x0045d360 | `renderer::Display_List::AddContainer` (vslot 7) |
| 0x0045d3b0 | `renderer::Display_List::AddContainerElement` (vslot 8) |
| 0x0045e680 | `renderer::Display_List::Render` (vslot 9) |
| 0x00459910 | `renderer::Display_List::RenderList(int, bool)` |
| 0x0045b0c0 | `renderer::Display_List::FreeOrphanNodes` |
| 0x0045ad10 | `renderer::Display_List::SortAndRenderWater` (called from GamePlayScene::Update) |
| 0x00458ff0 / 0x00459090 / 0x00459060 / 0x004589e0 | the four node comparators |
| 0x00461ad0 | `renderer::GetCurrentCamera` (`g[0x8111dc]`) |
| 0x00464d00 | `renderer::SetCurrentCamera` |
| 0x0069e580 | `pure3d::Camera::SphereVisible` (vslot 22) |
| 0x0069e400 | `pure3d::Camera::TestSphereCameraSpace` (vslot 19) |
| 0x0069e690 | `pure3d::Camera::GetPosition` (vslot 24) |
| 0x00460980 | `occlude::TestSphere` (returns 2 == occluded) |
| 0x00460900 | `occlude::Occluder::TestSphere` |
| 0x0046edc0 | `renderer::InstancePrimitive::ComputeLODBands` |
| 0x0046f030 | `renderer::InstancePrimitive::PreDisplay` |
| 0x0046f0f0 | `renderer::InstancePrimitive::DrawInstanced` |
| 0x0045a4c0 / 0x0045a680 / 0x0045a620 | `Display_List::RenderInstanceList72/73/74` |
| 0x0046a520 | `<interiors>::IsCameraIndoors` |
