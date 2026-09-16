#include "sky.h"
#include "render_manager.h"
#include "lighting.h"
#include "../billboard.h"
#include "../shader.h"
#include "../geometry.h"
#include "../pddi.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

namespace renderer
{

using namespace pure3d;

// retail: g[0x007c0c60]
bool g_skyEnabled = true;

// retail: g[0x008114ec] is the time of day in milliseconds and SkyRenderable::Update
// turns it into a 0..1 phase with g[0x737b00] == 1/86400000. There is exactly one clock
// in the game and the lights run off it too, so the viewer keeps it in one place:
// renderer::LightManager::timeOfDay, in hours (P3D_TIME; P3D_TIMEOFDAY is the same clock
// as a 0..1 fraction). gLightManager exists from RenderManager::Init onwards.
float GetTimeOfDay(void)
{
	if(gLightManager == nil)
		return 0.5f;
	float t = gLightManager->timeOfDay/24.0f;
	return t - floorf(t);
}

SkyRenderable::SkyRenderable(void)
 : Renderable(0),
   composite(nil),
   isRainy(false),
   tickCount(0),
   timer(1000),
   lastTime(0)
{
	typeMask = TYPE_SKY;
}

SkyRenderable::~SkyRenderable(void)
{
	Release(composite);
}

// retail: renderer::SkyRenderable::Display 0x00477450 (vslot 10) --- two instructions:
//	if(g_skyEnabled) Renderable::Display(); else Hide();
// The base Display does the rest: the sky has doDistanceTest cleared, so it always
// passes, and the TYPE_SKY branch pushes the renderable-wide fade into the composite
// (renderable.cpp), which is the cross-fade between the clear and the rainy sky box.
void
SkyRenderable::Display(void)
{
	if(g_skyEnabled)
		Renderable::Display();
	else
		Hide();
}

// retail: renderer::SkyRenderable::Update 0x00477500 (vslot 9). The whole function is
// a throttle around one job: push the time of day into the sky's frame controllers.
//
//	if(!g_skyEnabled) return;
//	t = wallClock;                                 // g[0x80ac04]->+0x34, ms
//	if(t - lastTime > 10) timer = 1000;            // restart after a stall
//	lastTime = t;
//	if(paused or in a cut scene) { timer = 1000; tickCount = 0; }
//	else if(timer > dt)          { timer -= dt; tickCount = 0; }
//	else                         { tickCount++; timer = 0; }
//	if(tickCount % 15) return;                     // every frame for the first
//	                                               // second, then every 15th
//	float phase = (u32)g_timeOfDayMs * (1.0f/86400000.0f);
//	for(fc : composite->frameControllers) {        // composite +0x48
//		fc->SetFrame((float)(int)fc->GetNumFrames() * phase);
//		fc->[0xd] = 1; fc->Update(0.0f, true); fc->[0xd] = 0;
//	}
//	Hide();                                        // force the nodes to be re-submitted
//
// The frame controllers of the sky composite are (re/notes/sky.md §4):
//   PTRN_sky          the pose animation: it turns the "sun_grp" joint, and with it the
//                     sun, the two flare rigs, the two lens flares and the moon
//   BQG_*             one per billboard quad group: colour, size and visibility
//   VRTX_*            one per sky box mesh: which vertex colour offset set is current
void
SkyRenderable::Update(TimeInfo *t)
{
	if(!g_skyEnabled)
		return;
	i32 dt = (i32)(t->dt*1000.0f);
	if(timer > dt) {
		timer -= dt;
		tickCount = 0;
	} else {
		tickCount++;
		timer = 0;
	}
	if(tickCount % 15)
		return;
	if(composite == nil)
		return;

	float phase = GetTimeOfDay();
	std::vector<FrameController*> &fcs = composite->GetFrameControllers();
	for(u32 i = 0; i < fcs.size(); i++)
		fcs[i]->SetFrame(fcs[i]->GetNumFrames() * phase);

	// The pose animation moves the billboard groups, and a display list node caches the
	// world matrix it was submitted with, so the nodes have to go: retail's Update ends
	// with exactly this call (renderer/README.md, "The frame").
	Hide();
}


SkyLoader::SkyLoader(void)
 : SimpleChunkHandler(Renderable::SKY_LOADER)
{
}

// retail: renderer::SkyLoader::LoadObject 0x00477680
void
SkyLoader::LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];
	char compName[256];

	f->GetString(name);

	SkyRenderable *sky = new SkyRenderable;
	sky->SetName(name);

	f->GetString(compName);
	// retail: strstr(compName, g[0x7c0c64] == "rainy_") == compName
	sky->isRainy = strstr(compName, "rainy_") == compName;
	if(sky->isRainy) {
		// the rainy box starts fully faded out and is faded in over 40 ms by the
		// weather code (retail: flags80 |= 2; SetToFadeOut(40.0f) at 0x4777de)
		sky->doFade = true;
		sky->SetToFadeOut(40.0f);
	} else
		sky->doFade = false;

	CompositeDrawable *composite = inventory->Find<CompositeDrawable>(compName);
	if(composite == nil) {
		fprintf(stderr, "warning : composite \"%s\" not found while loading sky from file %s\n",
			compName, f->GetName());
		delete sky;
		return;
	}
	Assign(sky->composite, composite);

	// Layers. A billboard quad group (sun, sun flare, stars, moon) goes to layer 28,
	// i.e. display list 76, the camera-locked list that is drawn last with fog off.
	// Everything else --- the sky box meshes themselves --- goes to layer 39 (list 46)
	// for the clear sky and layer 38 (list 47, the one RenderSky applies the container
	// fade to) for the rainy one.
	i32 occlusionIndex = 0;
	i32 n = composite->GetPrimitiveList()->GetNumPrimitives();
	for(i32 i = 0; i < n; i++) {
		DrawableContainer *draw = composite->GetPrimitiveList()->GetPrimitive(i)->GetDrawable();
		if(draw == nil)
			continue;
		for(i32 j = 0; j < draw->GetNumElements(); j++) {
			DrawablePrimitive *prim = draw->GetElement(j)->prim;
			if(prim == nil)
				continue;
			BillboardQuadGroup *group = dynamic_cast<BillboardQuadGroup*>(prim);
			if(group) {
				if(group->occlusion)
					group->occlusionIndex = occlusionIndex++;
				prim->SetLayer(28);
			} else
				prim->SetLayer(sky->isRainy ? 38 : 39);
			// retail also clears the "advance yourself" flag of every frame
			// controller of the primitive here ([fc+0x0d] = 0): the sky's
			// animation is driven by the time of day, not by the clock
		}
	}

	sky->SetNumElements(1);
	sky->SetElement(composite, 0, false);
	// retail: flags80 &= ~1 --- the sky is never distance tested or frustum culled
	sky->doDistanceTest = false;
	// retail: Matrix::Identity + Matrix::FillScale(2.0f) at 0x477963. The sky box is
	// modelled at half size; RenderSky additionally moves it to the camera's x/z.
	sky->matrix.Identity();
	sky->matrix.e[0] = sky->matrix.e[5] = sky->matrix.e[10] = 2.0f;

	if(getenv("P3D_VERBOSE"))
		printf("sky %s: composite %s, %d primitives, %s\n", name, compName, n,
			sky->isRainy ? "rainy" : "clear");

	*pObject = sky;
	*pUID = sky->GetUID();
}

}
