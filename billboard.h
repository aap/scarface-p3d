#pragma once

#include "entity.h"
#include "drawable.h"
#include "loadmanager.h"
#include "array.h"
#include "pddi.h"
#include "anim.h"

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

	// the cut-off cone mode four-CCs of 0x0001700a / 0x0001700b, mapped at 0x00695a50
	// (and inline at 0x006990d6 for the group's own pair). It is a bit MASK: bit 0 is
	// the cone around the (z,y) plane, bit 1 the one around the (z,x) plane.   [V]
	enum {
		CUTOFF_NONE	= 0,	// anything that is not one of the three below
		CUTOFF_VERT	= 1,	// "VERT"
		CUTOFF_HRZT	= 2,	// "HRZT"
		CUTOFF_BOTH	= 3	// "BOTH"
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

	// --- BillboardCutOffQuad (0x108 bytes instead of 0xc4) ---
	bool isCutOff;
	u32 sourceMode;		// +0xc4, 0x0001700a: the cone in the QUAD's own frame
	float sourceRange[4];	// +0xd0..+0xdc, the COSINES of (vertIn, vertOut, horzIn, horzOut)
	u32 edgeMode;		// +0xc8, 0x0001700b: the cone in the CAMERA's frame
	float edgeRange[4];	// +0xe0..+0xec, the same four cosines
	u32 falloffType;	// +0xcc, 0x0001700c: "LINE" -> 1
	float falloff[2];	// +0x100, +0x104
	float cutOffScale[4];	// +0xf0..+0xfc, 0x0001700d: two (hi, lo) size-scale pairs
	float intensity;	// the cut-off fade, 1 without a cut-off cone

	BillboardQuad(void);

	// retail: pure3d::BillboardCutOffQuad vslot 8, 0x00696f80 (the plain quad's slot is
	// a nullsub). Sets `intensity` from the two cones; see billboard.cpp.
	void Calculate(const Matrix &objectToWorld, const Matrix &cameraToWorld);
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

	// SHR: tBillboardQuadGroup::FindQuadByUID --- how a BQG animation group finds the
	// quad it animates
	BillboardQuad *FindQuad(const char *name);

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
	// the 0x00121204 / 0x00121201 frame controllers of the group. Retail hangs them off
	// the container's element (`e.frameControllers`); renderer::SkyLoader clears their
	// "advance yourself" flag and SkyRenderable::Update drives them from the clock.
	std::vector<FrameController*> frameControllers;

	BillboardObject(void);
	~BillboardObject(void);
	virtual void Display(DisplayList *list, GameDrawableInfo *info);
};

// retail: pure3d::BillboardQuadGroupAnimationController (the RTTI DynamicCaster vtable is
// 0x0076ac60); SHR: tBillboardQuadGroupAnimationController, p3d/anim/billboardobject-
// animation.cpp. One 'BQG' animation group per quad, matched by name; the channels are
// VIS / TRAN / ROT / WDT / HGT / DIST / CLR / OFF / ORNG / SRNG / ERNG (anim.h).
class BillboardQuadGroupAnimationController : public FrameController
{
	BillboardQuadGroup *group;
public:
	CLASSNAME(BillboardQuadGroupAnimationController);
	BillboardQuadGroupAnimationController(void) : group(nil) {}
	~BillboardQuadGroupAnimationController(void);

	void SetQuadGroup(BillboardQuadGroup *g);
	BillboardQuadGroup *GetQuadGroup(void) { return group; }

	// SHR: tBillboardQuadGroupAnimationController::Update
	virtual void SetFrame(float frame);
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
		BILLBOARD_CUTOFF_FALLOFF = 0x0001700C,
		BILLBOARD_CUTOFF_RANGE	= 0x0001700D,
	};
	CLASSNAME(BillboardObjectLoader)

	BillboardObjectLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory);
	BillboardQuad *LoadQuad(ChunkFile *f, LoadInventory *inventory);
};

}
