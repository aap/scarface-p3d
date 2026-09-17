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
	zWrite = true;
	zTest = true;

	state = new glState;
}

void
glContext::Begin(void)
{
	glPolygonMode(GL_FRONT_AND_BACK, pddiDebug.wireframe ? GL_LINE : GL_FILL);
	worldSP = 0;
	worldMatrix[worldSP].Identity();
	SetZWrite(true);
	SetZTest(true);
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
	u_lightColour = uniformRegistry.Register("u_lightColour", UNIFORM_VEC4, GL_MAX_LIGHTS);
	u_lightDir = uniformRegistry.Register("u_lightDir", UNIFORM_VEC4, GL_MAX_LIGHTS);
	u_lightPos = uniformRegistry.Register("u_lightPos", UNIFORM_VEC4, GL_MAX_LIGHTS);
	u_lightRange = uniformRegistry.Register("u_lightRange", UNIFORM_VEC4, GL_MAX_LIGHTS);
	u_debug = uniformRegistry.Register("u_debug", UNIFORM_VEC4);
	u_vertexFade = uniformRegistry.Register("u_vertexFade", UNIFORM_VEC4);
	u_fade = uniformRegistry.Register("u_fade", UNIFORM_VEC4);
	u_lit = uniformRegistry.Register("u_lit", UNIFORM_VEC4);
	u_fogColour = uniformRegistry.Register("u_fogColour", UNIFORM_VEC4);
	u_fogRange = uniformRegistry.Register("u_fogRange", UNIFORM_VEC4);
	isLit = true;
	isFogged = true;
	vertexFade = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	fade = 0.0f;
	whiteTex = 0;

	// pddiBaseContext::ResetState (SHR basecontext.cpp): off, white, 0..1000
	fogEnabled = false;
	fogColour = pddiColour(255, 255, 255);
	fogStart = 0.0f;
	fogEnd = 1000.0f;
	fogClamp = 255;

	// Until renderer::LightManager applies the game's own lights this is what the world
	// is lit with: aap's eyeballed stand-in for the retail noon sun, which turned out to
	// be very close to the LITE_Miami* animations at frame 120 (ambient 63,52,31 and
	// sun 99,97,72 from (-0.473,-0.743,0.473)).
	ambientColour = pddiColour(51, 43, 27);
	lights[0].SetDirectionalLight(pddiColour(97, 95, 70), Vector(0.5f, -0.5f, 0.5f));
	lights[0].enabled = true;
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
	Vector4 lcol[GL_MAX_LIGHTS], ldir[GL_MAX_LIGHTS], lpos[GL_MAX_LIGHTS], lrange[GL_MAX_LIGHTS];
	for(int i = 0; i < GL_MAX_LIGHTS; i++) {
		pddiLightDesc *l = &lights[i];
		Vector4 c = convCol(l->colour);
		lcol[i] = Vector4(c.x, c.y, c.z, l->enabled ? 1.0f : 0.0f);
		ldir[i] = Vector4(l->direction.x, l->direction.y, l->direction.z,
		                  l->type == PDDI_LIGHT_POINT ? 1.0f : 0.0f);
		lpos[i] = Vector4(l->position.x, l->position.y, l->position.z, 0.0f);
		lrange[i] = Vector4(l->innerRange, l->outerRange, 0.0f, 0.0f);
	}
	uniformRegistry.SetUniform(u_lightColour, lcol);
	uniformRegistry.SetUniform(u_lightDir, ldir);
	uniformRegistry.SetUniform(u_lightPos, lpos);
	uniformRegistry.SetUniform(u_lightRange, lrange);
	Vector4 dbg(pddiDebug.noLighting ? 1.0f : 0.0f, pddiDebug.noVertexColours ? 1.0f : 0.0f, pddiDebug.noTextures ? 1.0f : 0.0f, 0.0f);
	uniformRegistry.SetUniform(u_debug, &dbg);
	uniformRegistry.SetUniform(u_vertexFade, &vertexFade);
	// the per-primitive cross-fade as the shaders want it: an alpha multiplier, so
	// 1 = opaque. It is applied after the alpha test, which is what keeps it from
	// changing anything for the opaque and alpha-tested shaders.
	Vector4 fadev(1.0f - fade, 0.0f, 0.0f, 0.0f);
	uniformRegistry.SetUniform(u_fade, &fadev);
	Vector4 lit(isLit ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
	uniformRegistry.SetUniform(u_lit, &lit);
	// linear distance fog, exactly the two states d3dContext::SetFog/EnableFog write:
	// the colour, and the start/end of the linear ramp. w of u_fogColour is the enable.
	Vector4 fogc = convCol(fogColour);
	fogc.w = fogEnabled && isFogged ? 1.0f : 0.0f;
	uniformRegistry.SetUniform(u_fogColour, &fogc);
	// z: the FogClamp as the shaders see it (c49.w), w: whether to apply it
	Vector4 fogr(fogStart, fogEnd, fogClamp/255.0f, pddiDebug.fogClamp ? 1.0f : 0.0f);
	uniformRegistry.SetUniform(u_fogRange, &fogr);
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
	// pddi shades an unlit surface as texture*vertexColour, with no light at all ---
	// which is what the sky boxes need: their vertex colours ARE the sky
	this->isLit = isLit;
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
	if(tex) {
		tex->Bind(0);
		return;
	}
	// an unbound sampler reads black; an untextured shader wants white * colour
	if(whiteTex == 0) {
		u8 px[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &whiteTex);
		glBindTexture(GL_TEXTURE_2D, whiteTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	glBindTexture(GL_TEXTURE_2D, whiteTex);
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
