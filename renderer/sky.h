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

// retail: g[0x008114ec], the time of day in milliseconds; SkyRenderable::Update turns it
// into a 0..1 phase with g[0x737b00] == 1/86400000 and drives every frame controller of
// the sky composite with it. We have no time-of-day manager, so this is a plain 0..1
// value that P3D_TIMEOFDAY can set; the default is a clear daytime sky, because the
// meshes' own vertex colours (phase 0) are the midnight set.
extern float g_timeOfDay;

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
