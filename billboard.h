#pragma once

#include "entity.h"
#include "drawable.h"
#include "loadmanager.h"
#include "array.h"
#include "pddi.h"

namespace pure3d
{

using namespace core;
using namespace content;
using namespace math;

class Shader;

// retail: pure3d::BillboardQuad, vtable 0x0076aed8, size 0xc4 (0x108 for the
// BillboardCutOffQuad subclass). One camera-facing quad of a group; chunk 0x00017005
// (re/notes/sky.md). It is NOT a DrawablePrimitive: the group owns the quads and draws
// all of them into one primitive stream.
class BillboardQuad : public Entity
{
public:
	// the 4cc in the chunk, mapped by the loader (retail 0x6977ff)
	enum {
		MODE_NO_AXIS	= 0,	// "NOAX"  a flat quad, only the transform orients it
		MODE_ALL_AXIS	= 1,	// "AAX"   fully camera facing (everything in the sky)
		MODE_X_AXIS	= 2,	// "XAX"
		MODE_Y_AXIS	= 3,	// "YAX"
		MODE_LOCAL_X_AXIS = 4,	// "LXAX"
		MODE_LOCAL_Y_AXIS = 5,	// "LYAX"
	};

	CLASSNAME(BillboardQuad)

	pddiColour colour;	// +0x38
	Matrix transform;	// +0x3c, from the 0x00017007 sub-chunk (quaternion + position)
	float width;		// +0x7c, HALF the width the file stores (retail *0.5)
	float height;		// +0x80
	float distance;		// +0x84, extrusion towards the camera
	bool visible;		// +0x88
	u32 billboardMode;	// +0x8c
	u32 flipMode;		// +0x90, the uv flip flags of 0x00017009
	Vector2 uv[4];		// +0x94, bottom left, bottom right, top right, top left
	Vector2 uvOffset;	// +0xb4
	u32 cutOffMode;		// +0xc4 (BillboardCutOffQuad only), 0x00017008/a/b
	float intensity;	// the cut-off fade, always 1 without a cut-off mode

	BillboardQuad(void);
};

// retail: pure3d::BillboardQuadGroup : pure3d::DrawablePrimitive, vtable 0x0076afdc,
// ctor 0x00698830, size 0x94 (0xc8 for BillboardCutOffQuadGroup), Display 0x00698940.
// Chunk 0x00017006. The whole group is one shader and one primitive stream.
class BillboardQuadGroup : public DrawablePrimitive
{
	Shader *shader;
	pddiPrimBuffer *primBuffer;
public:
	CLASSNAME(BillboardQuadGroup)

	Matrix transform;	// +0x38, the group's own 0x00017007 transform
	float intensityBias;	// +0x78, pushed in by BillboardObject::Display
	bool zTest;		// +0x80
	bool zWrite;		// +0x81
	bool occlusion;		// +0x82, the PS2/Xbox occlusion query flag
	i32 occlusionIndex;	// +0x90, handed out by renderer::SkyLoader
	Array<BillboardQuad*> quads;	// +0x84

	BillboardQuadGroup(void);
	~BillboardQuadGroup(void);

	virtual u32 GetSomeMask(void) { return 1; }
	virtual void Display(void);
	virtual Shader *GetShader(void) const { return shader; }
	virtual void SetShader(Shader *shader);
	virtual bool IsLit(void) { return false; }	// retail: 0x652590, "return false"
	virtual bool IsALUM(void);
	virtual void UpdateBounds(void);
	virtual void SetFade(float fade) {}
};

// retail: pure3d::BillboardObject : pure3d::DrawableContainer, vtable 0x0076af04,
// size 0x5c, Display 0x00697660. The container the chunk loader actually registers in
// the inventory: one element, the BillboardQuadGroup. Its Display pushes the group's
// transform so that the display list node bakes it into its world matrix.
class BillboardObject : public DrawableContainer
{
public:
	CLASSNAME(BillboardObject)

	float intensityBias;	// +0x58, 1.0

	BillboardObject(void);
	virtual void Display(DisplayList *list, GameDrawableInfo *info);
};

// retail: pure3d::BillboardObjectLoader, vtable 0x0076afbc,
// LoadObject 0x006993d0 -> billboard_loader 0x00698ce0
class BillboardObjectLoader : public SimpleChunkHandler
{
public:
	enum {
		BILLBOARD_QUAD		= 0x00017005,
		BILLBOARD_QUAD_GROUP	= 0x00017006,
		BILLBOARD_TRANSFORM	= 0x00017007,
		BILLBOARD_DISPLAY_INFO	= 0x00017008,
		BILLBOARD_UV_INFO	= 0x00017009,
		BILLBOARD_CUTOFF_SOURCE	= 0x0001700A,
		BILLBOARD_CUTOFF_EDGE	= 0x0001700B,
		BILLBOARD_CUTOFF_RANGE	= 0x0001700D,
	};
	CLASSNAME(BillboardObjectLoader)

	BillboardObjectLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory);
	BillboardQuad *LoadQuad(ChunkFile *f, LoadInventory *inventory);
};

}
