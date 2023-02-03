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
public:
	PrimGroup(u32 vertexFormat, u32 vertexCount);

	virtual u32 GetSomeMask(void) { return 1; }
	virtual void Display(void);
	virtual Shader *GetShader(void) const { return mShader; }
	virtual void SetShader(Shader *shader);
	virtual bool IsLit(void);
	virtual bool IsALUM(void);

	void SetPrimType(pddiPrimType primType) { mPrimType = primType; }
	void SetPrimBuffer(pddiPrimBuffer *buf) { Assign(mPrimBuffer, buf); }
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
