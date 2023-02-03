#include "loadmanager.h"
#include "geometry.h"
#include "shader.h"
#include "primgroup.h"

#include <assert.h>

namespace pure3d
{

PrimGroup::PrimGroup(u32 vertexFormat, u32 vertexCount)
 : mShader(nil),
   mPrimType(PDDI_PRIM_TRIANGLES),
   mVertexFormat(vertexFormat),
   mVertexCount(vertexCount),
   mUnknown2(0xFFFFFFFF),
   mPrimBuffer(nil)
{
}

void
PrimGroup::Display(void)
{
	context->DrawPrimBuffer(mShader->GetShader(), mPrimBuffer);
}

void
PrimGroup::SetShader(Shader *shader)
{
	Assign(mShader, shader);
	if(mShader) {
		// TODO: some weird stuff
	}
}

bool
PrimGroup::IsLit(void)
{
	return mShader && mShader->GetIsLit();
}

bool
PrimGroup::IsALUM(void)
{
	return mShader && mShader->GetALUM();
}



PrimGroupLoader::PrimGroupLoader(void)
 : mShader(nil),
   mPrimType(PDDI_PRIM_TRIANGLES),
   unk1(false),
   unk3(0),
   unk4(0),
   mVertexFormatMask(0xFFFFFFFF),
   mVertexFormat(0xFFFFFFFF),
   mVertexCount(0),
   mIndexCount(0),
   mMatrixCount(0)
{
	unk2 = true;
}

void
PrimGroupLoader::Load(content::ChunkFile *f, PrimEntry *entry, content::LoadInventory *inventory, bool optimize)
{
	if(!ParseHeader(f, inventory))
		return;

	// TODO: skinned and all arguments
	if(!unk2 || !optimize || unk3)
		printf("skip load streamed\n");
	else
		LoadOptimized(entry, f, inventory);

	Release(mShader);
}

bool
PrimGroupLoader::ParseHeader(content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];

	if(f->GetCurrentID() != Geometry::PRIMGROUP)
		return false;

	i32 version = f->GetI32();
	f->GetString(name);
//	printf("primgroup %s\n", name);

	Release(mShader);
	mShader = inventory->Find<Shader>(name);
	if(mShader == nil) {
		fprintf(stderr, "warning : shader \"%s\" not found while loading primgroup from file %s\n",
			name, f->GetName());
		mShader = new Shader("error");
	}
	mShader->AddRef();

	mPrimType = (pddiPrimType)f->GetU32();
	mVertexFormat = f->GetU32() & mVertexFormatMask;
//printf("format: %X\n", mVertexFormat);
	mVertexCount = f->GetU32();
	mIndexCount = f->GetU32();
	mMatrixCount = f->GetU32();
	unk1 = f->GetU32() != 0;
	unk2 = f->GetU32() != 0;
	unk3 = f->GetU32();
	unk4 = f->GetU32();

	mVertexFormat |= PDDI_V_POSITION;

	return true;
}

void
PrimGroupLoader::LoadOptimized(PrimEntry *entry, ChunkFile *f, LoadInventory *inventory)
{
	int nColoursChannels = pddiNumColourSets(mVertexFormat);
	int nUVChannels = pddiNumUVSets(mVertexFormat);
	pddiPrimBufferStream *stream;

	// TODO: skinned
	PrimGroup *pg = new PrimGroupOptimized(mVertexFormat, mVertexCount);
	pg->SetShader(mShader);
	pg->SetPrimType(mPrimType);
	entry->SetPrimitive(pg);
	// TODO: this should use a descriptor
	pddiPrimBuffer *buf = device->NewPrimBuffer(mPrimType, mVertexFormat, mVertexCount, mIndexCount);
	pg->SetPrimBuffer(buf);

	// TODO: stuff

	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()){
		case Geometry::BOX:
			f->GetData(&pg->box.low, 3, sizeof(float));
			f->GetData(&pg->box.high, 3, sizeof(float));
			break;

		case Geometry::SPHERE:
			f->GetData(&pg->sphere.centre, 3, sizeof(float));
			pg->sphere.radius = f->GetFloat();
			break;

		case Geometry::POSITIONLIST:
			if(mVertexFormat & PDDI_V_POSITION) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				stream = buf->Lock();
				pddiVector v;
				while(n--) {
					f->GetData(&v, 3, sizeof(float));
					stream->Position(v.x, v.y, v.z);
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::NORMALLIST:
			if(mVertexFormat & PDDI_V_POSITION) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				stream = buf->Lock();
				pddiVector v;
				while(n--) {
					f->GetData(&v, 3, sizeof(float));
					stream->Normal(v.x, v.y, v.z);
					stream->Next();
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::DEFORMVERTEXLIST:
			if(mVertexFormat & PDDI_V_DEFORM) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				stream = buf->Lock();
				pddiVector v;
				while(n--) {
					f->GetData(&v, 3, sizeof(float));
					stream->DeformedPosition(v.x, v.y, v.z);
					stream->Next();
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::DEFORMNORMALLIST:
			if(mVertexFormat & PDDI_V_DEFORM) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				stream = buf->Lock();
				pddiVector v;
				while(n--) {
					f->GetData(&v, 3, sizeof(float));
					stream->DeformedNormal(v.x, v.y, v.z);
					stream->Next();
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::TANGENTLIST:
			if(mVertexFormat & PDDI_V_TANGENT) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				stream = buf->Lock();
				pddiVector v;
				while(n--) {
					f->GetData(&v, 3, sizeof(float));
					stream->Tangent(v.x, v.y, v.z);
					stream->Next();
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::BINORMALLIST:
			if(mVertexFormat & PDDI_V_BINORMAL) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				stream = buf->Lock();
				pddiVector v;
				while(n--) {
					f->GetData(&v, 3, sizeof(float));
					stream->Binormal(v.x, v.y, v.z);
					stream->Next();
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::UVLIST:
			if(nUVChannels) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				int channel = f->GetI32();
				assert(channel < nUVChannels);
				stream = buf->Lock();
				pddiVector2 v;
				while(n--) {
					f->GetData(&v, 2, sizeof(float));
					stream->TexCoord2(v.x, v.y, channel);
					stream->Next();
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::COLOURLIST:
			if(mVertexFormat & PDDI_V_COLOUR) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				stream = buf->Lock();
				pddiColour c;
				while(n--) {
					f->GetData(&c, 4);
					stream->Colour(c);
					stream->Next();
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::MULTICOLOURLIST:
			if(mVertexFormat & PDDI_V_COLOUR2) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				int channel = f->GetI32();
				assert(channel < nColoursChannels);
				stream = buf->Lock();
				pddiColour c;
				while(n--) {
					f->GetData(&c, 4);
					stream->Colour(c, channel);
					stream->Next();
				}
				buf->Unlock(stream);
			}
			break;

		case Geometry::INDEXLIST: {
			u32 n = f->GetI32();
			assert(n == mIndexCount);
			u16 *indices = new u16[n];
			for(u32 i = 0; i < n; i++)
				indices[i] = f->GetU32();
			buf->SetIndices(indices);	// TODO: actually with index count
			delete[] indices;
			break;
		}

		case Geometry::MATRIXLIST:
			// indices
			break;

		case Geometry::WEIGHTLIST:
			break;

		case Geometry::MATRIXPALETTE:
			break;

		case Geometry::MEMORYIMAGEVERTEXLIST:
		case Geometry::MEMORYIMAGEVERTEXDESCRIPTIONLIST:
			break;

		case Geometry::MEMORYIMAGEINDEXLIST:
			break;

		case 0x121204:		// TODO: what's this?
			break;

		}
		f->EndChunk();
	}

	// Finalize primbuf, but unused on PC
}


}
