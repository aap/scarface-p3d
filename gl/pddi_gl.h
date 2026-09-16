#pragma once
#include "../pddi.h"

// TODO: do this properly...
#include "../p3dview/glad/glad.h"

#include <vector>

namespace pure3d
{

// only internal


// TODO: this is still total shit
class glTexture : public pddiTexture
{
	GLuint gltex;
	int numMips;
public:
	glTexture(void);

	virtual void Bind(int stage);
	virtual void AddMipmap(u32 format, u32 sz, u8 *data);
};




enum { MAX_UNIFORMS = 40 };

enum UniformType
{
	UNIFORM_NA,
	UNIFORM_VEC4,
	UNIFORM_IVEC4,
	UNIFORM_MAT4
};

struct Uniform
{
	char *name;
	UniformType type;
	u32 serial;
	i32 num;
	void *data;
};

struct UniformRegistry
{
	i32 numUniforms;
	Uniform uniforms[MAX_UNIFORMS];

	i32 Register(const char *name, UniformType type = UNIFORM_NA, i32 num = 1);
	i32 Find(const char *name);
	void SetUniform(i32 id, const void *data);
	void Flush(void);
};
extern UniformRegistry uniformRegistry;

class glProgram : public pddiObject
{
public:
	GLuint program;
	i32 numUniforms;
	struct ProgUniform {
		GLint location;
		u32 serial;
	} *uniforms;

	glProgram(const char **vsrc, const char **fsrc);
	~glProgram(void);
	void Bind(void);
};
extern glProgram *currentProgram;



struct MaterialColours
{
	pddiColour ambient;
	pddiColour diffuse;
	pddiColour specular;
	pddiColour emissive;

	MaterialColours(void) : ambient(0, 0, 0, 255), diffuse(0, 0, 0, 255), specular(0, 0, 0, 255), emissive(0, 0, 0, 255) {}
};

class glShader : public pddiBaseShader
{
	static pddiShadeTextureTable glTextureTable[];
	static pddiShadeIntTable glIntTable[];
	static pddiShadeFloatTable glFloatTable[];
	static pddiShadeColourTable glColourTable[];

protected:
	static glProgram *simpleProgram;
	static glProgram *discardProgram;
public:
	glShader(void);
	~glShader(void);
//	virtual const char *GetType(void) { return "fake"; }

	virtual int GetPass(void) { return 1; }
	virtual void SetPass(i32 pass);

	virtual pddiShadeTextureTable	*GetTextureTable(void) { return glTextureTable; }
	virtual pddiShadeIntTable	*GetIntTable(void) { return glIntTable; }
	virtual pddiShadeFloatTable	*GetFloatTable(void) { return glFloatTable; }
	virtual pddiShadeColourTable	*GetColourTable(void) { return glColourTable; }
	virtual pddiShadeVectorTable	*GetVectorTable(void) { return nil; }
	virtual pddiShadeMatrixTable	*GetMatrixTable(void) { return nil; }

	MaterialColours colours;

	pddiTexture *baseTex;
	pddiUVMode uvMode;

	int isLit;
	int twoSided;
	int multiCBV;

	// ??
//	int ecmd;
//	pddiColour scmd;

	pddiBlendMode blendMode;

	bool alphaTest;
	pddiCompareMode alphaCompare;
	float alphaRef;

	void SetTextureDummy(pddiTexture *tex) {}
	void SetIntDummy(int val) {}
	void SetFloatDummy(float val) {}
	void SetColourDummy(pddiColour val) {}

	void SetAmbient(pddiColour col) { colours.ambient = col; }
	void SetDiffuse(pddiColour col) { colours.diffuse = col; }
	void SetSpecular(pddiColour col) { colours.specular = col; }
	void SetEmisive(pddiColour col) { colours.emissive = col; }

	void SetTexture(pddiTexture *tex) {
		if(tex) tex->AddRef();
		if(baseTex) baseTex->Release();
		baseTex = tex;
	}
	void SetUVMode(pddiUVMode mode) { uvMode = mode; }
	void EnableLighting(int enable) { isLit = enable; }
	void SetTwoSided(int enable) { twoSided = enable; }
	void EnableMultiCBV(int enable) { multiCBV = enable; }
	void SetBlendMode(pddiBlendMode mode) { blendMode = mode; }

	void SetAlphaTest(int enable) { alphaTest = enable != 0; }
	void SetAlphaCompare(pddiCompareMode cmp) { alphaCompare = cmp; }
	void SetAlphaRef(float ref) { alphaRef = ref > 1.0f ? 1.0f : ref < 0.0f ? 0.0f : ref; }

//	void SetECMD(int val) { ecmd = val; }
//	void SetSCMD(pddiColour col) { scmd = col; }
};

class glErrorShader : public glShader
{
public:
	virtual const char *GetType(void) { return "error"; }
};

class glReflShader : public glShader
{
public:
	virtual const char *GetType(void) { return "reflection"; }
};

class glSimpleShader : public glShader
{
public:
	virtual const char *GetType(void) { return "simple"; }
};

class glSpecularShader : public glShader
{
public:
	virtual const char *GetType(void) { return "specular"; }
};

class glPointSpriteShader : public glShader
{
public:
	virtual const char *GetType(void) { return "pointsprite"; }
};

class glLayeredShader : public glShader
{
public:
	virtual const char *GetType(void) { return "layered"; }
};

class glFBEffectsShader : public glShader
{
public:
	virtual const char *GetType(void) { return "fbeffectsshader"; }
};

class glShadowShader : public glShader
{
public:
	virtual const char *GetType(void) { return "shadow"; }
};

class glDecalShader : public glShader
{
public:
	virtual const char *GetType(void) { return "decal"; }
	virtual void PreRender(void);
	virtual void PostRender(void);
	virtual void SetPass(int pass);
};

class glCharacterShader : public glShader
{
public:
	virtual const char *GetType(void) { return "character"; }
};

class glSimpleCBVLitShader : public glShader
{
public:
	virtual const char *GetType(void) { return "cbvlit"; }
};

class glVehicleShader : public glShader
{
public:
	virtual const char *GetType(void) { return "vehicle"; }
};

class glShadowDecalShader : public glShader
{
public:
	virtual const char *GetType(void) { return "shadowdecal"; }
	virtual void PreRender(void);
	virtual void PostRender(void);
	virtual void SetPass(int pass);
};

// retail d3dVertexFadeShader (ctor 0x7099b0, SetPass 0x709a30): untextured, lit,
// alpha blended, ambient white / emissive black by default, and its vertex shader gets
// the constants (200, 250, 0.1, ...) --- read as a fade-in band by distance from the
// camera. The low-LOD city is drawn with it, so it vanishes where the real geometry is.
class glVertexFadeShader : public glShader
{
public:
	glVertexFadeShader(void);
	virtual const char *GetType(void) { return "vertexfade"; }
	virtual void SetPass(i32 pass);
};



// the number of pddi light slots the shader can consume; keep in sync with MAXLIGHTS in
// gl/shaders/shader.vert. Retail's d3d context has 8 and pure3d::LightsChooser reduces
// the world lights to 4 per lit object.
enum { GL_MAX_LIGHTS = 4 };

class glState
{
	i32 u_world;
	i32 u_view;
	i32 u_proj;
	i32 u_alphaTest;
	i32 u_matAmbient;
	i32 u_matDiffuse;
	i32 u_matSpecular;
	i32 u_matEmissive;
	i32 u_ambientColour;
	i32 u_lightColour;
	i32 u_lightDir;
	i32 u_lightPos;
	i32 u_lightRange;
	i32 u_debug;
	i32 u_vertexFade;
	i32 u_lit;
	bool isLit;			// the shader's PDDI_SP_ISLIT
	Vector4 vertexFade;		// x: fade start, y: fade end (m from the camera), z: enable
	u32 whiteTex;			// bound when a shader has no texture

	pddiColour ambientColour;
	pddiLightDesc lights[GL_MAX_LIGHTS];
public:
	glState(void);
	void Flush(void);
	void SetAlphaTest(bool alphaTest, pddiCompareMode alphaCompare, float alphaRef);
	void SetAlphaBlend(pddiBlendMode mode);
	void SetMaterial(bool isLit, bool twoSided, const MaterialColours &colors);
	// TODO: more
	void SetTexture(pddiTexture *tex);
	void SetVertexFade(float start, float end, bool enable) { vertexFade = Vector4(start, end, enable ? 1.0f : 0.0f, 0.0f); }

	void SetAmbientColour(pddiColour col) { ambientColour = col; }
	pddiColour GetAmbientColour(void) { return ambientColour; }
	void SetLight(int i, const pddiLightDesc *desc) { if(i >= 0 && i < GL_MAX_LIGHTS) lights[i] = *desc; }
	void EnableLight(int i, bool on) { if(i >= 0 && i < GL_MAX_LIGHTS) lights[i].enabled = on; }
	const pddiLightDesc *GetLight(int i) { return i >= 0 && i < GL_MAX_LIGHTS ? &lights[i] : nil; }
};
extern glState *state;


class glContext : public pddiContext
{
	int worldSP;
	Matrix worldMatrix[20];
	Matrix viewMatrix;
	Matrix projMatrix;
	bool zWrite;
	bool zTest;
public:
	glContext(void);

	virtual int GetMaxLights(void) { return GL_MAX_LIGHTS; }
	virtual void SetAmbientLight(pddiColour colour) { state->SetAmbientColour(colour); }
	virtual pddiColour GetAmbientLight(void) { return state->GetAmbientColour(); }
	virtual void SetLight(int handle, const pddiLightDesc *desc) { state->SetLight(handle, desc); }
	virtual void EnableLight(int handle, bool enable) { state->EnableLight(handle, enable); }
	virtual const pddiLightDesc *GetLight(int handle) { return state->GetLight(handle); }

	virtual void Begin(void);
	virtual void End(void);
	virtual Matrix &GetWorldMatrix(void) { return worldMatrix[worldSP]; }
	virtual void SetWorldMatrix(const Matrix &mat);
	virtual void MultWorldMatrix(const Matrix &mat);
	virtual void PushWorldMatrix(void);
	virtual void PopWorldMatrix(void);
	virtual Matrix &GetViewMatrix(void) { return viewMatrix; }
	virtual void SetViewMatrix(const Matrix &mat);
	virtual Matrix &GetProjectionMatrix(void) { return projMatrix; }
	virtual void SetProjectionMatrix(const Matrix &mat);
	virtual void PushDebugName(const char *name);
	virtual void PopDebugName(void);

	virtual void DrawPrimBuffer(pddiShader *mat, pddiPrimBuffer *buf);
	virtual void SetZWrite(bool enable) { zWrite = enable; glDepthMask(enable ? GL_TRUE : GL_FALSE); }
	virtual bool GetZWrite(void) { return zWrite; }
	virtual void SetZTest(bool enable) {
		zTest = enable;
		if(enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	}
	virtual bool GetZTest(void) { return zTest; }
	virtual void SetColourWrite(bool r, bool g, bool b, bool a) {
		glColorMask(r ? GL_TRUE : GL_FALSE, g ? GL_TRUE : GL_FALSE,
		            b ? GL_TRUE : GL_FALSE, a ? GL_TRUE : GL_FALSE);
	}
};


enum {
	ATTRIB_POS,
	ATTRIB_NORMAL,
	ATTRIB_COLOR,
	ATTRIB_WEIGHTS,
	ATTRIB_INDICES,
	ATTRIB_TEXCOORDS0,
	ATTRIB_TEXCOORDS1,
};

struct AttribDesc
{
	GLint index;
	GLint size;
	GLenum type;
	GLboolean normalized;
	GLsizei stride;
	GLuint offset;
};

class glPrimBuffer : public pddiPrimBuffer
{
	GLenum primType;
	pddiPrimType primTypePDDI;
	u32 vertexFormat;
	int nVertices;
	int nIndices;
	GLuint ibo;
	GLuint vbo;
	u8 *vertexBuffer;
	bool lockedVertex;
	bool lockedIndex;
	int stride;
	AttribDesc *attribs;

public:
	glPrimBuffer(pddiPrimType primType, u32 vertexFormat, u32 nVertices, u32 nIndices);
	~glPrimBuffer(void);

	virtual pddiPrimBufferStream *Lock(void);
	virtual void Unlock(pddiPrimBufferStream *stream);

	virtual void SetVertices(void *vertices);
	virtual void SetIndices(void *indices);

	void Display(void);
};

}
