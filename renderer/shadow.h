#pragma once

#include "renderable.h"
#include "../loadmanager.h"
#include "../compositedrawable.h"

namespace renderer
{

using namespace content;

// retail: g[0x007c0b46], a bool that reads 1 in the unpacked image: the global
// "draw the building shadows" switch. ShadowRenderable::Display draws through the base
// Renderable::Display when it is set and hides the shadow when it is not, and
// ShadowRenderable::Update only does anything when it is clear (re/notes/shadows.md §3).
extern bool g_buildingShadowsEnabled;

// retail: renderer::ShadowRenderable : Renderable, vtable 0x007383d4, 0xbc bytes,
// typeMask 0x100 (TYPE_SHADOW). Chunk 0x08800008 (re/notes/renderables.md §6,
// re/notes/shadows.md §3). 44 of them in z04: one per shell that casts building
// shadows, plus NPC_shadow and the vehicle ones.
//
// A building shadow is not geometry of its own: it is a CompositeDrawable of
// 0x0001001a pure3d::ShadowMesh drawables (a closed shell plus its 0x0001001b edge
// topology) whose silhouette is extruded away from the sun every frame and drawn as a
// stencil shadow volume. The loader forces every one of those primitives to
// Display_List layer 2, which is the only layer that reaches the volume lists 61..64;
// GetSomeMask() == 0x20 (a shadow primitive) routes it to list 63.
class ShadowRenderable : public Renderable
{
public:
	CLASSNAME(ShadowRenderable)

	bool isBuildingShadow;		// +0x84
	bool flagB;			// +0x86, always 0 in z04
	float f0;			// +0x88, always 0.0
	float drawDist;			// +0x8c, 200 (world) / 15 (NPC) / 100 (car).
					//   Read by nothing in the loader; the blob path
					//   and the volume pass do their own culling.
	Pose *pose;			// +0x90, the composite's pose
	CompositeDrawable *composite;	// +0x94

	ShadowRenderable(void);
	~ShadowRenderable(void);

	virtual void Update(TimeInfo *t);	// retail: vslot 9,  0x474cd0
	virtual void Display(void);		// retail: vslot 10, 0x4751c0
};

// retail: renderer::ShadowLoader, ctor 0x004758e0, vtable 0x0073841c,
// LoadObject 0x00475930
class ShadowLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(ShadowLoader)

	ShadowLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
