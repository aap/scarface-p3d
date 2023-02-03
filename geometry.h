#pragma once

#include "entity.h"
#include "drawable.h"
#include "primgroup.h"
#include "loadmanager.h"

namespace pure3d
{

using namespace core;
using namespace content;


class Geometry : public DrawableContainer
{
	// TODO: unknowns
	bool isFading;
	float fadeAmount;
	// frame controller
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
	};

	CLASSNAME(Geometry)
	Geometry(i32 nPrimGroup);

	virtual void Display(DisplayList *list, GameDrawableInfo *info);

	virtual void SetFading(bool enable) { isFading = enable; }
	virtual void SetFadeAmount(float fade) { fadeAmount = fade; }
	virtual bool IsFading(void) { return isFading; }
	virtual float GetFadeAmount(void) { return fadeAmount; }
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
