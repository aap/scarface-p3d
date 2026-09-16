#pragma once

#include "refcount.h"
#include "drawable.h"
#include "pddi.h"

namespace content
{
	class ChunkFile;
	class LoadInventory;
};

namespace pure3d
{

using namespace content;

class Shader;

class PrimGroup : public DrawablePrimitive
{
	Shader *mShader;
	pddiPrimType mPrimType;
	u32 mVertexFormat;
	u32 mVertexCount;
	u32 mUnknown2;		// set from loader unk4
	pddiPrimBuffer *mPrimBuffer;
	// retail: DrawablePrimitive vslot 14, pushed in by the display list walks around
	// the draw (notes/displaylist.md §4). Retail turns it into a shader alpha; we only
	// honour the "completely gone" end of it, which is what the rainy sky box and a
	// finished LOD cross-fade need.
	float mFade;
	// a copy of the COLOURLIST, so that the vertex colour animation can add its
	// per-frame offsets to it instead of accumulating them
	pddiColour *mBaseColours;
	// the same for UV channel 0: the sky's horizon gradient is a palette of horizon
	// colours by hour and its 'UV0' vertex animation slides u across it over the day
	pddiVector2 *mBaseUVs;
public:
	PrimGroup(u32 vertexFormat, u32 vertexCount);
	~PrimGroup(void);

	virtual u32 GetSomeMask(void) { return 1; }
	virtual void Display(void);
	virtual void SetFade(float fade) { mFade = fade; }
	virtual Shader *GetShader(void) const { return mShader; }
	virtual void SetShader(Shader *shader);
	virtual bool IsLit(void);
	virtual bool IsALUM(void);

	void SetPrimType(pddiPrimType primType) { mPrimType = primType; }
	void SetPrimBuffer(pddiPrimBuffer *buf) { Assign(mPrimBuffer, buf); }
	// the sky's vertex colour animation (re/notes/sky.md): remember the loaded colours
	// and rewrite the vertex buffer's colour channel with base + offset
	void SetBaseColours(const pddiColour *colours, u32 n);
	void SetVertexColourOffsets(const pddiColour *offsets, u32 n);
	void SetBaseUVs(const pddiVector2 *uvs, u32 n);
	void SetVertexUVOffsets(const pddiVector2 *offsets, u32 n);
	// not retail: paint every vertex one colour (the sky's below-horizon hemisphere
	// takes the fog colour until the ocean covers it, renderer/sky.cpp)
	void SetVertexColour(pddiColour colour);
};

class PrimGroupStreamed : public PrimGroup
{
	// vertexList
public:
	CLASSNAME(PrimGroupStreamed)
	virtual u32 GetSomeMask(void) { return 0x81; }
};

class PrimGroupSkinnedStreamed : public PrimGroupStreamed
{
	// bones
	// matrix palette
public:
	CLASSNAME(PrimGroupSkinnedStreamed)
	virtual u32 GetSomeMask(void) { return 0x181; }
};

class PrimGroupOptimized : public PrimGroup
{
	// no new members
public:
	CLASSNAME(PrimGroupOptimized)
	PrimGroupOptimized(u32 vertexFormat, u32 vertexCount) : PrimGroup(vertexFormat, vertexCount) {}
};

class PrimGroupSkinnedOptimized : public PrimGroupOptimized
{
	// bones
	// matrix palette
public:
	CLASSNAME(PrimGroupSkinnedOptimized)
};

class PrimGroupSkinnedPC : public PrimGroupSkinnedOptimized
{
	// vertexList
public:
	CLASSNAME(PrimGroupSkinnedPC)
	virtual u32 GetSomeMask(void) { return 0x181; }
};

// NOTE:
//	VertexAnimGroup	mask 0x40

	class RenderObjectLoader : public NonCopyable
	{
	};

class PrimGroupLoader : public RenderObjectLoader
{
	Shader *mShader;
	pddiPrimType mPrimType;
	bool unk1;
	bool unk2;	// optimized?
	u16 unk3;
	u32 unk4;
	u32 mVertexFormatMask;
	u32 mVertexFormat;
	u32 mVertexCount;
	u32 mIndexCount;
	u32 mMatrixCount;
public:
	CLASSNAME(PrimGroupLoader)
	PrimGroupLoader(void);

	void SetVertexMask(u32 mask) { mVertexFormatMask = mask; }
	// TODO: more
	void Load(ChunkFile *f, PrimEntry *entry, LoadInventory *inventory, bool optimize);
	bool ParseHeader(ChunkFile *f, LoadInventory *inventory);
	void LoadOptimized(PrimEntry *entry, ChunkFile *f, LoadInventory *inventory);
};

}
