#pragma once

#include "renderable.h"
#include "display_list.h"

namespace renderer
{

// retail: renderer::Scene, vtable 0x00737a80, ctor 0x468ed0(index, capacity), 0x1c bytes.
// NOT a scene graph: a fixed-size array of Renderable* slots plus a typeMask filter and
// an enable flag. Adding a renderable is "find the first nil slot" and a full scene
// silently drops it. There are exactly four, and they are updated and rendered in index
// order 0, 1, 2, 3.
class Scene
{
public:
	Array<Renderable*> slots;	// +0x04/+0x08
	bool enabled;			// +0x10
	i32 index;			// +0x14, copied into Renderable::sceneId
	u32 typeMaskFilter;		// +0x18

	CLASSNAME(Scene);
	Scene(i32 index, i32 capacity);
	virtual ~Scene(void);
	virtual void AddRenderable(Renderable *r);	// vslot 1, retail: 0x468ac0
	virtual void RemoveRenderable(Renderable *r);	// vslot 2, retail: 0x468b20
	virtual void Render(void);			// vslot 3, retail: 0x468b80
	virtual void Update(TimeInfo *t);		// vslot 4, retail: 0x468c00
	void EnableTypes(bool on, u32 mask);		// retail: 0x468a70
};

// retail: renderer::GamePlayScene : Scene, vtable 0x00737a9c, ctor 0x468f90, 0x24 bytes.
// The one scene that owns a Display_List and therefore does deferred, sorted, batched
// drawing; the other three draw immediately through Renderable::RenderImmediate.
class GamePlayScene : public Scene
{
public:
	Display_List *displayList;	// +0x20, also published as g_displayList

	CLASSNAME(GamePlayScene);
	GamePlayScene(i32 index, i32 capacity, i32 displayListNodes);
	virtual ~GamePlayScene(void);
	virtual void AddRenderable(Renderable *r);	// retail: 0x468cc0
	virtual void Render(void);			// retail: 0x468aa0
	virtual void Update(TimeInfo *t);		// retail: 0x468c50
};

// retail: renderer::Canvas, vtable 0x007376e8, ctor 0x458210, 0x38 bytes.
// NOT a render target: a pure3d::View plus animated fog plus an enable flag.
// Canvas::RenderScene is "tick the fog fade, then tell the scene to render".
class Canvas
{
public:
	// retail +0x04 is the pure3d::View and +0x08 its camera; we have neither, the
	// viewer owns the projection and the view matrix.
	bool enabled;			// +0x0c
	u32 backgroundColour;		// +0x10
	float fogFadeTimeLeft;		// +0x14
	u32 fogColour;			// +0x18
	float fogStart;			// +0x1c
	float fogEnd;			// +0x20
	i32 fogDensity;			// +0x24
	u32 fogColourTo;		// +0x28
	float fogStartTo;		// +0x2c
	float fogEndTo;			// +0x30
	i32 fogDensityTo;		// +0x34

	Canvas(void);
	void SetBackgroundColour(u32 colour) { backgroundColour = colour; }	// retail: 0x4582e0
	// retail: renderer::Canvas::SetFog 0x458300 --- time > 0 animates, else it snaps
	void SetFog(bool on, u32 colour, float start, float end, i32 density, float time);
	// retail: renderer::Canvas::UpdateFog 0x458390
	void UpdateFog(TimeInfo *t);
	// retail: renderer::Canvas::RenderScene 0x458620
	void RenderScene(Scene *scene, TimeInfo *t);
};

// retail: renderer::RenderManager, vtable 0x007379b8, ctor 0x467a00, 0x104 bytes.
// NOT a renderer: the owner. 4 scenes, 2 canvases, 12 memory heaps, the environment
// manager and the handful of singleton renderables.
class RenderManager
{
public:
	enum { NUM_SCENES = 4 };
	// the leak's scene names; the creators always pass GAMEPLAY_SCENE
	enum { GAMEPLAY_SCENE = 0, GUI_SCENE = 1 };
	// retail: the 12 heaps of RenderManager::Init (0x468490); GetHeap(id) is
	// heaps[id].heap and every facade entry point scopes the allocator on one of them
	enum {
		HEAP_VEHICLE = 0, HEAP_SMALL_STATEPROP, HEAP_MEDIUM_STATEPROP, HEAP_UNUSED3,
		HEAP_INSTANCE, HEAP_PARTICLE_EFFECT, HEAP_BUILDING_SHADOW, HEAP_SHADOW,
		HEAP_DECAL, HEAP_WAKE, HEAP_RENDERABLE_HANDLE, HEAP_EMPTY, NUM_HEAPS
	};

	Scene *scenes[NUM_SCENES];	// +0x04
	Canvas *canvas;			// +0x10
	Canvas *canvas2;		// +0x14
	bool initialised;		// +0x18
	bool skyEnabled;		// +0x19
	Renderable *sky;		// +0x28
	Renderable *sky2;		// +0x2c
	Renderable *mainCharacter;	// +0x3c

	RenderManager(void);
	~RenderManager(void);
	// retail: renderer::RenderManager::Init 0x468490
	void Init(i32 maxRenderables, i32 displayListNodes);
	// retail: renderer::RenderManager::Update 0x467810 --- scenes 0..3 in index order
	void Update(TimeInfo *t);
	// retail: renderer::RenderManager::Render 0x4689a0
	void Render(TimeInfo *t);
	// retail: renderer::RenderManager::GetHeap 0x4675b0 --- heaps[id].heap. We have no
	// heaps, so this only exists to keep the call sites honest.
	void *GetHeap(i32 id) { return nil; }
	void SetSky(Renderable *r, bool secondary);	// retail: 0x467950
};

// retail: g_renderMgr g[0x8111d0]
extern RenderManager *g_renderMgr;

// retail: renderer::RenderableHandle, vtable 0x0073781c, 0x0c bytes, allocated from the
// RenderableHandleBlock pool (550 of them, hard cap). A weak reference with a generation
// check: the game never holds a Renderable*, it holds one of these, and every facade
// call starts by revalidating it.
class RenderableHandle
{
public:
	Renderable *renderable;		// +0x04
	u32 uid;			// +0x08, snapshot of renderable->uniqueId

	RenderableHandle(Renderable *r);	// retail: 0x461870
	virtual ~RenderableHandle(void);
	void Detach(void);			// retail: 0x4618d0
};

// the validity check repeated verbatim in ~90 facade functions
Renderable *Deref(RenderableHandle *h, i32 wantMask = 0);
// retail: renderer::Renderable_Destroy 0x462340 --- appends to the deferred queue
void Renderable_Destroy(RenderableHandle **h);
// retail: renderer::DestroyPendingRenderables 0x464ac0 --- drains it, between
// RenderManager::Update and RenderManager::Render
void DestroyPendingRenderables(void);

}
