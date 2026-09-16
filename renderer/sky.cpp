#include "sky.h"
#include "render_manager.h"
#include "../billboard.h"
#include "../shader.h"
#include "../geometry.h"
#include "../pddi.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

namespace renderer
{

using namespace pure3d;

// retail: g[0x007c0c60]
bool g_skyEnabled = true;
// retail: g[0x008114ec] / 86400000
float g_timeOfDay = 0.25f;

static struct TimeOfDayInit {
	TimeOfDayInit(void) {
		if(const char *e = getenv("P3D_TIMEOFDAY"))
			g_timeOfDay = (float)atof(e);
	}
} timeOfDayInit;

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
// We have no frame controllers (the 0x00121204/0x00121201 BillboardQuadGroupAnimation-
// Controller chunks are skipped by both the composite and the billboard loader), and no
// time-of-day manager, so only the throttle is here; see re/notes/sky.md.
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
	// retail: fc->SetFrame((float)(int)fc->GetNumFrames() * phase) for every frame
	// controller of the composite. The only one we can drive is the vertex colour
	// animation of the sky box meshes, which is the one that matters: it is what turns
	// the sky from night to day. The billboard groups' own
	// BillboardQuadGroupAnimationController (chunk 0x00121204/0x00121201) is not
	// loaded, so the sun and the stars keep their rest colour.
	i32 n = composite->GetPrimitiveList()->GetNumPrimitives();
	for(i32 i = 0; i < n; i++) {
		DrawableContainer *draw = composite->GetPrimitiveList()->GetPrimitive(i)->GetDrawable();
		Geometry *geo = dynamic_cast<Geometry*>(draw);
		if(geo == nil)
			continue;
		i32 frames = geo->GetNumColourAnimFrames();
		if(frames > 0)
			geo->SetColourAnimFrame((float)frames * g_timeOfDay);
	}
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
