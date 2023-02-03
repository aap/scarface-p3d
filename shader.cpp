#include "core.h"
#include "texture.h"
#include "shader.h"

#include <string.h>
#include <assert.h>

namespace pure3d
{

using namespace content;

Shader::Shader(const char *name, char *definition)
{
	flag1 = false;
	bIsLit = false;
	bIsALUM = false;
	bAlphaTest = false;
	texture = nil;
	blendMode = PDDI_BLEND_NONE;
	type = SHADER_UNKNOWN;

	shader = device->NewShader(name, definition);
	assert(shader);
	shader->AddRef();

	if(strcmp(name, "simple") == 0) type = SHADER_SIMPLE;
	else if(strcmp(name, "cbvlit") == 0) type = SHADER_CBVLIT;
	else if(strcmp(name, "specular") == 0) type = SHADER_SPECULAR;
	else if(strcmp(name, "vehicle") == 0) type = SHADER_VEHICLE;
	else if(strcmp(name, "environment") == 0 ||
	        strcmp(name, "spheremap") == 0) type = SHADER_ENV;
	else if(strcmp(name, "character") == 0) { type = SHADER_CHARACTER; bIsLit = 1; }
	else if(strcmp(name, "layered") == 0) type = SHADER_LAYERED;
	else if(strcmp(name, "decal") == 0) type = SHADER_DECAL;
	else if(strcmp(name, "foam") == 0) type = SHADER_FOAM;
	else if(strcmp(name, "nightlight") == 0) type = SHADER_NIGHTLIGHT;
	else if(strcmp(name, "shadowdecal") == 0) type = SHADER_SHADOWDECAL;
	else if(strcmp(name, "untextured") == 0) type = SHADER_UNTEXTURED;
	else if(strcmp(name, "vertexfade") == 0) type = SHADER_VERTEXFADE;
}

Shader::~Shader(void)
{
	Release(shader);
}

void
Shader::SetLit(bool lit)
{
	SetInt(PDDI_SP_ISLIT, lit);
	bIsLit = lit;
}

void
Shader::SetALUM(bool alum)
{
	SetInt(PDDI_SP_ALUM, alum);
	bIsALUM = alum;
}

void
Shader::SetBlendMode(pddiBlendMode mode)
{
	shader->SetInt(PDDI_SP_BLENDMODE, mode);
	if(strcmp(shader->GetType(), "layered") == 0 ||
	   strcmp(shader->GetType(), "lightmap") == 0)
		blendMode = PDDI_BLEND_NONE;
	else
		blendMode = mode;
}

void
Shader::SetAlphaTest(bool alphatest)
{
	SetInt(PDDI_SP_ALPHATEST, alphatest);
	bAlphaTest = alphatest;
}

void
Shader::SetMultiCBV(i32 multiCBV)
{
	if(type == SHADER_SPECULAR && multiCBV)
		type = SHADER_SPECULAR_MCBV;
	SetInt(PDDI_SP_MULTI_CBV, multiCBV);
}


void
ShaderLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256];
	char shaderName[256];

	f->GetString(name);

	i32 version = f->GetI32();
	f->GetString(shaderName);
//printf("Shader %s %s\n", name, shaderName);

	// unused
	i32 HasTranslucency = f->GetI32();
	u32 VertexNeeds = f->GetU32();
	u32 VertexMask = f->GetU32();
	i32 count = f->GetI32();

	Shader *shader = new Shader(shaderName);

	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()){
		case Shader::SHADER_DEFINITION: {
			assert(0 && "no shader definition yet");
			break;
		}

		case Shader::TEXTURE_PARAM: {
			char texName[256];
			u32 param = f->GetU32();
			f->GetString(texName);
			Texture *tex = inventory->Find<Texture>(texName);
			if(tex)
				shader->SetTexture(param, tex);
			else
				printf("\tmissing texture <%s> in shader <%s>\n", texName, name);
			break;
		}

		case Shader::INT_PARAM: {
			u32 param = f->GetU32();
			i32 val = f->GetI32();
			switch(param) {
			case PDDI_SP_ISLIT:
				shader->SetLit(val != 0);
				break;
			case PDDI_SP_ALUM:
				shader->SetALUM(val != 0);
				break;
			case PDDI_SP_BLENDMODE:
				shader->SetBlendMode((pddiBlendMode)val);
				break;
			case PDDI_SP_ALPHATEST:
				shader->SetAlphaTest(val != 0);
				break;
			case PDDI_SP_MULTI_CBV:
				shader->SetMultiCBV(val);
				break;
			default:
				shader->SetInt(param, val);
			}
			break;
		}

		case Shader::FLOAT_PARAM: {
			u32 param = f->GetU32();
			float val = f->GetFloat();
			shader->SetFloat(param, val);
			break;
		}

		case Shader::COLOUR_PARAM: {
			u32 param = f->GetU32();
			u32 val = f->GetU32();
			if(param != PDDI_SP_AMBIENT)
				shader->SetColour(param, pddiColour(val));
			if(param == PDDI_SP_DIFFUSE)
				shader->SetColour(param, pddiColour(val));
			break;
		}

		case Shader::VECTOR_PARAM: {
			u32 param = f->GetU32();
			pddiVector v;
			f->GetData(&v, 3, sizeof(float));
			shader->SetVector(param, v);
			break;
		}

		case Shader::MATRIX_PARAM: {
			u32 param = f->GetU32();
			pddiMatrix m;
			f->GetData(&m, 16, sizeof(float));
			shader->SetMatrix(param, m);
			break;
		}
		}
		f->EndChunk();
	}

	shader->SetName(name);
	*pObject = shader;
	*pUID = shader->GetUID();
}

}
