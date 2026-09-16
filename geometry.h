#pragma once

#include "entity.h"
#include "drawable.h"
#include "primgroup.h"
#include "loadmanager.h"
#include "anim.h"

#include <vector>

namespace pure3d
{

using namespace core;
using namespace content;


// The vertex colour half of a mesh's "VRTXANIM": numFrames per-vertex colour OFFSETS
// that the mesh's frame controller interpolates between and adds to the mesh's own
// COLOURLIST. The sky box shapes use it to carry their two to six time of day colour
// sets: the COLOURLIST is the night sky and frame 0 is all zeros, the day frames add
// the blue back in (re/notes/sky.md). Retail runs it through a streamed prim group
// whose vertices live in RAM; we rewrite the vertex buffer instead.
class VertexColourAnim
{
public:
	i32 numFrames;
	i32 numVertices;
	// [numFrames*numVertices], byte order as in the file's colour list (D3DCOLOR,
	// i.e. b, g, r, unused)
	pddiColour *offsets;

	VertexColourAnim(i32 numFrames, i32 numVertices);
	~VertexColourAnim(void);
	pddiColour *GetFrame(i32 i) { return &offsets[i*numVertices]; }
};

class Geometry : public DrawableContainer
{
	// TODO: unknowns
	bool isFading;
	float fadeAmount;
	// the mesh's own 0x00121201 frame controllers (the sky's "VRTX_<mesh>Shape" ones);
	// the composite drawable copies them into its own list at load time
	std::vector<FrameController*> frameControllers;
	VertexColourAnim *colourAnim;
public:
	enum {
		MESH			= 0x10000,
		SKIN			= 0x10001,
		PRIMGROUP_OLD		= 0x10002,
		BOX			= 0x10003,
		SPHERE			= 0x10004,
		POSITIONLIST		= 0x10005,
		NORMALLIST		= 0x10006,
		UVLIST			= 0x10007,
		COLOURLIST		= 0x10008,
		STRIPLIST		= 0x10009,
		INDEXLIST		= 0x1000A,
		MATRIXLIST		= 0x1000B,
		WEIGHTLIST		= 0x1000C,
		MATRIXPALETTE		= 0x1000D,
		OFFSETLIST		= 0x1000E,
		INSTANCEINFO		= 0x1000F,
		PACKEDNORMALLIST	= 0x10010,
		VERTEXSHADER		= 0x10011,
		MEMORYIMAGEVERTEXLIST	= 0x10012,
		MEMORYIMAGEINDEXLIST	= 0x10013,
		MEMORYIMAGEVERTEXDESCRIPTIONLIST	= 0x10014,
		TANGENTLIST		= 0x10015,
		BINORMALLIST		= 0x10016,
		RENDERSTATUS		= 0x10017,
		EXPRESSIONOFFSETS	= 0x10018,
		SHADOWSKIN		= 0x10019,
		SHADOWMESH		= 0x1001A,
		TOPOLOGY		= 0x1001B,
		MULTICOLOURLIST		= 0x1001C,
		MESHSTATS		= 0x1001D,
		// 1E
		// 1F
		PRIMGROUP		= 0x10020,
		VERTEXCOMPRESSIONHINT	= 0x10021,

		DEFORMVERTEXLIST	= 0x10022,
		DEFORMNORMALLIST	= 0x10023,

		// the "VRTXANIM" vertex animation of a mesh (re/notes/sky.md): a list of
		// key frames, one chunk per key frame, and the per-vertex data itself
		VERTEXANIM		= 0x121305,
		VERTEXANIMFRAME		= 0x121306,
		VERTEXANIMDATA		= 0x10F02,

		// { u32 version; float key; } --- the container sort key, clamped to [0,1]
		// (retail GeometryLoader::LoadObject 0x0069ca7a, stored at container +0x3c)
		SORTKEY			= 0x122000,
	};

	CLASSNAME(Geometry)
	Geometry(i32 nPrimGroup);
	~Geometry(void);

	virtual void Display(DisplayList *list, GameDrawableInfo *info);

	virtual void SetFading(bool enable) { isFading = enable; }
	virtual void SetFadeAmount(float fade) { fadeAmount = fade; }
	virtual bool IsFading(void) { return isFading; }
	virtual float GetFadeAmount(void) { return fadeAmount; }

	void SetColourAnim(VertexColourAnim *anim) { colourAnim = anim; }
	i32 GetNumColourAnimFrames(void) { return colourAnim ? colourAnim->numFrames : 0; }
	// interpolate between key frame floor(frame) and the next one (wrapping) and push
	// the result into the first prim group's vertex buffer. retail does the same thing
	// through a frame controller (re/notes/sky.md).
	void SetColourAnimFrame(float frame);

	std::vector<FrameController*> &GetFrameControllers(void) { return frameControllers; }
	void AddFrameController(FrameController *fc) { fc->AddRef(); frameControllers.push_back(fc); }
};

// retail/SHR: pure3d::VertexAnimController (SHR tVertexAnimController,
// p3d/anim/vertexanimcontroller.cpp). A 'VRTX' animation with one INT channel, also
// tagged 'VRTX', whose value is the INDEX of the morph key frame; SHR has an entity
// channel of keys there and blends between the two bracketing ones, which is what the
// fractional part of our sampled index does.
class VertexAnimationController : public FrameController
{
	Geometry *geometry;
public:
	CLASSNAME(VertexAnimationController);
	VertexAnimationController(void) : geometry(nil) {}
	~VertexAnimationController(void);

	void SetGeometry(Geometry *g);
	virtual void SetFrame(float frame);
};

class GeometryLoader : public SimpleChunkHandler
{
	bool mOptimize;
	u32 mVertexMask;
	PrimGroupLoader *primGroupLoader;
	u8 compressionHints[40];
public:
	CLASSNAME(GeometryLoader)

	GeometryLoader(void);
	~GeometryLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
