#pragma once

#include "worldgeo.h"

namespace renderer
{

// 0x08800004: one per shell/detail package. Lists the WorldGeoRenderables of
// the package and, in 0x08800009 children, gives each its draw distances and
// the horizontal reference point used for the LOD distance test.
class ZonePkgRenderable : public Renderable
{
public:
	Array<WorldGeoRenderable*> worldGeos;

	CLASSNAME(ZonePkgRenderable)
	ZonePkgRenderable(void);
	~ZonePkgRenderable(void);
};

class ZonePkgLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(ZonePkgLoader)

	ZonePkgLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
