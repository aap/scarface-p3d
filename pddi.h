#pragma once

#include "core.h"
#include "gmath.h"

namespace pure3d
{

using namespace math;
using namespace core;

#define FOURCC(s) ((u32)s[0] | (u32)s[1]<<8 | (u32)s[2]<<16 | (u32)s[3]<<24)

enum {
	// TODO

	PDDI_SP_BASETEX		= FOURCC("TEX"),
	PDDI_SP_TOPTEX		= FOURCC("TTEX"),
	PDDI_SP_TWOLAYERCBV	= FOURCC("CLYR"),
	PDDI_SP_UVMODE		= FOURCC("UVMD"),
	PDDI_SP_FILTER		= FOURCC("FIMD"),
	PDDI_SP_SHADEMODE	= FOURCC("SHMD"),
	PDDI_SP_ISLIT		= FOURCC("LIT"),
	PDDI_SP_AMBIENT		= FOURCC("AMBI"),
	PDDI_SP_DIFFUSE		= FOURCC("DIFF"),
	PDDI_SP_EMISSIVE	= FOURCC("EMIS"),
	PDDI_SP_SPECULAR	= FOURCC("SPEC"),
	PDDI_SP_BLENDMODE	= FOURCC("BLMD"),
	PDDI_SP_TEXBLENDMODE	= FOURCC("TBLM"),
	PDDI_SP_ALPHATEST	= FOURCC("ATST"),
	PDDI_SP_ALPHACOMPARE	= FOURCC("ACMP"),
	PDDI_SP_REFLMAP		= FOURCC("REFL"),

	PDDI_SP_TWOSIDED	= FOURCC("2SID"),

	PDDI_SP_ENVBLEND	= FOURCC("ENVB"),

	PDDI_SP_ZWRITE		= FOURCC("ZWRT"),
	PDDI_SP_ZTEST		= FOURCC("ZTST"),
	PDDI_SP_ALPHACOMPARE_THRESHOLD	= FOURCC("ACTH"),
	PDDI_SP_MULTI_CBV	= FOURCC("MCBV"),
	PDDI_SP_CBV_BLEND_VALUE	= FOURCC("CBVV"),

/* unknown */
	PDDI_SP_ALUM		= FOURCC("ALUM"),
	PDDI_SP_TCI		= FOURCC("TCI"),
	PDDI_SP_PLMD		= FOURCC("PLMD"),
	PDDI_SP_COLB		= FOURCC("COLB"),
	PDDI_SP_TRNB		= FOURCC("TRNB"),
};

enum {
	PDDI_V_UVCOUNT0		= 0,
	PDDI_V_UVCOUNT1		= 1,
	PDDI_V_UVCOUNT2		= 2,
	PDDI_V_UVCOUNT3		= 3,
	PDDI_V_UVCOUNT4		= 4,
	PDDI_V_UVCOUNT5		= 5,
	PDDI_V_UVCOUNT6		= 6,
	PDDI_V_UVCOUNT7		= 7,
	PDDI_V_UVCOUNT8		= 8,
	PDDI_V_NORMAL		= 1<<4,
	PDDI_V_COLOUR		= 1<<5,
	PDDI_V_SPECULAR		= 1<<6,
	PDDI_V_INDICES		= 1<<7,
	PDDI_V_WEIGHTS		= 1<<8,
	PDDI_V_SIZE		= 1<<9,
	PDDI_V_W		= 1<<10,
	PDDI_V_BINORMAL		= 1<<11,
	PDDI_V_TANGENT		= 1<<12,
	PDDI_V_POSITION		= 1<<13,
	PDDI_V_COLOUR2		= 1<<14,
	PDDI_V_COLOUR_COUNT0	= 0<<15,
	PDDI_V_COLOUR_COUNT1	= 1<<15,
	PDDI_V_COLOUR_COUNT2	= 2<<15,
	PDDI_V_COLOUR_COUNT3	= 3<<15,
	PDDI_V_COLOUR_COUNT4	= 4<<15,
	PDDI_V_COLOUR_COUNT5	= 5<<15,
	PDDI_V_COLOUR_COUNT6	= 6<<15,
	PDDI_V_COLOUR_COUNT7	= 7<<15,

	PDDI_V_DEFORM		= 1<<20
};
inline int pddiNumUVSets(u32 format) { return format&0xF; }
inline int pddiNumColourSets(u32 format) {
	return format&PDDI_V_COLOUR ? 1 :
	       format&PDDI_V_COLOUR2 ? (format>>15)&7 :
	       0;
}

enum pddiCullMode
{
	PDDI_CULL_NONE,
	PDDI_CULL_NORMAL,
	PDDI_CULL_INVERTED,
	PDDI_CULL_SHADOW_BACKFACE,
	PDDI_CULL_SHADOW_FRONTFACE
};

enum pddiCompareMode
{
	PDDI_COMPARE_NONE,
	PDDI_COMPARE_ALWAYS,
	PDDI_COMPARE_LESS,
	PDDI_COMPARE_LESSEQUAL,
	PDDI_COMPARE_GREATER,
	PDDI_COMPARE_GREATEREQUAL,
	PDDI_COMPARE_EQUAL,
	PDDI_COMPARE_NOTEQUAL
};

enum pddiTextureBlendMode
{
	PDDI_TEXBLEND_DECAL,
	PDDI_TEXBLEND_DECALALPHA,
	PDDI_TEXBLEND_MODULATE,
	PDDI_TEXBLEND_MODULATEALPHA,
	PDDI_TEXBLEND_ADD,
	PDDI_TEXBLEND_MODULATEINTENSITY
};

enum pddiUVMode
{
	PDDI_UV_TILE,
	PDDI_UV_CLAMP
};

enum pddiFilterMode
{
	PDDI_FILTER_NONE,
	PDDI_FILTER_BILINEAR,
	PDDI_FILTER_MIPMAP,
	PDDI_FILTER_MIPMAP_BILINEAR,
	PDDI_FILTER_MIPMAP_TRILINEAR
};

enum pddiBlendMode
{
	PDDI_BLEND_NONE,
	PDDI_BLEND_ALPHA,
	PDDI_BLEND_ADD,
	PDDI_BLEND_SUBTRACT,
	PDDI_BLEND_MODULATE,
	PDDI_BLEND_MODULATE2,
	PDDI_BLEND_ADDMODULATEALPHA,
	PDDI_BLEND_SUBMODULATEALPHA,
	PDDI_BLEND_DESTALPHA
};

enum pddiShadeMode
{
	PDDI_SHADE_FLAT,
	PDDI_SHADE_GOURAUD
};

enum pddiPrimType
{
	PDDI_PRIM_TRIANGLES,
	PDDI_PRIM_TRISTRIP,
	PDDI_PRIM_LINES,
	PDDI_PRIM_LINESTRIP,
	PDDI_PRIM_POINTS
};


struct pddiVector2
{
	float x, y;
};

struct pddiVector
{
	float x, y, z;
};

struct pddiMatrix
{
	float e[16];
};

struct pddiColour
{
	u32 c;

	pddiColour(void) {}
	pddiColour(u32 color) : c(color) {}
	pddiColour(u8 r, u8 g, u8 b) : c((u32)r | (u32)g<<8 | (u32)b<<16) {}
	pddiColour(u8 r, u8 g, u8 b, u8 a) : c((u32)r | (u32)g<<8 | (u32)b<<16 | (u32)a<<24) {}
	u8 R(void) { return c&0xFF; }
	u8 G(void) { return (c>>8)&0xFF; }
	u8 B(void) { return (c>>16)&0xFF; }
	u8 A(void) { return (c>>24)&0xFF; }
};

class pddiObject
{
	int refCount;
	int lastError;
public:
	pddiObject(void) : refCount(0), lastError(1) {}
	virtual void AddRef(void) { refCount++; }
	virtual bool Release(void) {
		if(--refCount < 1) {
			delete this;
			return true;
		}
		return false;
	}
	virtual int GetLastError(void) { return lastError; }
	virtual ~pddiObject(void) {}
};


class pddiTexture : public pddiObject
{
	// TODO
public:
	// this is actually completely wrong:
	virtual void Bind(int stage) = 0;
	virtual void AddMipmap(u32 format, u32 sz, u8 *data) = 0;
};


class pddiShader : public pddiObject
{
public:
	virtual const char *GetType(void) = 0;
	virtual bool SetTexture(u32 param, pddiTexture *tex) = 0;
	virtual bool SetInt(u32 param, int) = 0;
	virtual bool SetFloat(u32 param, float) = 0;
	virtual bool SetColour(u32 param, pddiColour) = 0;
	virtual bool SetVector(u32 param, const pddiVector&) = 0;
	virtual bool SetMatrix(u32 param, const pddiMatrix&) = 0;
};

class pddiPrimBufferStream
{
public:
	virtual void Position(float x, float y, float z) = 0;
	virtual void Normal(float x, float y, float z) = 0;
	// 3 null
	// null(arg)
	// null
	virtual void DeformedPosition(float x, float y, float z) = 0;
	virtual void DeformedNormal(float x, float y, float z) = 0;
	virtual void Binormal(float x, float y, float z) {}
	virtual void Tangent(float x, float y, float z) {}
	virtual void Colour(pddiColour colour, int channel = 0) = 0;
	virtual void TexCoord1(float s, int channel = 0) = 0;
	virtual void TexCoord2(float s, float t, int channel = 0) = 0;
	virtual void TexCoord3(float s, float t, float u, int channel = 0) = 0;
	virtual void TexCoord4(float s, float t, float u, float v, int channel = 0) = 0;
	virtual void Specular(pddiColour colour, int channel = 0) = 0;
	virtual void SkinIndices(u32 i0, u32 i1 = 0, u32 i2 = 0, u32 i3 = 0) = 0;
	virtual void SkinWeights(float w0, float w1 = 0.0f, float w2 = 0.0f) = 0;
	// vertex....
	virtual void Next(void) = 0;
};

class pddiPrimBuffer : public pddiObject
{
public:
	virtual pddiPrimBufferStream *Lock(void) = 0;
	virtual void Unlock(pddiPrimBufferStream *stream) = 0;

	// not original
	virtual void SetVertices(void *vertices) = 0;
	virtual void SetIndices(void *indices) = 0;
};

class pddiDevice : public pddiObject
{
public:
	// NewDisplay
	// NewRenderContext
	virtual pddiTexture *NewTexture(void) = 0;
	// TODO: this should take a descriptor
	virtual pddiPrimBuffer *NewPrimBuffer(pddiPrimType primType, u32 vertexFormat, u32 nVertices, u32 nIndices) = 0;
	virtual pddiShader *NewShader(const char *name, const char *def = nil) = 0;
};
extern pddiDevice *device;

class pddiContext : public pddiObject
{
public:
	// these are kinda wrong
	virtual void Begin(void) = 0;
	virtual void End(void) = 0;
	virtual Matrix &GetWorldMatrix(void) = 0;
	virtual void SetWorldMatrix(const Matrix &mat) = 0;
	virtual void MultWorldMatrix(const Matrix &mat) = 0;
	virtual void PushWorldMatrix(void) = 0;
	virtual void PopWorldMatrix(void) = 0;
	virtual Matrix &GetViewMatrix(void) = 0;
	virtual void SetViewMatrix(const Matrix &mat) = 0;
	virtual Matrix &GetProjectionMatrix(void) = 0;
	virtual void SetProjectionMatrix(const Matrix &mat) = 0;
	virtual void PushDebugName(const char *name) {}
	virtual void PopDebugName(void) {}

	virtual void DrawPrimBuffer(pddiShader *mat, pddiPrimBuffer *buf) = 0;
};
extern pddiContext *context;

void InitDevice(void);


class pddiBaseShader;

// TODO?: context
typedef pddiBaseShader *(*pddiShadeAllocFunc)(const char *name, const char *aux);

#define SHADETABLE(type, arg) \
typedef void (pddiBaseShader::*pddiShade##type##Function)(arg); \
struct pddiShade##type##Table { \
	u32 param; \
	pddiShade##type##Function func; \
};
SHADETABLE(Texture, pddiTexture*)
SHADETABLE(Int, int)
SHADETABLE(Float, float)
SHADETABLE(Colour, pddiColour)
SHADETABLE(Vector, const pddiVector&)
SHADETABLE(Matrix, const pddiMatrix&)
#define SHADE_TEXTURE(x) ((pddiShadeTextureFunction)x)
#define SHADE_INT(x) ((pddiShadeIntFunction)x)
#define SHADE_FLOAT(x) ((pddiShadeFloatFunction)x)
#define SHADE_COLOUR(x) ((pddiShadeColourFunction)x)
#define SHADE_VECTOR(x) ((pddiShadeVectorFunction)x)
#define SHADE_MATRIX(x) ((pddiShadeMatrixFunction)x)

class pddiBaseShader : public pddiShader
{
public:
	static void InstallShader(const char *name, pddiShadeAllocFunc func, const char *aux);
	// TODO? context
	static pddiBaseShader *AllocateShader(const char *name, const char *aux);

	virtual bool SetTexture(u32 param, pddiTexture *tex);
	virtual bool SetInt(u32 param, int);
	virtual bool SetFloat(u32 param, float);
	virtual bool SetColour(u32 param, pddiColour);
	virtual bool SetVector(u32 param, const pddiVector&);
	virtual bool SetMatrix(u32 param, const pddiMatrix&);

	virtual int GetPass(void) = 0;
	virtual void SetMaterial(i32 pass);
	virtual void PreRender(void) {};
	virtual void PostRender(void) {};
	virtual void SetPass(i32 pass) = 0;

	virtual pddiShadeTextureTable	*GetTextureTable(void) { return nil; }
	virtual pddiShadeIntTable	*GetIntTable(void) { return nil; }
	virtual pddiShadeFloatTable	*GetFloatTable(void) { return nil; }
	virtual pddiShadeColourTable	*GetColourTable(void) { return nil; }
	virtual pddiShadeVectorTable	*GetVectorTable(void) { return nil; }
	virtual pddiShadeMatrixTable	*GetMatrixTable(void) { return nil; }

	// TODO? or pddi? GetShaderInfo
	// ShaderPass ??
	// PickVertexProgram
	// SetupShader

	static void ClearCurrentShader(void);
};

}
