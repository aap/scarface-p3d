#pragma once

#include "entity.h"
#include "loadmanager.h"
#include "texture.h"
#include "pddi.h"

namespace pure3d
{

using namespace core;

class Shader : public Entity
{
	pddiShader *shader;
	Texture *texture;	// what's the point of this?
	pddiBlendMode blendMode;
	u8 type;
	bool flag1 : 1;
	bool bIsLit : 1;
	bool bIsALUM : 1;
	bool bAlphaTest : 1;

public:
	enum {
		SHADER_SIMPLE,
		SHADER_ENV,
		SHADER_VEHICLE,
		SHADER_SPECULAR,
		SHADER_SPECULAR_MCBV,
		SHADER_CHARACTER,
		SHADER_LAYERED,
		SHADER_DECAL,
		SHADER_FOAM,
		SHADER_CBVLIT,
		SHADER_SHADOWDECAL,
		SHADER_NIGHTLIGHT,
		SHADER_UNTEXTURED,
		SHADER_VERTEXFADE,
		SHADER_UNKNOWN
	};

	enum {
		SHADER = 0x11000,
		SHADER_DEFINITION = 0x11001,
		TEXTURE_PARAM = 0x11002,
		INT_PARAM = 0x11003,
		FLOAT_PARAM = 0x11004,
		COLOUR_PARAM = 0x11005,
		VECTOR_PARAM = 0x11006,
		MATRIX_PARAM = 0x11007,
	};
	CLASSNAME(Shader)
	Shader(const char *name, char *definition = nil);
	~Shader(void);

	pddiShader *GetShader(void) { return shader; }
	u32 GetType(void) { return type; }
	void SetLit(bool lit);
	bool GetIsLit(void) { return bIsLit; }
	void SetALUM(bool alum);
	bool GetALUM(void) { return bIsALUM; }
	void SetBlendMode(pddiBlendMode mode);
	pddiBlendMode GetBlendMode(void) { return blendMode; }
	void SetAlphaTest(bool alphatest);
	bool GetAlphaTest(void) { return bAlphaTest; }
	void SetMultiCBV(i32 multiCBV);

	void SetTexture(u32 param, Texture *tex) {
		texture = tex;
		shader->SetTexture(param, tex->GetTexture());
	};
	void SetInt(u32 param, i32 val) { shader->SetInt(param, val); }
	void SetFloat(u32 param, float val) { shader->SetFloat(param, val); }
	void SetColour(u32 param, pddiColour color) { shader->SetColour(param, color); }
	void SetVector(u32 param, const pddiVector &vec) { shader->SetVector(param, vec); }
	void SetMatrix(u32 param, const pddiMatrix &mat) { shader->SetMatrix(param, mat); }

	// What the hell?
	bool IsXXXBlendMode(void) {
		return blendMode == PDDI_BLEND_ALPHA ||
			blendMode == PDDI_BLEND_ADD ||
			blendMode == PDDI_BLEND_SUBTRACT ||
			blendMode == PDDI_BLEND_SUBMODULATEALPHA;
	}
	bool IsBlendAddSub(void) {
		return blendMode == PDDI_BLEND_ADD ||
			blendMode == PDDI_BLEND_SUBTRACT;
	}
};

class ShaderLoader : public content::SimpleChunkHandler
{
public:
	CLASSNAME(ShaderLoader)
	ShaderLoader(void) : content::SimpleChunkHandler(Shader::SHADER) {}
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
