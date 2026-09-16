#include "zonepkg.h"

#include <string.h>

namespace renderer
{

using namespace pure3d;

ZonePkgRenderable::ZonePkgRenderable(void)
 : Renderable(0)
{
	typeMask = 0x8000;
}

ZonePkgRenderable::~ZonePkgRenderable(void)
{
	for(u32 i = 0; i < worldGeos.Size(); i++)
		if(worldGeos[i])
			worldGeos[i]->Release();
}

ZonePkgLoader::ZonePkgLoader(void)
 : SimpleChunkHandler(Renderable::ZONEPKG_LOADER)
{
}

// retail: ZonePkgLoader::LoadObject 0x472680, entry 0x471c10
void
ZonePkgLoader::LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];
	char wgName[256];

	f->GetString(name);
	ZonePkgRenderable *pkg = new ZonePkgRenderable;
	pkg->SetName(name);

	// the name list is vestigial; the array is filled from the child chunks
	u32 numNames = f->GetU32();
	pkg->worldGeos.Create(numNames);
	for(u32 i = 0; i < numNames; i++) {
		pkg->worldGeos[i] = nil;
		f->GetString(wgName);
	}

	u32 n = 0;
	while(f->ChunksRemaining()) {
		f->BeginChunk();
		if(f->GetCurrentID() == 0x8800009) {
			f->GetString(wgName);
			WorldGeoRenderable *wg = inventory->Find<WorldGeoRenderable>(wgName);
			if(wg && n < numNames) {
				wg->AddRef();
				pkg->worldGeos[n++] = wg;
			}

			u32 numFloats = f->GetU32();
			float v[16];
			for(u32 i = 0; i < numFloats; i++) {
				float x = f->GetFloat();
				if(i < nelem(v)) v[i] = x;
			}
			if(wg) {
				float fade = (v[1] - v[0]) * 0.2f;
				if(numFloats >= 3) fade = v[2];
				float ox = 0.0f, oz = 0.0f;
				bool haveOther = false;
				if(numFloats >= 5) { ox = v[4]; haveOther = true; }
				if(numFloats >= 6) oz = -v[5];
				if(haveOther && ox == 0.0f && oz == 0.0f)
					haveOther = false;
				wg->SetElementDrawDist(0, v[0], v[1], fade);
				if(haveOther) {
					wg->useOtherPosition = true;
					wg->otherPosition = Vector(ox, 0.0f, oz);
				}
			}
		}
		f->EndChunk();
	}

	*pObject = pkg;
	*pUID = pkg->GetUID();
}

}
