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
	alphaBits = -1;
	inStaticShadows = false;
	quadProgram = nil;
	quadVBO = 0;

	state = new glState;
}


// ---------------------------------------------------------------------------
// the static shadow mask --- retail's pddiExtStaticShadowGen (pddi extension 0x108,
// re/notes/shadows.md §2). Begin clears an alpha mask to 0 and lets the decals blend
// their coverage into it with colour write = alpha only; End multiplies the frame by
// the mask once. Retail keeps the mask in a screen-sized A8R8G8B8 render target of its
// own; we keep it in the frame buffer's own alpha channel, which the D3D9 code was
// plainly written around (notes/displaylist.md step 3 fills that channel through an
// alpha-only clear and every later pass protects it with SetColourWrite(1,1,1,0)) and
// which costs no render target. Two things follow from doing it at all:
//   * overlapping decals stop darkening twice --- the mask saturates instead of the
//     frame being multiplied once per decal;
//   * a decal of coverage c darkens by c*c, because the mask is accumulated with the
//     decal's own PDDI_BLEND_ALPHA: mask = c*c + mask*(1-c).
// The cap `strength` is retail's wash colour 0xff808080 (half brightness, §2.1) scaled
// into the mask by the first of the two full-screen quads.
static const char *quadVertSrc =
"#version 120\n"
"attribute vec4 in_pos;\n"
"void main(void) { gl_Position = in_pos; }\n";
static const char *quadFragSrc =
"#version 120\n"
"uniform vec4 u_quadColour;\n"
"void main(void) { gl_FragColor = u_quadColour; }\n";

void
glContext::DrawFullscreenQuad(const Vector4 &col)
{
	static const float verts[] = {
		-1.0f, -1.0f, 0.0f, 1.0f,
		 1.0f, -1.0f, 0.0f, 1.0f,
		-1.0f,  1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f, 0.0f, 1.0f,
	};
	if(quadProgram == nil) {
		const char *vs[] = { quadVertSrc, nil };
		const char *fs[] = { quadFragSrc, nil };
		quadProgram = new glProgram(vs, fs);
		glGenBuffers(1, &quadVBO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	}
	quadProgram->Bind();
	state->SetQuadColour(col);
	glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
	glEnableVertexAttribArray(ATTRIB_POS);
	glVertexAttribPointer(ATTRIB_POS, 4, GL_FLOAT, GL_FALSE, 4*4, (void*)0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(ATTRIB_POS);
}

bool
glContext::HasStaticShadowMask(void)
{
	if(alphaBits < 0) {
		// the default frame buffer's alpha channel is the mask, so without one
		// there is nothing to accumulate into (and GL_DST_ALPHA would read 1 and
		// multiply the whole frame to black)
		GLint bits = 0;
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT,
			GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &bits);
		if(glGetError() != GL_NO_ERROR || bits == 0) {
			bits = 0;
			glGetIntegerv(GL_ALPHA_BITS, &bits);	// GL2 / compatibility
			glGetError();
		}
		alphaBits = bits;
		if(alphaBits == 0)
			fprintf(stderr, "no destination alpha: the static shadow decals "
				"are painted on the ground instead of masked\n");
	}
	return alphaBits > 0;
}

void
glContext::BeginStaticShadows(void)
{
	if(!HasStaticShadowMask())
		return;
	inStaticShadows = true;
	state->SetShadowDecalMask(true);
	// retail: SetZWrite(false), SetColourWrite(0,0,0,1), Clear(TARGET, 0x00000000).
	// glClear honours the colour mask, so this clears the alpha channel only.
	SetColourWrite(false, false, false, true);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void
glContext::EndStaticShadows(float strength)
{
	if(!inStaticShadows)
		return;
	inStaticShadows = false;
	state->SetShadowDecalMask(false);

	bool zt = GetZTest();
	SetZTest(false);
	SetZWrite(false);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);

	// mask *= strength (the 0xff808080 wash as a cap on the darkening)
	if(strength < 1.0f) {
		SetColourWrite(false, false, false, true);
		glBlendFunc(GL_ZERO, GL_SRC_ALPHA);
		DrawFullscreenQuad(Vector4(0.0f, 0.0f, 0.0f,
			strength < 0.0f ? 0.0f : strength));
	}
	// frame *= 1 - mask. Retail's own composite is SRCBLEND ZERO / DESTBLEND SRCALPHA
	// with the mask in a texture; with the mask in the frame buffer the destination
	// alpha is the blend factor, and the complement is the polarity the pass needs
	// (the mask holds coverage, cleared to 0 --- notes/shadows.md §2.3).
	SetColourWrite(true, true, true, false);
	glBlendFunc(GL_ZERO, GL_ONE_MINUS_DST_ALPHA);
	DrawFullscreenQuad(Vector4(0.0f, 0.0f, 0.0f, 0.0f));

	// leave the frame buffer's alpha the way the rest of the frame expects it (opaque:
	// the screenshot path and any blend that reads destination alpha)
	SetColourWrite(false, false, false, true);
	glDisable(GL_BLEND);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	SetColourWrite(true, true, true, true);
	SetZTest(zt);
	SetZWrite(true);
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
	u_shadowDecal = uniformRegistry.Register("u_shadowDecal", UNIFORM_VEC4);
	shadowDecal = Vector4(0.0f, 1.0f, 1.0f, 0.0f);
	shadowDecalMask = false;
	u_quadColour = uniformRegistry.Register("u_quadColour", UNIFORM_VEC4);
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
	uniformRegistry.SetUniform(u_shadowDecal, &shadowDecal);
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

// retail: d3dState::SetUVMode (0x65afb0) --- the shader's UVMD reaches
// D3DSAMP_ADDRESSU/V/W through table_7ec8fc = { D3DTADDRESS_WRAP, D3DTADDRESS_CLAMP },
// so UVMD 0 tiles and UVMD 1 clamps. pddi addressing is per sampler; in GL it is per
// texture object, which only differs when one texture is used by two shaders with
// different UVMD (it is set at every SetPass, so the last one wins for that draw).
void
glState::SetUVMode(pddiUVMode mode)
{
	GLint wrap = mode == PDDI_UV_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

void
glState::SetQuadColour(const Vector4 &col)
{
	uniformRegistry.SetUniform(u_quadColour, &col);
	uniformRegistry.Flush();
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
