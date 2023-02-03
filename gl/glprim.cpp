#include "../pddi.h"
#include "pddi_gl.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

namespace pure3d
{

// TODO: this possibly goes somewhere else
// also: could cache this

AttribDesc*
MakeAttribDesc(u32 vertexFormat)
{
	AttribDesc attribs[20], *ret;
	int offset = 0;
	int n = 0;

	u32 nColourSets = pddiNumColourSets(vertexFormat);
	u32 nUV = pddiNumUVSets(vertexFormat);

	if(vertexFormat & PDDI_V_POSITION) {
		attribs[n].index = ATTRIB_POS;
		attribs[n].size = 3;
		attribs[n].type = GL_FLOAT;
		attribs[n].normalized = GL_FALSE;
		attribs[n].offset = offset;
		n++;
		offset += 3*4;
	}
	if(vertexFormat & PDDI_V_WEIGHTS) {
		attribs[n].index = ATTRIB_WEIGHTS;
		attribs[n].size = 3;
		attribs[n].type = GL_FLOAT;
		attribs[n].normalized = GL_FALSE;
		attribs[n].offset = offset;
		n++;
		offset += 3*4;
	}
	if(vertexFormat & PDDI_V_INDICES) {
		attribs[n].index = ATTRIB_INDICES;
		attribs[n].size = 3;
		attribs[n].type = GL_UNSIGNED_BYTE;
		attribs[n].normalized = GL_TRUE;
		attribs[n].offset = offset;
		n++;
		offset += 4;
	}
	if(vertexFormat & PDDI_V_NORMAL) {
		attribs[n].index = ATTRIB_NORMAL;
		attribs[n].size = 3;
		attribs[n].type = GL_FLOAT;
		attribs[n].normalized = GL_FALSE;
		attribs[n].offset = offset;
		n++;
		offset += 3*4;
	}
	if(vertexFormat & PDDI_V_COLOUR) {
		attribs[n].index = ATTRIB_COLOR;
		attribs[n].size = 4;
		attribs[n].type = GL_UNSIGNED_BYTE;
		attribs[n].normalized = GL_TRUE;
		attribs[n].offset = offset;
		n++;
		offset += 4;
	}
	if(vertexFormat & PDDI_V_COLOUR2 && nColourSets > 0) {
		offset += 2*4;
	}
	if(vertexFormat & PDDI_V_SPECULAR) {
		offset += 4;
	}
	for(u32 i = 0; i < nUV; i++) {
		attribs[n].index = ATTRIB_TEXCOORDS0 + i;
		attribs[n].size = 2;
		attribs[n].type = GL_FLOAT;
		attribs[n].normalized = GL_FALSE;
		attribs[n].offset = offset;
		n++;
		offset += 2*4;
	}
	if(vertexFormat & PDDI_V_COLOUR2 && nColourSets > 2) {
		offset += (nColourSets-2)*4;
	}
	if(vertexFormat & PDDI_V_BINORMAL) {
		offset += 3*4;
	}
	if(vertexFormat & PDDI_V_TANGENT) {
		offset += 3*4;
	}
	if(vertexFormat & PDDI_V_DEFORM) {
		offset += 3*4;
		offset += 3*4;
	}

	attribs[n].index = -1;

	for(int i = 0; i < n; i++)
		attribs[i].stride = offset;

	ret = new AttribDesc[n+1];
	memcpy(ret, attribs, (n+1)*sizeof(AttribDesc));
	return ret;
}

void
SetAttribDesc(AttribDesc *desc)
{
	for(; desc->index >= 0; desc++) {
		glEnableVertexAttribArray(desc->index);
		glVertexAttribPointer(desc->index, desc->size, desc->type, desc->normalized, desc->stride, (void*)(uintptr_t)desc->offset);
	}
	glVertexAttrib4f(ATTRIB_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
}

void
ClearAttribDesc(AttribDesc *desc)
{
	for(; desc->index >= 0; desc++)
		glDisableVertexAttribArray(desc->index);
}




static GLenum primTypeMap[] = {
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_LINES,
	GL_LINE_STRIP,
	GL_POINT
};






class glPrimBufferStream : public pddiPrimBufferStream
{
	bool locked;
	void *basePtr;
	int nVertex;
	int maxVertex;
	u32 stride;
	int nColourSets;
	int nUV;

	pddiVector *coord;
	pddiVector *normal;
	u32 *compressedNormal;
	pddiColour *colour;
	pddiColour *specular;
	pddiVector2 *uv;
	pddiVector *binormal;
	pddiVector *tangent;
	pddiColour *colour2;
	pddiVector *deformedCoord;
	pddiVector *deformedNormal;
	u8 *skinIndices;
	pddiVector *skinWeights;
public:
	glPrimBufferStream(void);

	void Lock(void) { assert(!locked); locked = true; }
	void Unlock(void) { assert(locked); locked = false; }
	void SetBasePtr(void *base, u32 stride, u32 vertexFormat, int vertCount);

	virtual void Position(float x, float y, float z);
	virtual void Normal(float x, float y, float z);
	virtual void DeformedPosition(float x, float y, float z);
	virtual void DeformedNormal(float x, float y, float z);
	virtual void Binormal(float x, float y, float z);
	virtual void Tangent(float x, float y, float z);
	virtual void Colour(pddiColour colour, int channel = 0);
	virtual void TexCoord1(float s, int channel = 0) {}
	virtual void TexCoord2(float s, float t, int channel = 0);
	virtual void TexCoord3(float s, float t, float u, int channel = 0) {}
	virtual void TexCoord4(float s, float t, float u, float v, int channel = 0) {}
	virtual void Specular(pddiColour colour, int channel = 0);
	virtual void SkinIndices(u32 i0, u32 i1 = 0, u32 i2 = 0, u32 i3 = 0);
	virtual void SkinWeights(float w0, float w1 = 0.0f, float w2 = 0.0f);

	virtual void Next(void);
};

glPrimBufferStream::glPrimBufferStream(void)
 : locked(false),
   basePtr(nil),
   nVertex(0),
   maxVertex(0),
   stride(0),
   nColourSets(0),
   nUV(0),
   coord(nil),
   normal(nil),
   compressedNormal(nil),
   colour(nil),
   specular(nil),
   uv(nil),
   binormal(nil),
   tangent(nil),
   colour2(nil),
   deformedCoord(nil),
   deformedNormal(nil),
   skinIndices(nil),
   skinWeights(nil)
{
}

void
glPrimBufferStream::SetBasePtr(void *base, u32 str, u32 vertexFormat, int vertCount)
{
	basePtr = base;
	maxVertex = vertCount;
	stride = str;
	nVertex = 0;
	coord = nil;
	normal = nil;
	compressedNormal = nil;
	colour = nil;
	specular = nil;
	uv = nil;
	binormal = nil;
	tangent = nil;
	colour2 = nil;
	deformedCoord = nil;
	deformedNormal = nil;
	skinIndices = nil;
	skinWeights = nil;
	nColourSets = pddiNumColourSets(vertexFormat);
	nUV = pddiNumUVSets(vertexFormat);

	u8 *ptr = (u8*)basePtr;

	coord = (pddiVector*)ptr;
	ptr += sizeof(pddiVector);
	if(vertexFormat & PDDI_V_WEIGHTS) {
		skinWeights = (pddiVector*)ptr;
		ptr += sizeof(pddiVector);		// pure3d has twice this???
	}
	if(vertexFormat & PDDI_V_INDICES) {
		skinIndices = (u8*)ptr;
		ptr += 4;
	}
	if(vertexFormat & PDDI_V_NORMAL) {
		normal = (pddiVector*)ptr;
		ptr += sizeof(pddiVector);
	}
	if(vertexFormat & PDDI_V_COLOUR) {
		colour = (pddiColour*)ptr;
		ptr += sizeof(pddiColour);
	}
	if(vertexFormat & PDDI_V_COLOUR2 && nColourSets > 0) {
		colour = (pddiColour*)ptr;
		ptr += 2*sizeof(pddiColour);
	}
	if(vertexFormat & PDDI_V_SPECULAR) {
		specular = (pddiColour*)ptr;
		ptr += sizeof(pddiColour);
	}
	if(nUV) {
		uv = (pddiVector2*)ptr;
		ptr += sizeof(pddiVector2) * nUV;
	}
	if(vertexFormat & PDDI_V_COLOUR2 && nColourSets > 2) {
		colour2 = (pddiColour*)ptr;
		ptr += (nColourSets-2)*sizeof(pddiColour);
	}
	if(vertexFormat & PDDI_V_BINORMAL) {
		binormal = (pddiVector*)ptr;
		ptr += sizeof(pddiVector);
	}
	if(vertexFormat & PDDI_V_TANGENT) {
		tangent = (pddiVector*)ptr;
		ptr += sizeof(pddiVector);
	}
	if(vertexFormat & PDDI_V_DEFORM) {
		deformedCoord = (pddiVector*)ptr;
		ptr += sizeof(pddiVector);
		deformedNormal = (pddiVector*)ptr;
	}
}

void
glPrimBufferStream::Position(float x, float y, float z)
{
	assert(coord < (void*)((char*)basePtr + stride*maxVertex));
	coord->x = x;
	coord->y = y;
	coord->z = z;
	Next();
}
void
glPrimBufferStream::Normal(float x, float y, float z)
{
	assert(normal < (void*)((char*)basePtr + stride*maxVertex));
	if(compressedNormal) {
		assert(0 && "no compressed normals");
	} else {
		normal->x = x;
		normal->y = y;
		normal->z = z;
	}
}

void
glPrimBufferStream::DeformedPosition(float x, float y, float z)
{
	assert(deformedCoord < (void*)((char*)basePtr + stride*maxVertex));
	deformedCoord->x = x;
	deformedCoord->y = y;
	deformedCoord->z = z;
}

void
glPrimBufferStream::DeformedNormal(float x, float y, float z)
{
	assert(deformedNormal < (void*)((char*)basePtr + stride*maxVertex));
	deformedNormal->x = x;
	deformedNormal->y = y;
	deformedNormal->z = z;
}

void
glPrimBufferStream::Binormal(float x, float y, float z)
{
	assert(binormal < (void*)((char*)basePtr + stride*maxVertex));
	binormal->x = x;
	binormal->y = y;
	binormal->z = z;
}

void
glPrimBufferStream::Tangent(float x, float y, float z)
{
	assert(tangent < (void*)((char*)basePtr + stride*maxVertex));
	tangent->x = x;
	tangent->y = y;
	tangent->z = z;
}

void
glPrimBufferStream::Colour(pddiColour col, int channel)
{
	if(channel < 2) {
		assert(&colour[channel] < (void*)((char*)basePtr + stride*maxVertex));
		colour[channel] = col;
	} else {
		assert(&colour2[channel-2] < (void*)((char*)basePtr + stride*maxVertex));
		colour2[channel-2] = col;
	}
}

void
glPrimBufferStream::TexCoord2(float s, float t, int channel)
{
	assert(&uv[channel] < (void*)((char*)basePtr + stride*maxVertex));
	uv[channel].x = s;
	uv[channel].y = t;
}

void
glPrimBufferStream::Specular(pddiColour colour, int channel)
{
	assert(specular < (void*)((char*)basePtr + stride*maxVertex));
	*specular = colour;
}

void
glPrimBufferStream::SkinIndices(u32 i0, u32 i1, u32 i2, u32 i3)
{
	assert(skinIndices < (void*)((char*)basePtr + stride*maxVertex));
	skinIndices[0] = i0*3;
	skinIndices[1] = i1*3;
	skinIndices[2] = i2*3;
	skinIndices[3] = i3*3;
}

void
glPrimBufferStream::SkinWeights(float w0, float w1, float w2)
{
	assert(skinWeights < (void*)((char*)basePtr + stride*maxVertex));
	skinWeights->x = w0;
	skinWeights->y = w1;
	skinWeights->z = w2;
}


void
glPrimBufferStream::Next(void)
{
	coord = (pddiVector*)((char*)coord + stride);
	if(skinWeights) skinWeights = (pddiVector*)((char*)skinWeights + stride);
	if(skinIndices) skinIndices = (u8*)((char*)skinIndices + stride);
	if(normal) normal = (pddiVector*)((char*)normal + stride);
	if(colour) colour = (pddiColour*)((char*)colour + stride);
	if(specular) specular = (pddiColour*)((char*)specular + stride);
	if(uv) uv = (pddiVector2*)((char*)uv + stride);
	if(colour2) colour2 = (pddiColour*)((char*)colour2 + stride);
	if(binormal) binormal = (pddiVector*)((char*)binormal + stride);
	if(tangent) tangent = (pddiVector*)((char*)tangent + stride);
	if(deformedCoord) deformedCoord = (pddiVector*)((char*)deformedCoord + stride);
	if(deformedNormal) deformedNormal = (pddiVector*)((char*)deformedNormal + stride);
}






glPrimBuffer::glPrimBuffer(pddiPrimType primType, u32 vertexFormat, u32 nVertices, u32 nIndices)
 : primType(primTypeMap[primType]),
   primTypePDDI(primType),
   vertexFormat(vertexFormat),
   nVertices(nVertices),
   nIndices(nIndices),
   lockedVertex(false),
   lockedIndex(false)
{
	attribs = MakeAttribDesc(vertexFormat);
	stride = attribs->stride;
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ibo);
	vertexBuffer = new u8[stride*nVertices];
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, stride*nVertices, nil, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 2*nIndices, nil, GL_STATIC_DRAW);
}

glPrimBuffer::~glPrimBuffer(void)
{
	delete[] vertexBuffer;
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ibo);
}

pddiPrimBufferStream*
glPrimBuffer::Lock(void)
{
	lockedVertex = true;
	glPrimBufferStream *stream =  new glPrimBufferStream;
	stream->Lock();
	stream->SetBasePtr(vertexBuffer, stride, vertexFormat, nVertices);
	return stream;
}

void
glPrimBuffer::Unlock(pddiPrimBufferStream *stream)
{
	lockedVertex = false;
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, stride*nVertices, vertexBuffer, GL_STATIC_DRAW);
	((glPrimBufferStream*)stream)->Unlock();
}


void
glPrimBuffer::SetVertices(void *vertices)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	memcpy(vertexBuffer, vertices, stride*nVertices);
	glBufferData(GL_ARRAY_BUFFER, stride*nVertices, vertices, GL_STATIC_DRAW);
}

void
glPrimBuffer::SetIndices(void *indices)
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 2*nIndices, indices, GL_STATIC_DRAW);
}

void
glPrimBuffer::Display(void)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	SetAttribDesc(attribs);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	state->Flush();
	glDrawElements(primType, nIndices, GL_UNSIGNED_SHORT, 0);
	ClearAttribDesc(attribs);
}


}
