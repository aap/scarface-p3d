#include "refcount.h"
#include "geometry.h"

#include <string.h>
#include <math.h>
#include <assert.h>

namespace pure3d
{

VertexUVAnim::VertexUVAnim(i32 numFrames, i32 numVertices)
 : numFrames(numFrames), numVertices(numVertices)
{
	offsets = new pddiVector2[numFrames*numVertices];
	for(i32 i = 0; i < numFrames*numVertices; i++)
		offsets[i].x = offsets[i].y = 0.0f;
}

VertexUVAnim::~VertexUVAnim(void)
{
	delete[] offsets;
}

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
 : DrawableContainer(nPrimGroup), isFading(false), fadeAmount(0.0f), colourAnim(nil), uvAnim(nil)
{
}

Geometry::~Geometry(void)
{
	for(u32 i = 0; i < frameControllers.size(); i++)
		Release(frameControllers[i]);
	delete colourAnim;
	delete uvAnim;
}


VertexAnimationController::~VertexAnimationController(void)
{
}

// NOT ref counted: the geometry owns the controller, so a reference back would be a cycle
void
VertexAnimationController::SetGeometry(Geometry *g)
{
	geometry = g;
}

// SHR: tVertexAnimController::Update --- the group id is the prim group index (always 0
// in the sky) and the channel value is the morph key frame index.
void
VertexAnimationController::SetFrame(float frame)
{
	if(geometry == nil || animation == nil)
		return;
	frame = animation->MakeValidFrame(frame + frameOffset);
	for(u32 i = 0; i < animation->groups.size(); i++) {
		const Animation::Channel *c = animation->groups[i].Find(Animation::CHANNEL_VERTEX);
		if(c)
			geometry->SetColourAnimFrame(c->GetInt(frame));
	}
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
	if((colourAnim == nil && uvAnim == nil) || GetNumElements() < 1)
		return;
	PrimGroup *pg = (PrimGroup*)GetElement(0)->prim;
	if(pg == nil)
		return;
	if(colourAnim) {
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
	if(uvAnim) {
		// no wrap: the last key holds (the horizon palette runs u 0 -> 1 over the day
		// and must not slide back to the night column just before midnight)
		i32 nf = uvAnim->numFrames;
		i32 nv = uvAnim->numVertices;
		float f = frame;
		if(f < 0.0f) f = 0.0f;
		if(f > (float)(nf-1)) f = (float)(nf-1);
		i32 i0 = (i32)f;
		i32 i1 = i0+1 < nf ? i0+1 : i0;
		float t = f - (float)i0;
		pddiVector2 *a = uvAnim->GetFrame(i0);
		pddiVector2 *b = uvAnim->GetFrame(i1);
		pddiVector2 *tmp = new pddiVector2[nv];
		for(i32 i = 0; i < nv; i++) {
			tmp[i].x = a[i].x + (b[i].x - a[i].x)*t;
			tmp[i].y = a[i].y + (b[i].y - a[i].y)*t;
		}
		pg->SetVertexUVOffsets(tmp, nv);
		delete[] tmp;
	}
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
			VertexUVAnim *uvanim = nil;
			while(f->ChunksRemaining()) {
				if(f->BeginChunk() == Geometry::VERTEXANIMFRAME) {
					f->GetI32();		// version
					i32 frame = f->GetI32();
					f->GetI32();		// unknown, 0
					while(f->ChunksRemaining()) {
						u32 id = f->BeginChunk();
						if(id == Geometry::VERTEXANIMDATA || id == Geometry::VERTEXANIMUV) {
							// { u32 version; u32 "CLR0"; u32 count;
							//   { u32 vertex; u16 r, g, b, a; } [count] }
							f->GetI32();
							u32 type = f->GetU32();
							i32 n = f->GetI32();
							if(type == FOURCC("UV0\0") && numFrames > 0 && n > 0) {
								// { u32 vertex; float u, v; } [count]
								if(uvanim == nil)
									uvanim = new VertexUVAnim(numFrames, n);
								for(i32 i = 0; i < n; i++) {
									u32 v = f->GetU32();
									float u = f->GetFloat(), vv = f->GetFloat();
									if(frame >= 0 && frame < uvanim->numFrames &&
									   (i32)v < uvanim->numVertices) {
										uvanim->GetFrame(frame)[v].x = u;
										uvanim->GetFrame(frame)[v].y = vv;
									}
								}
							} else if(type == FOURCC("CLR0") && numFrames > 0 && n > 0) {
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
			if(uvanim)
				geo->SetUVAnim(uvanim);
			break;
		}

		// the mesh's own frame controller, "VRTX_<mesh>" for the sky boxes. It has to
		// come after the VERTEXANIM chunk, which it does in every file.
		case Animation::FRAME_CONTROLLER: {
			FrameControllerInfo info;
			ReadFrameControllerInfo(f, &info);
			if(info.type != Animation::TYPE_VRTX)
				break;
			Animation *anim = inventory->Find<Animation>(info.animName);
			if(anim == nil)
				break;
			VertexAnimationController *ctrl = new VertexAnimationController;
			ctrl->SetName(info.name);
			ctrl->frameOffset = info.frameOffset;
			ctrl->SetAnimation(anim);
			ctrl->SetGeometry(geo);
			geo->AddFrameController(ctrl);
			break;
		}

		// retail: 0x0069ca7a. It is what puts the sky in the right order --- the dome
		// is 1.0, the cloud layers 0.3 and 0.2, the horizon gradient 0.0, and list 46
		// is sorted by CmpKey (descending), so the dome is painted first and the
		// clouds and the gradient over it. Without it everything sits at the ctor's
		// 0.5 and the dome can cover the lot (re/notes/sky.md).
		case Geometry::SORTKEY: {
			f->GetI32();			// version
			float key = f->GetFloat();
			geo->sortKey = key < 0.0f ? 0.0f : key > 1.0f ? 1.0f : key;
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
