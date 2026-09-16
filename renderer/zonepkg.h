#pragma once

#include "worldgeo.h"

namespace renderer
{

// retail: renderer::ZonePkgRenderable : Renderable, vtable 0x00738254, 0x90 bytes,
// typeMask 0x8000. Not geometry: a named bag of WorldGeoRenderables = one streaming
// package = one .p3d. It has zero elements, so it draws nothing itself.
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

	// retail: 0x471bc0 --- does NOT call the base, it forwards to every member
	virtual void SetVisible(bool visible);
	// retail: 0x438400 --- a package has no transform
	virtual void SetMatrix(const Matrix &m) {}
};

class ZonePkgLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(ZonePkgLoader)

	ZonePkgLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
