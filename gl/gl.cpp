#include "../pddi.h"
#include "pddi_gl.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

namespace pure3d
{

using namespace math;

static Vector4 convCol(pddiColour c) { return Vector4(c.R()/255.0f, c.G()/255.0f, c.B()/255.0f, c.A()/255.0f); }


class glDevice : public pddiDevice
{
public:
	virtual pddiTexture *NewTexture(void);
	virtual pddiPrimBuffer *NewPrimBuffer(pddiPrimType primType, u32 vertexFormat, u32 nVertices, u32 nIndices);
	virtual pddiShader *NewShader(const char *name, const char *def = nil);
};

pddiTexture*
glDevice::NewTexture(void)
{
	return new glTexture;
}

pddiPrimBuffer*
glDevice::NewPrimBuffer(pddiPrimType primType, u32 vertexFormat, u32 nVertices, u32 nIndices)
{
	return new glPrimBuffer(primType, vertexFormat, nVertices, nIndices);
}

pddiShader*
glDevice::NewShader(const char *name, const char *def)
{
	return pddiBaseShader::AllocateShader(name, nil);
}



glContext::glContext(void)
{
	worldSP = 0;
	worldMatrix[worldSP].Identity();

	state = new glState;
}

void
glContext::Begin(void)
{
	worldSP = 0;
	worldMatrix[worldSP].Identity();
}

void
glContext::End(void)
{
	pddiBaseShader::ClearCurrentShader();
}


void
glContext::SetWorldMatrix(const Matrix &mat)
{
	worldMatrix[worldSP] = mat;
}

void
glContext::MultWorldMatrix(const Matrix &mat)
{
	worldMatrix[worldSP] = Multiply(mat, worldMatrix[worldSP]);
}

void
glContext::PushWorldMatrix(void)
{
	worldSP++;
	assert(worldSP < (int)nelem(worldMatrix));
	worldMatrix[worldSP] = worldMatrix[worldSP-1];
}

void
glContext::PopWorldMatrix(void)
{
	worldSP--;
	assert(worldSP >= 0);
}

void
glContext::SetViewMatrix(const Matrix &mat)
{
	viewMatrix = mat;
}

void
glContext::SetProjectionMatrix(const Matrix &mat)
{
	projMatrix = mat;
}

void
glContext::PushDebugName(const char *name)
{
	glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
}

void
glContext::PopDebugName(void)
{
	glPopDebugGroup();
}



void
glContext::DrawPrimBuffer(pddiShader *mat, pddiPrimBuffer *buf)
{
	pddiBaseShader *material = (pddiBaseShader*)mat;
	glPrimBuffer *buffer = (glPrimBuffer*)buf;

	material->SetMaterial(0);
	buffer->Display();
}


glState *state;

glState::glState(void)
{
	u_world = uniformRegistry.Register("u_world", UNIFORM_MAT4);
	u_view = uniformRegistry.Register("u_view", UNIFORM_MAT4);
	u_proj = uniformRegistry.Register("u_proj", UNIFORM_MAT4);
	u_alphaTest = uniformRegistry.Register("u_alphaTest", UNIFORM_VEC4);
	u_matAmbient = uniformRegistry.Register("u_matAmbient", UNIFORM_VEC4);
	u_matDiffuse = uniformRegistry.Register("u_matDiffuse", UNIFORM_VEC4);
	u_matSpecular = uniformRegistry.Register("u_matSpecular", UNIFORM_VEC4);
	u_matEmissive = uniformRegistry.Register("u_matEmissive", UNIFORM_VEC4);
	u_ambientColour = uniformRegistry.Register("u_ambientColour", UNIFORM_VEC4);
	u_lightDir1 = uniformRegistry.Register("u_lightDir1", UNIFORM_VEC4);
	u_lightColour1 = uniformRegistry.Register("u_lightColour1", UNIFORM_VEC4);
}

void
glState::Flush(void)
{
	Vector4 col;
	uniformRegistry.SetUniform(u_world, &context->GetWorldMatrix());
	uniformRegistry.SetUniform(u_view, &context->GetViewMatrix());
	uniformRegistry.SetUniform(u_proj, &context->GetProjectionMatrix());
	col = convCol(ambientColour);
	uniformRegistry.SetUniform(u_ambientColour, &col);
	uniformRegistry.SetUniform(u_lightDir1, &lightDir1);
	col = convCol(lightColour1);
	uniformRegistry.SetUniform(u_lightColour1, &col);
	uniformRegistry.Flush();
}

void
glState::SetAlphaTest(bool alphaTest, pddiCompareMode alphaCompare, float alphaRef)
{
	float alpha[4] = { -1000.0f, 1000.0f, 0.0f, 0.0f };
	if(alphaTest) {
		// discard if a < alpha[0] || a >= alpha[1]
	//	float epsilon = 0.001f;
		switch(alphaCompare) {
		case PDDI_COMPARE_GREATER:
		case PDDI_COMPARE_GREATEREQUAL:
			alpha[0] = alphaRef;
			break;
		case PDDI_COMPARE_LESS:
		case PDDI_COMPARE_LESSEQUAL:
			alpha[1] = alphaRef;
			break;
		default:
			assert(0 && "unsupported alpha test mode");
		}
	}
	uniformRegistry.SetUniform(u_alphaTest, alpha);
}

void
glState::SetAlphaBlend(pddiBlendMode mode)
{
	if(mode == PDDI_BLEND_NONE) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
		switch(mode) {
		case PDDI_BLEND_ALPHA:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case PDDI_BLEND_ADD:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			break;
		default: break;
		}
	}
}

void
glState::SetMaterial(bool isLit, bool twoSided, const MaterialColours &colours)
{
	Vector4 colv;
	colv = convCol(colours.ambient);
	uniformRegistry.SetUniform(u_matAmbient, &colv);
	colv = convCol(colours.diffuse);
	uniformRegistry.SetUniform(u_matDiffuse, &colv);
	colv = convCol(colours.specular);
	uniformRegistry.SetUniform(u_matSpecular, &colv);
	colv = convCol(colours.emissive);
	uniformRegistry.SetUniform(u_matEmissive, &colv);
}

void
glState::SetTexture(pddiTexture *tex)
{
	if(tex)
		tex->Bind(0);
	else
		glBindTexture(GL_TEXTURE_2D, 0);
}




//static pddiBaseShader *AllocDummy(const char *name, const char *aux) { return new glShader; }
static pddiBaseShader *AllocSimple(const char *name, const char *aux) { return new glSimpleShader; }
static pddiBaseShader *AllocRefl(const char *name, const char *aux) { return new glReflShader; }
static pddiBaseShader *AllocError(const char *name, const char *aux) { return new glErrorShader; }
static pddiBaseShader *AllocSpecular(const char *name, const char *aux) { return new glSpecularShader; }
static pddiBaseShader *AllocPointSprite(const char *name, const char *aux) { return new glPointSpriteShader; }
static pddiBaseShader *AllocLayered(const char *name, const char *aux) { return new glLayeredShader; }
static pddiBaseShader *AllocFBEffects(const char *name, const char *aux) { return new glFBEffectsShader; }
static pddiBaseShader *AllocShadow(const char *name, const char *aux) { return new glShadowShader; }
static pddiBaseShader *AllocDecal(const char *name, const char *aux) { return new glDecalShader; }
static pddiBaseShader *AllocCharacter(const char *name, const char *aux) { return new glCharacterShader; }
static pddiBaseShader *AllocSimpleCBVLit(const char *name, const char *aux) { return new glSimpleCBVLitShader; }
static pddiBaseShader *AllocVehicle(const char *name, const char *aux) { return new glVehicleShader; }
static pddiBaseShader *AllocShadowDecal(const char *name, const char *aux) { return new glShadowDecalShader; }
static pddiBaseShader *AllocVertexFade(const char *name, const char *aux) { return new glVertexFadeShader; }

void
glShaderSetup(void)
{
	pddiBaseShader::InstallShader("environment", AllocRefl, nil);
	pddiBaseShader::InstallShader("error", AllocError, nil);
	pddiBaseShader::InstallShader("simple", AllocSimple, nil);
	pddiBaseShader::InstallShader("specular", AllocSpecular, nil);
	pddiBaseShader::InstallShader("pointsprite", AllocPointSprite, nil);
	pddiBaseShader::InstallShader("layered", AllocLayered, nil);
	pddiBaseShader::InstallShader("fbeffectsshader", AllocFBEffects, nil);
	pddiBaseShader::InstallShader("shadow", AllocShadow, nil);
	pddiBaseShader::InstallShader("decal", AllocDecal, nil);
	pddiBaseShader::InstallShader("foam", AllocSimple, nil);
	pddiBaseShader::InstallShader("nightlight", AllocSimple, nil);
	pddiBaseShader::InstallShader("untextured", AllocSimple, nil);
	pddiBaseShader::InstallShader("character", AllocCharacter, nil);
	pddiBaseShader::InstallShader("cbvlit", AllocSimpleCBVLit, nil);
	pddiBaseShader::InstallShader("vehicle", AllocVehicle, nil);
	pddiBaseShader::InstallShader("shadowdecal", AllocShadowDecal, nil);
	pddiBaseShader::InstallShader("vertexfade", AllocVertexFade, nil);
}

void
InitDevice(void)
{
	device = new glDevice();
	context = new glContext();
	glShaderSetup();
}


}
