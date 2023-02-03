#include "refcount.h"
#include "geometry.h"

#include <string.h>
#include <assert.h>

namespace pure3d
{

Geometry::Geometry(i32 nPrimGroup)
 : DrawableContainer(nPrimGroup), isFading(false), fadeAmount(0.0f)
{
}

void
Geometry::Display(DisplayList *list, GameDrawableInfo *info)
{
	// TOOD: frame controller
	DrawPrimitives(list, info);
}




GeometryLoader::GeometryLoader(void)
 : SimpleChunkHandler(Geometry::MESH),
   mOptimize(true),
   mVertexMask(0xFFFFFFFF),
   primGroupLoader(nil)
{
	// this is very strange
	compressionHints[0] = 0x20;
	compressionHints[1] = 0x20;
	compressionHints[2] = 0x20;
	compressionHints[3] = 0x20;
	compressionHints[4] = 0x20;
	compressionHints[5] = 0x20;
	compressionHints[6] = 0x20;
	for(int i = 7; i < 40; i++)
		compressionHints[i] = 0;
	Assign(primGroupLoader, new PrimGroupLoader);
}

GeometryLoader::~GeometryLoader(void)
{
	Release(primGroupLoader);
}

void
GeometryLoader::LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	u8 hints[40];
	char name[256];

	memcpy(hints, compressionHints, sizeof(compressionHints));
	f->GetString(name);

//	printf("geometry: %s\n", name);

	i32 version = f->GetI32();

	i32 nPrimGroup = f->GetI32();
	Geometry *geo = new Geometry(nPrimGroup);
	geo->SetName(name);

	int primGroupCount = 0;

	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()){
		case Geometry::PRIMGROUP:
			assert(primGroupCount < nPrimGroup);
			primGroupLoader->SetVertexMask(mVertexMask);
			primGroupLoader->Load(f, geo->GetElement(primGroupCount), inventory, mOptimize);
			primGroupCount++;
			break;

		case Geometry::BOX:
			f->GetData(&geo->box.low, 3, sizeof(float));
			f->GetData(&geo->box.high, 3, sizeof(float));
			break;

		case Geometry::SPHERE:
			f->GetData(&geo->sphere.centre, 3, sizeof(float));
			geo->sphere.radius = f->GetFloat();
			break;

		case Geometry::VERTEXCOMPRESSIONHINT:
			f->GetI32();	// unused
			hints[0] = f->GetI32();
			hints[1] = f->GetI32();
			hints[2] = f->GetI32();
			hints[3] = f->GetI32();
			hints[4] = f->GetI32();
			hints[5] = f->GetI32();
			hints[6] = f->GetI32();
			break;

		// TODO: more

		default:
//			printf("	geo chunk %X\n", f->GetCurrentID());
			break;
		}
		f->EndChunk();
	}

	*pObject = geo;
	*pUID = geo->GetUID();
}

}
