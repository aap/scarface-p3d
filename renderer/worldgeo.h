#pragma once

#include "renderable.h"
#include "../loadmanager.h"

namespace renderer
{

using namespace content;

class WorldGeoRenderable : public Renderable
{
public:
	Vector otherPosition;
	bool useOtherPosition : 1;
	bool isDetails : 1;
	bool isSkyline : 1;
	bool drawFirst : 1;
	bool isLowLOD : 1;
	DisplayListPrimitive *primitives;
	i32 *poseIDs;
	i32 numPrimitives;

	CLASSNAME(WorldGeoRenderable)
	WorldGeoRenderable(void);
	~WorldGeoRenderable(void);
	void SetNumPrimitives(i32 n);

	virtual void SetVisible(bool visible);
};

class WorldGeoLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(WorldGeoLoader)

	WorldGeoLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
