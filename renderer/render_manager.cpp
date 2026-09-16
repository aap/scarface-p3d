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
#include "lighting.h"
#include "sky.h"
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
	// retail also hooks up building shadows (typeMask 0x100). The sky goes into the
	// primary slot or --- for the "rainy_" one, SkyRenderable +0x88 --- the secondary
	// one, which is the pair the weather cross-fade works on.
	if(r->typeMask == Renderable::TYPE_SKY && g_renderMgr) {
		SkyRenderable *sky = dynamic_cast<SkyRenderable*>(r);
		g_renderMgr->SetSky(r, sky && sky->isRainy);
	}
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
 : enabled(true), backgroundColour(0xff191919), fogEnabled(false), fogFadeTimeLeft(0.0f),
   fogColour(0xff808080), fogStart(100.0f), fogEnd(1000.0f), fogClamp(200),
   fogColourTo(0xff808080), fogStartTo(100.0f), fogEndTo(1000.0f), fogClampTo(200)
{
}

// retail: renderer::Canvas::SetFog 0x458300
void
Canvas::SetFog(bool on, u32 colour, float start, float end, i32 clamp, float time)
{
	fogEnabled = on;
	if(time > 0.0f && on) {
		fogColourTo = colour;
		fogStartTo = start;
		fogEndTo = end;
		fogClampTo = clamp;
		fogFadeTimeLeft = time;
	} else {
		fogColour = colour;
		fogStart = start;
		fogEnd = end;
		fogClamp = clamp;
		fogFadeTimeLeft = 0.0f;
	}
	// retail then writes the current values into the pure3d::View (+0x30..+0x3c)
}

// Retail's fog lives in the pure3d::View, which hands it to pddi in View::BeginRender
// (0x67ddb0) --- three calls, in this order, and the last two only when fog is on:
//     ctx->EnableFog(on); ctx->SetFog(colour, start, end); ctx->SetFogClamp(clamp);
// We have no View, so the canvas talks to the context itself.
void
Canvas::ApplyFog(void)
{
	context->EnableFog(fogEnabled);
	if(fogEnabled) {
		context->SetFog(pddiColour(fogColour), fogStart, fogEnd);
		context->SetFogClamp(fogClamp);
	}
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
		fogClamp = fogClampTo;
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
		fogClamp += (i32)((fogClampTo - fogClamp)*k);
	}
}

// retail: renderer::Canvas::RenderScene 0x458620
void
Canvas::RenderScene(Scene *scene, TimeInfo *t)
{
	if(!enabled)
		return;
	UpdateFog(t);
	ApplyFog();
	scene->Render();
}


// ---------------------------------------------------------------- EnvManager

// retail: RenderManager::env, created by RenderManager::Init
EnvManager *gEnvManager;

// The game's own fog, per time of day. The six key frames are the TODObject "tod" of
// packages/z04/miami_lod.p3d (PrelitLuminanceTime_0..5 = 4, 9, 12, 18, 21, 24 h) and the
// numbers are the FogStart/FogEnd/FogColor_Red/Green/Blue/Alpha/FogClamp properties of
// the twelve environment_{clear,rainy}_{4,9,12,18,21,24} EnvironmentObjects it attaches,
// which live in scriptc/graphanims.cso of cement.rcf. FogEnabled is 1 in all twelve.
// re/notes/fog.md has the table and how it was read out of the compiled script.
const EnvManager::KeyFrame EnvManager::keyFrames[EnvManager::NUM_KEYFRAMES] = {
	//                 r    g    b    a  start   end  clamp
	{  4.0f, FogParameters( 23,  28,  40,  95,  20.0f, 800.0f, 10.0f),
	         FogParameters( 16,  16,  17,  95,  10.0f, 525.0f, 18.0f) },
	{  9.0f, FogParameters(245, 240, 160, 128,  25.0f, 800.0f, 70.0f),
	         FogParameters( 35,  35,  23, 128,  20.0f, 700.0f, 50.0f) },
	{ 12.0f, FogParameters(200, 200, 150, 110,  25.0f, 900.0f, 60.0f),
	         FogParameters( 35,  35,  28, 110,  20.0f, 700.0f, 45.0f) },
	{ 18.0f, FogParameters(227, 223, 105, 150,  25.0f, 800.0f, 65.0f),
	         FogParameters( 35,  35,  21, 150,  20.0f, 600.0f, 55.0f) },
	{ 21.0f, FogParameters( 50,  35,  33,  95,  20.0f, 800.0f, 45.0f),
	         FogParameters( 26,  21,  21,  95,  20.0f, 700.0f, 45.0f) },
	{ 24.0f, FogParameters( 20,  25,  32,  95,  20.0f, 800.0f, 15.0f),
	         FogParameters( 14,  14,  16,  95,  10.0f, 500.0f, 20.0f) },
};

static FogParameters
LerpFog(const FogParameters &a, const FogParameters &b, float t)
{
	FogParameters f;
	f.enabled = a.enabled;
	f.r = a.r + (i32)((b.r - a.r)*t);
	f.g = a.g + (i32)((b.g - a.g)*t);
	f.b = a.b + (i32)((b.b - a.b)*t);
	f.a = a.a + (i32)((b.a - a.a)*t);
	f.start = a.start + (b.start - a.start)*t;
	f.end = a.end + (b.end - a.end)*t;
	f.clamp = a.clamp + (b.clamp - a.clamp)*t;
	return f;
}

FogParameters
EnvManager::GetFog(float hour, bool raining) const
{
	// the keys are 4, 9, 12, 18, 21 and 24 h; before the first one we come round from
	// the last, which is the same midnight
	while(hour < 0.0f) hour += 24.0f;
	while(hour >= 24.0f) hour -= 24.0f;
	i32 i1 = 0;
	while(i1 < NUM_KEYFRAMES && keyFrames[i1].hour < hour)
		i1++;
	if(i1 >= NUM_KEYFRAMES)
		i1 = NUM_KEYFRAMES-1;
	i32 i0 = i1 == 0 ? NUM_KEYFRAMES-1 : i1-1;
	float h0 = i1 == 0 ? keyFrames[i0].hour - 24.0f : keyFrames[i0].hour;
	float t = keyFrames[i1].hour > h0 ? (hour - h0)/(keyFrames[i1].hour - h0) : 0.0f;
	if(t < 0.0f) t = 0.0f;
	if(t > 1.0f) t = 1.0f;
	return raining ? LerpFog(keyFrames[i0].rainy, keyFrames[i1].rainy, t) :
	                 LerpFog(keyFrames[i0].clear, keyFrames[i1].clear, t);
}

// retail: the fog half of EnvManager::Update 0x46c1d0 --- it builds a FogParameters out
// of the interpolated env parameters and calls View_SetFog, which is Canvas::SetFog on
// both canvases. We snap (time 0), like the env manager does.
void
EnvManager::Update(Canvas *canvas, float hour, bool raining)
{
	if(!enabled || canvas == nil)
		return;
	// retail: View_SetFog 0x464e40 --- pack the colour, truncate the clamp, time 0
	FogParameters f = GetFog(hour, raining);
	canvas->SetFog(f.enabled, f.Colour().c, f.start, f.end, (i32)f.clamp, 0.0f);
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
	// retail: the environment manager is RenderManager+0x1c, built here (ctor 0x46bd80)
	if(gEnvManager == nil)
		gEnvManager = new EnvManager;
	// retail: renderer::Init 0x465120 makes the light manager here too (g[0x8111cc])
	if(gLightManager == nil)
		gLightManager = new LightManager;
	initialised = true;
}

// retail: renderer::RenderManager::Update 0x467810 --- the compiler reordered the
// if/else chain, but the loop counter still selects scenes[i], so it really is 0,1,2,3
void
RenderManager::Update(TimeInfo *t)
{
	if(!canvas->enabled)
		return;
	// retail: LightManager::Update 0x460130, called from here (0x0046786a). It collects
	// the lights that can reach the camera and hands them to the context.
	if(gLightManager)
		gLightManager->Update(t);
	// retail: EnvManager::Update 0x46c1d0, called from here too. It blends the two env
	// parameter sets around the clock and pushes the fog out through View_SetFog, i.e.
	// Canvas::SetFog on both canvases. The viewer has one clock, LightManager's.
	if(gEnvManager && gLightManager)
		gEnvManager->Update(canvas, gLightManager->timeOfDay, gLightManager->raining);
	for(i32 i = 0; i < NUM_SCENES; i++)
		if(scenes[i])
			scenes[i]->Update(t);
	// retail also updates the environment manager and the decal system here, and does
	// the extra indoor pass when the camera is inside
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
