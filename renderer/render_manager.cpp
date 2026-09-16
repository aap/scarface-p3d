// The spine objects: RenderManager owns the scenes and canvases, a Scene is an array of
// Renderable slots, a Canvas is a view plus animated fog, and a RenderableHandle is the
// weak reference the game holds instead of a Renderable*. See re/notes/rendercore.md.
//
// The frame (retail: RenderFlowClient::OnFrame 0x465590):
//     RenderManager::Update(t)            scenes 0..3
//         GamePlayScene::Update           Tick + filter + Renderable::Display
//         Display_List::SortAllLists
//     DestroyPendingRenderables()         between Update and Render, always
//     RenderManager::Render(t)            canvas->RenderScene(scenes[i])
//         GamePlayScene::Render           Display_List::Render

#include "render_manager.h"
#include "../pddi.h"

#include <vector>

namespace renderer
{

// retail: g_renderMgr g[0x8111d0]
RenderManager *g_renderMgr;


// ---------------------------------------------------------------- Scene

// retail: renderer::Scene::Scene 0x468ed0
Scene::Scene(i32 index, i32 capacity)
 : enabled(true), index(index), typeMaskFilter(0xffffffff)
{
	slots.Create(capacity);
	for(u32 i = 0; i < slots.Size(); i++)
		slots[i] = nil;
}

Scene::~Scene(void)
{
	for(u32 i = 0; i < slots.Size(); i++)
		if(slots[i]) slots[i]->Release();
}

// retail: renderer::Scene::AddRenderable 0x468ac0 --- first free slot, and it silently
// drops the renderable when the scene is full (there is no growth and no error path)
void
Scene::AddRenderable(Renderable *r)
{
	r->sceneId = index;
	for(u32 i = 0; i < slots.Size(); i++)
		if(slots[i] == nil) {
			r->AddRef();
			slots[i] = r;
			return;
		}
}

// retail: renderer::Scene::RemoveRenderable 0x468b20
void
Scene::RemoveRenderable(Renderable *r)
{
	for(u32 i = 0; i < slots.Size(); i++)
		if(slots[i] == r) {
			r->Release();
			slots[i] = nil;
			return;
		}
}

// retail: renderer::Scene::Render 0x468b80 --- immediate mode, the HUD/frontend layers
void
Scene::Render(void)
{
	if(!enabled)
		return;
	for(u32 i = 0; i < slots.Size(); i++) {
		Renderable *r = slots[i];
		if(r && (r->typeMask & typeMaskFilter) && r->isVisible)
			r->RenderImmediate();
	}
}

// retail: renderer::Scene::Update 0x468c00
void
Scene::Update(TimeInfo *t)
{
	if(!enabled)
		return;
	for(u32 i = 0; i < slots.Size(); i++) {
		Renderable *r = slots[i];
		if(r && r->isVisible && (r->typeMask & typeMaskFilter))
			r->Update(t);
	}
}

// retail: renderer::Scene::EnableTypes 0x468a70
void
Scene::EnableTypes(bool on, u32 mask)
{
	if(on)
		typeMaskFilter |= mask;
	else
		typeMaskFilter &= ~mask;
}


// ---------------------------------------------------------------- GamePlayScene

// retail: renderer::GamePlayScene::GamePlayScene 0x468f90
GamePlayScene::GamePlayScene(i32 index, i32 capacity, i32 displayListNodes)
 : Scene(index, capacity)
{
	displayList = new Display_List(displayListNodes);
	SetGlobalDisplayList(displayList);	// retail: 0x4589d0
}

GamePlayScene::~GamePlayScene(void)
{
	SetGlobalDisplayList(nil);
	delete displayList;
}

// retail: renderer::GamePlayScene::AddRenderable 0x468cc0
void
GamePlayScene::AddRenderable(Renderable *r)
{
	Scene::AddRenderable(r);
	// retail also hooks up building shadows (typeMask 0x100) and the sky (typeMask 1)
	if(r->typeMask == Renderable::TYPE_SKY && g_renderMgr)
		g_renderMgr->SetSky(r, false);
}

// retail: renderer::GamePlayScene::Render 0x468aa0
void
GamePlayScene::Render(void)
{
	if(enabled)
		displayList->Render();
}

// retail: renderer::GamePlayScene::Update 0x468c50 --- Tick runs on every renderable,
// even the hidden ones; only the visible ones that pass the type filter get the
// per-class Update and then Display.
void
GamePlayScene::Update(TimeInfo *t)
{
	if(!enabled)
		return;
	for(u32 i = 0; i < slots.Size(); i++) {
		Renderable *r = slots[i];
		if(r == nil)
			continue;
		r->Tick(t);
		if(!r->isVisible)
			continue;
		if(!(r->typeMask & typeMaskFilter))
			continue;
		context->PushDebugName(r->GetName());
		r->Update(t);
		r->Display();
		context->PopDebugName();
	}
	displayList->SortAllLists();
}


// ---------------------------------------------------------------- Canvas

// retail: renderer::Canvas::Canvas 0x458210
Canvas::Canvas(void)
 : enabled(true), backgroundColour(0xff191919), fogFadeTimeLeft(0.0f),
   fogColour(0xff808080), fogStart(100.0f), fogEnd(1000.0f), fogDensity(200),
   fogColourTo(0xff808080), fogStartTo(100.0f), fogEndTo(1000.0f), fogDensityTo(200)
{
}

// retail: renderer::Canvas::SetFog 0x458300
void
Canvas::SetFog(bool on, u32 colour, float start, float end, i32 density, float time)
{
	if(time > 0.0f && on) {
		fogColourTo = colour;
		fogStartTo = start;
		fogEndTo = end;
		fogDensityTo = density;
		fogFadeTimeLeft = time;
	} else {
		fogColour = colour;
		fogStart = start;
		fogEnd = end;
		fogDensity = density;
		fogFadeTimeLeft = 0.0f;
	}
	// retail then writes the current values into the pure3d::View (+0x30..+0x3c)
}

// retail: renderer::Canvas::UpdateFog 0x458390 --- lerp each ARGB byte and each float
// towards the target by dt/timeLeft, and snap when the time runs out
void
Canvas::UpdateFog(TimeInfo *t)
{
	if(fogFadeTimeLeft <= 0.0f)
		return;
	float k = t->dt/fogFadeTimeLeft;
	fogFadeTimeLeft -= t->dt;
	if(fogFadeTimeLeft <= 0.0f) {
		fogColour = fogColourTo;
		fogStart = fogStartTo;
		fogEnd = fogEndTo;
		fogDensity = fogDensityTo;
		fogFadeTimeLeft = 0.0f;
	} else {
		u32 c = 0;
		for(int i = 0; i < 4; i++) {
			int a = (fogColour >> i*8) & 0xff;
			int b = (fogColourTo >> i*8) & 0xff;
			c |= (u32)(a + (int)((b - a)*k)) << i*8;
		}
		fogColour = c;
		fogStart += (fogStartTo - fogStart)*k;
		fogEnd += (fogEndTo - fogEnd)*k;
		fogDensity += (i32)((fogDensityTo - fogDensity)*k);
	}
}

// retail: renderer::Canvas::RenderScene 0x458620
void
Canvas::RenderScene(Scene *scene, TimeInfo *t)
{
	if(!enabled)
		return;
	UpdateFog(t);
	scene->Render();
}


// ---------------------------------------------------------------- RenderManager

// retail: renderer::RenderManager::RenderManager 0x467a00
RenderManager::RenderManager(void)
 : canvas(nil), canvas2(nil), initialised(false), skyEnabled(false),
   sky(nil), sky2(nil), mainCharacter(nil)
{
	for(i32 i = 0; i < NUM_SCENES; i++)
		scenes[i] = nil;
	g_renderMgr = this;
}

RenderManager::~RenderManager(void)
{
	for(i32 i = 0; i < NUM_SCENES; i++)
		delete scenes[i];
	delete canvas;
	delete canvas2;
	if(g_renderMgr == this) g_renderMgr = nil;
}

// retail: renderer::RenderManager::Init 0x468490 --- builds the 12 heaps, the four
// scenes, the two canvases and the three global pools (decals, skid marks, tracers).
// Everything but the scenes and canvases is out of scope here.
void
RenderManager::Init(i32 maxRenderables, i32 displayListNodes)
{
	if(initialised)
		return;
	scenes[0] = new GamePlayScene(0, maxRenderables, displayListNodes);
	scenes[2] = new Scene(2, 64);
	scenes[3] = new Scene(3, 64);
	scenes[1] = new Scene(1, 64);
	canvas = new Canvas;
	canvas2 = new Canvas;
	initialised = true;
}

// retail: renderer::RenderManager::Update 0x467810 --- the compiler reordered the
// if/else chain, but the loop counter still selects scenes[i], so it really is 0,1,2,3
void
RenderManager::Update(TimeInfo *t)
{
	if(!canvas->enabled)
		return;
	for(i32 i = 0; i < NUM_SCENES; i++)
		if(scenes[i])
			scenes[i]->Update(t);
	// retail also updates the environment manager, the decal system and the light
	// manager here, and the extra indoor pass when the camera is inside
}

// retail: renderer::RenderManager::Render 0x4689a0
void
RenderManager::Render(TimeInfo *t)
{
	if(!canvas->enabled)
		return;
	for(i32 i = 0; i < NUM_SCENES; i++)
		if(scenes[i])
			canvas->RenderScene(scenes[i], t);
}

// retail: renderer::RenderManager::SetSky 0x467950
void
RenderManager::SetSky(Renderable *r, bool secondary)
{
	Renderable *&slot = secondary ? sky2 : sky;
	if(r) r->AddRef();
	if(slot) slot->Release();
	slot = r;
}


// ---------------------------------------------------------------- RenderableHandle

// retail: g_handlesCreated/Destroyed/Live g[0x8110a4/a8/ac]
static u32 handlesCreated, handlesDestroyed, handlesLive;

// retail: renderer::RenderableHandle::RenderableHandle 0x461870
RenderableHandle::RenderableHandle(Renderable *r)
{
	renderable = nil;
	uid = r->uniqueId;
	r->AddRef();
	renderable = r;
	r->hasHandle = true;
	handlesCreated++;
	handlesLive++;
}

// retail: renderer::RenderableHandle::Detach 0x4618d0 --- Hide() so the display list
// forgets the renderable, then drop the reference and invalidate the generation
void
RenderableHandle::Detach(void)
{
	if(renderable) {
		renderable->hasHandle = false;
		renderable->Hide();
		renderable->Release();
		renderable = nil;
		handlesLive--;
		handlesDestroyed++;
	}
	uid = 0xffffffff;
}

RenderableHandle::~RenderableHandle(void)
{
	Detach();
}

// The check repeated verbatim in ~90 facade functions: a stale handle whose renderable
// was destroyed and whose slot was reused fails the generation test, and every call on
// it is a silent no-op.
Renderable*
Deref(RenderableHandle *h, i32 wantMask)
{
	if(h == nil)
		return nil;
	Renderable *r = h->renderable;
	if(r == nil)
		return nil;
	if(r->uniqueId != h->uid)
		return nil;
	if(wantMask && r->typeMask != wantMask)
		return nil;
	return r;
}

// retail: the deferred-destroy queue g[0x8110b4/b8/bc]
static std::vector<RenderableHandle*> destroyQueue;

// retail: renderer::Renderable_Destroy 0x462340 --- destruction is deferred to the next
// frame; this only validates and queues
void
Renderable_Destroy(RenderableHandle **h)
{
	if(h == nil || Deref(*h) == nil)
		return;
	destroyQueue.push_back(*h);
	*h = nil;
}

// retail: renderer::DestroyPendingRenderables 0x464ac0 --- runs the per-typeMask
// teardown, removes the renderable from scenes[r->sceneId] (the only reason that field
// exists) and deletes the handle
void
DestroyPendingRenderables(void)
{
	for(u32 i = 0; i < destroyQueue.size(); i++) {
		RenderableHandle *h = destroyQueue[i];
		if(h == nil)
			continue;
		Renderable *r = h->renderable;
		if(r && g_renderMgr && r->sceneId >= 0 && r->sceneId < RenderManager::NUM_SCENES &&
		   g_renderMgr->scenes[r->sceneId])
			g_renderMgr->scenes[r->sceneId]->RemoveRenderable(r);
		delete h;
	}
	destroyQueue.clear();
}

}
