#include <stdlib.h>
#include <string.h>
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
   mPrimBuffer(nil),
   mFade(0.0f),
   mBaseColours(nil), mBaseUVs(nil)
{
}

PrimGroup::~PrimGroup(void)
{
	delete[] mBaseColours;
	delete[] mBaseUVs;
}

// retail: PrimGroup::SetFade 0x006a46b0 --- it keeps nothing, it sets the pddi shader's
// 'FADE' float. The display list walks push the container's fade into every primitive
// they are about to draw and reset it to 0 afterwards (notes/displaylist.md §4), so the
// value is per draw, not per primitive.
void
PrimGroup::SetFade(float fade)
{
	mFade = fade;
	if(mShader)
		mShader->SetFloat(PDDI_SP_FADE, fade);
}

void
PrimGroup::Display(void)
{
	if(mFade >= 1.0f)
		return;
	context->DrawPrimBuffer(mShader->GetShader(), mPrimBuffer);
}

void
PrimGroup::SetBaseColours(const pddiColour *colours, u32 n)
{
	delete[] mBaseColours;
	mBaseColours = new pddiColour[n];
	for(u32 i = 0; i < n; i++)
		mBaseColours[i] = colours[i];
}

void
PrimGroup::SetVertexColourOffsets(const pddiColour *offsets, u32 n)
{
	if(mPrimBuffer == nil || mBaseColours == nil || (mVertexFormat & PDDI_V_COLOUR) == 0)
		return;
	if(n > mVertexCount)
		n = mVertexCount;
	pddiPrimBufferStream *stream = mPrimBuffer->Lock();
	for(u32 i = 0; i < n; i++) {
		u32 base = mBaseColours[i].c, off = offsets[i].c;
		u32 col = 0;
		for(int j = 0; j < 3; j++) {
			u32 v = ((base>>(j*8))&0xFF) + ((off>>(j*8))&0xFF);
			col |= (v > 255 ? 255 : v) << (j*8);
		}
		col |= base & 0xFF000000;
		stream->Colour(pddiColour(col));
		stream->Next();
	}
	mPrimBuffer->Unlock(stream);
}

void
PrimGroup::SetBaseUVs(const pddiVector2 *uvs, u32 n)
{
	delete[] mBaseUVs;
	mBaseUVs = new pddiVector2[n];
	for(u32 i = 0; i < n; i++)
		mBaseUVs[i] = uvs[i];
}

void
PrimGroup::SetVertexUVOffsets(const pddiVector2 *offsets, u32 n)
{
	if(mPrimBuffer == nil || mBaseUVs == nil || pddiNumUVSets(mVertexFormat) == 0)
		return;
	if(n > mVertexCount)
		n = mVertexCount;
	pddiPrimBufferStream *stream = mPrimBuffer->Lock();
	for(u32 i = 0; i < n; i++) {
		stream->TexCoord2(mBaseUVs[i].x + offsets[i].x, mBaseUVs[i].y + offsets[i].y, 0);
		stream->Next();
	}
	mPrimBuffer->Unlock(stream);
}

void
PrimGroup::SetVertexColour(pddiColour colour)
{
	if(mPrimBuffer == nil || (mVertexFormat & PDDI_V_COLOUR) == 0)
		return;
	pddiPrimBufferStream *stream = mPrimBuffer->Lock();
	for(u32 i = 0; i < mVertexCount; i++) {
		stream->Colour(colour);
		stream->Next();
	}
	mPrimBuffer->Unlock(stream);
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

	// TODO: skinned and all arguments.
	// unk3 != 0 means the prim group is vertex animated: the six sky box shapes carry
	// 0x00121305/0x00121306/0x00010f02 morph targets (re/notes/sky.md) and retail keeps
	// those in a streamed group so the animation can rewrite the vertices every frame.
	// We have no vertex animation, so load them as ordinary optimized groups and show
	// the rest pose --- without this there is no sky at all.
	if(!unk2 || !optimize)
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
			if(mVertexFormat & PDDI_V_NORMAL) {
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
				pddiVector2 *uvs = new pddiVector2[n];
				f->GetData(uvs, n*2, sizeof(float));
				stream = buf->Lock();
				for(u32 i = 0; i < n; i++) {
					stream->TexCoord2(uvs[i].x, uvs[i].y, channel);
					stream->Next();
				}
				buf->Unlock(stream);
				// kept so the vertex uv animation can offset them
				if(channel == 0)
					pg->SetBaseUVs(uvs, n);
				delete[] uvs;
			}
			break;

		case Geometry::COLOURLIST:
			if(mVertexFormat & PDDI_V_COLOUR) {
				u32 n = f->GetI32();
				assert(n == mVertexCount);
				pddiColour *colours = new pddiColour[n];
				f->GetData(colours, n, 4);
				stream = buf->Lock();
				for(u32 i = 0; i < n; i++) {
					stream->Colour(colours[i]);
					stream->Next();
				}
				buf->Unlock(stream);
				// kept so the vertex colour animation can offset them
				pg->SetBaseColours(colours, n);
				delete[] colours;
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
