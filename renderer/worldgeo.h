#pragma once

#include "renderable.h"
#include "../loadmanager.h"

namespace renderer
{

using namespace content;

// retail: the three draw distances of the custom Display path, g[0x7c0a48] (details),
// g[0x7c0a44] (shells/underwater) and g[0x7c0a40] (skyline). Retail rewrites them from
// the "DrawDistance" video setting (renderContext+0x540, 0..2) on every Display; we set
// them once. The defaults are retail's .data values, i.e. level 2.
void SetWorldGeoDrawDistanceLevel(i32 level);	// retail: 0x471874..0x4718e9

// retail: renderer::WorldGeoRenderable : Renderable, vtable 0x007381e4, ctor 0x471490,
// 0xa0 bytes, typeMask 8. One piece of static map geometry. It is the only class that
// can cull and fade the sub-primitives of its composite individually (primitives[] /
// poseIDs[], its Display override at 0x471640) and the only one with a distance
// reference point of its own instead of its matrix.
class WorldGeoRenderable : public Renderable
{
public:
	Vector otherPosition;		// +0x84, from the ZonePkg 0x8800009 record
	// +0x90
	bool useOtherPosition : 1;	// 0x01
	bool isDetails : 1;		// 0x02  "details_" / "cbvlitdecals_"
	bool isSkyline : 1;		// 0x04  "skyline_"
	bool drawFirst : 1;		// 0x08  "shells_" / "underwater_"
	bool isLowLOD : 1;		// 0x10  "low_LOD_"
	DisplayListPrimitive *primitives;	// +0x94
	i32 *poseIDs;			// +0x98
	i32 numPrimitives;		// +0x9c

	CLASSNAME(WorldGeoRenderable)
	WorldGeoRenderable(void);
	~WorldGeoRenderable(void);
	void SetNumPrimitives(i32 n);

	virtual void SetVisible(bool visible);		// retail: 0x471570
	virtual void Display(void);			// retail: 0x471640
	virtual void Hide(void);			// retail: 0x471510
	virtual bool GetDistanceRefPos(Vector *p);	// retail: 0x4714e0
};

class WorldGeoLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(WorldGeoLoader)

	WorldGeoLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
