#include "refcount.h"
#include "geometry.h"

#include <string.h>
#include <math.h>
#include <assert.h>

namespace pure3d
{

VertexColourAnim::VertexColourAnim(i32 numFrames, i32 numVertices)
 : numFrames(numFrames), numVertices(numVertices)
{
	offsets = new pddiColour[numFrames*numVertices];
	for(i32 i = 0; i < numFrames*numVertices; i++)
		offsets[i] = pddiColour((u32)0);
}

VertexColourAnim::~VertexColourAnim(void)
{
	delete[] offsets;
}


Geometry::Geometry(i32 nPrimGroup)
 : DrawableContainer(nPrimGroup), isFading(false), fadeAmount(0.0f), colourAnim(nil)
{
}

Geometry::~Geometry(void)
{
	delete colourAnim;
}

void
Geometry::Display(DisplayList *list, GameDrawableInfo *info)
{
	// TOOD: frame controller
	DrawPrimitives(list, info);
}

void
Geometry::SetColourAnimFrame(float frame)
{
	if(colourAnim == nil || GetNumElements() < 1)
		return;
	PrimGroup *pg = (PrimGroup*)GetElement(0)->prim;
	if(pg == nil)
		return;
	i32 nf = colourAnim->numFrames;
	i32 nv = colourAnim->numVertices;
	float f = fmodf(frame, (float)nf);
	if(f < 0.0f) f += (float)nf;
	i32 i0 = (i32)f;
	if(i0 >= nf) i0 = nf-1;
	i32 i1 = (i0+1)%nf;
	float t = f - (float)i0;
	pddiColour *a = colourAnim->GetFrame(i0);
	pddiColour *b = colourAnim->GetFrame(i1);
	pddiColour *tmp = new pddiColour[nv];
	for(i32 i = 0; i < nv; i++)
		tmp[i] = pddiColour(
			(u8)(a[i].R() + (b[i].R() - a[i].R())*t),
			(u8)(a[i].G() + (b[i].G() - a[i].G())*t),
			(u8)(a[i].B() + (b[i].B() - a[i].B())*t),
			0);
	pg->SetVertexColourOffsets(tmp, nv);
	delete[] tmp;
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

		case Geometry::VERTEXANIM: {
			// { u32 version; u32 numFrames; u32 keyFrame[numFrames];
			//   u32 unknown[numFrames]; } + one VERTEXANIMFRAME per key frame
			f->GetI32();			// version
			i32 numFrames = f->GetI32();
			VertexColourAnim *anim = nil;
			while(f->ChunksRemaining()) {
				if(f->BeginChunk() == Geometry::VERTEXANIMFRAME) {
					f->GetI32();		// version
					i32 frame = f->GetI32();
					f->GetI32();		// unknown, 0
					while(f->ChunksRemaining()) {
						if(f->BeginChunk() == Geometry::VERTEXANIMDATA) {
							// { u32 version; u32 "CLR0"; u32 count;
							//   { u32 vertex; u16 r, g, b, a; } [count] }
							f->GetI32();
							u32 type = f->GetU32();
							i32 n = f->GetI32();
							if(type == FOURCC("CLR0") && numFrames > 0 && n > 0) {
								if(anim == nil)
									anim = new VertexColourAnim(numFrames, n);
								for(i32 i = 0; i < n; i++) {
									u32 v = f->GetU32();
									// r, g, b as u16, then a fourth
									// word that is always 0
									u32 r = f->GetU16(), g = f->GetU16();
									u32 bl = f->GetU16();
									f->GetU16();
									// store in the colour list's byte
									// order (b, g, r)
									if(frame >= 0 && frame < anim->numFrames &&
									   (i32)v < anim->numVertices)
										anim->GetFrame(frame)[v] =
											pddiColour(bl, g, r, 0);
								}
							}
						}
						f->EndChunk();
					}
				}
				f->EndChunk();
			}
			if(anim)
				geo->SetColourAnim(anim);
			break;
		}

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
