#pragma once

#include "renderable.h"
#include "../loadmanager.h"
#include "../compositedrawable.h"

namespace renderer
{

using namespace content;

// retail: g[0x007c0c60], a .data bool that starts out true. Both SkyRenderable::Display
// and ::Update bail out on it, i.e. it is the global "draw the sky at all" switch.
extern bool g_skyEnabled;
// not retail: paint the sky box's below-horizon hemisphere (skybox16Shape, one dark blue
// in the file) with the current fog colour. In the game the fogged ocean covers that part
// of the sky box out to the far plane, so the horizon is the fog colour; until the viewer
// draws the ocean this stand-in gives the same result. P3D_NOSKYFOG=1 turns it off.
extern bool g_skyFogHorizon;
// not retail: the lens flare rigs (fxSys_SunFlare*, the two flare stars) need the
// occlusion query the sun's billboards use in retail, which the viewer lacks, so they
// shine through walls and the ground. Off by default; View tab > Sky, or P3D_FLARES=1.
extern bool g_skyLensFlares;

// retail: g[0x008114ec], the time of day in milliseconds; SkyRenderable::Update turns it
// into a 0..1 phase with g[0x737b00] == 1/86400000 and drives every frame controller of
// the sky composite with it. There is one clock for the whole renderer and the lights
// already own it, so this reads renderer::LightManager::timeOfDay (hours, P3D_TIME;
// P3D_TIMEOFDAY sets the same clock as a fraction of a day) and returns it as 0..1.
float GetTimeOfDay(void);

// retail: renderer::SkyRenderable : Renderable, vtable 0x007385e4, 0x98 bytes,
// typeMask 1 (TYPE_SKY). Chunk 0x08800002 (re/notes/sky.md). Two of them live in
// Common.p3d, "sky" and "rainy_skybox"; the game cross-fades between them with the
// renderable-wide fade, which is why Renderable::Display has a TYPE_SKY branch that
// pushes the fade into the drawable even though the sky is never distance tested.
class SkyRenderable : public Renderable
{
public:
	CLASSNAME(SkyRenderable)

	CompositeDrawable *composite;	// +0x84
	bool isRainy;			// +0x88, the composite name starts with "rainy_"
	i32 tickCount;			// +0x8c
	i32 timer;			// +0x90, counts the first 1000 ms down
	i32 lastTime;			// +0x94

	SkyRenderable(void);
	~SkyRenderable(void);

	virtual void Update(TimeInfo *t);		// retail: 0x477500
	virtual void Display(void);			// retail: 0x477450
	virtual void SetMatrix(const Matrix &m) {}	// retail: vslot 12 = 0x438400, a stub
};

// retail: renderer::SkyLoader, ctor 0x00477660, vtable 0x00738628,
// LoadObject 0x00477680
class SkyLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(SkyLoader)

	SkyLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
