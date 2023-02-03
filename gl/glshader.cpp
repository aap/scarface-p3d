#include "../pddi.h"
#include "pddi_gl.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

// HACK
bool IsShadervisible(const char *name);


namespace pure3d
{

glProgram *glShader::simpleProgram;
glProgram *glShader::discardProgram;

glShader::glShader(void)
{
	blendMode = PDDI_BLEND_NONE;
	baseTex = nil;
	uvMode = PDDI_UV_TILE;
	twoSided = 0;
	isLit = false;
	colours.ambient = pddiColour(255, 255, 255);
	alphaTest = false;
	alphaCompare = PDDI_COMPARE_GREATEREQUAL;
	alphaRef = 0.5f;

	if(simpleProgram == nil) {
#include "shaders/inc/shader.vert.inc"
#include "shaders/inc/shader.frag.inc"
		const char *vs[] = { shader_vert_src, nil };
		const char *fs[] = { shader_frag_src, nil };
		simpleProgram = new glProgram(vs, fs);
	}
	simpleProgram->AddRef();

	if(discardProgram == nil) {
#include "shaders/inc/shader.vert.inc"
#include "shaders/inc/discard.frag.inc"
		const char *vs[] = { shader_vert_src, nil };
		const char *fs[] = { discard_frag_src, nil };
		discardProgram = new glProgram(vs, fs);
	}
	discardProgram->AddRef();
}

glShader::~glShader(void)
{
	if(baseTex) baseTex->Release();
	if(simpleProgram->Release())
		simpleProgram = nil;
	if(discardProgram->Release())
		discardProgram = nil;
}

// TODO: more stuff

pddiShadeTextureTable glShader::glTextureTable[] = {
	{ PDDI_SP_BASETEX, SHADE_TEXTURE(&glShader::SetTexture)  },

	{ PDDI_SP_TOPTEX, SHADE_TEXTURE(&glShader::SetTextureDummy)  },
	{ PDDI_SP_REFLMAP, SHADE_TEXTURE(&glShader::SetTextureDummy)  },
	{ 0, nil }
};
pddiShadeIntTable glShader::glIntTable[] = {
	{ PDDI_SP_UVMODE, SHADE_INT(&glShader::SetUVMode)  },
	{ PDDI_SP_ISLIT, SHADE_INT(&glShader::EnableLighting)  },
	{ PDDI_SP_TWOSIDED, SHADE_INT(&glShader::SetTwoSided)  },
	{ PDDI_SP_BLENDMODE, SHADE_INT(&glShader::SetBlendMode)  },

	{ PDDI_SP_ALPHATEST, SHADE_INT(&glShader::SetAlphaTest)  },
	{ PDDI_SP_ALPHACOMPARE, SHADE_INT(&glShader::SetAlphaCompare)  },
	{ PDDI_SP_MULTI_CBV, SHADE_INT(&glShader::EnableMultiCBV)  },

	{ PDDI_SP_ALUM, SHADE_INT(&glShader::SetIntDummy)  },
	{ PDDI_SP_TWOLAYERCBV, SHADE_INT(&glShader::SetIntDummy)  },
	{ PDDI_SP_FILTER, SHADE_INT(&glShader::SetIntDummy)  },
	{ PDDI_SP_PLMD, SHADE_INT(&glShader::SetIntDummy)  },
	{ PDDI_SP_SHADEMODE, SHADE_INT(&glShader::SetIntDummy)  },
	{ PDDI_SP_TEXBLENDMODE, SHADE_INT(&glShader::SetIntDummy)  },
	{ PDDI_SP_ZTEST, SHADE_INT(&glShader::SetIntDummy)  },
	{ PDDI_SP_ZWRITE, SHADE_INT(&glShader::SetIntDummy)  },
	{ 0, nil }
};
pddiShadeFloatTable glShader::glFloatTable[] = {
	{ PDDI_SP_ALPHACOMPARE_THRESHOLD, SHADE_FLOAT(&glShader::SetAlphaRef)  },
	{ PDDI_SP_CBV_BLEND_VALUE, SHADE_FLOAT(&glShader::SetFloatDummy)  },
	{ PDDI_SP_TCI, SHADE_FLOAT(&glShader::SetFloatDummy)  },
	{ 0, nil }
};
pddiShadeColourTable glShader::glColourTable[] = {
	{ PDDI_SP_AMBIENT, SHADE_COLOUR(&glShader::SetAmbient)  },
	{ PDDI_SP_DIFFUSE, SHADE_COLOUR(&glShader::SetDiffuse)  },
	{ PDDI_SP_SPECULAR, SHADE_COLOUR(&glShader::SetSpecular)  },
	{ PDDI_SP_EMISSIVE, SHADE_COLOUR(&glShader::SetEmisive)  },

	{ PDDI_SP_COLB, SHADE_COLOUR(&glShader::SetColourDummy)  },
	{ PDDI_SP_ENVBLEND, SHADE_COLOUR(&glShader::SetColourDummy)  },
	{ PDDI_SP_TRNB, SHADE_COLOUR(&glShader::SetColourDummy)  },
	{ 0, nil }
};


void
glShader::SetPass(i32 pass)
{
	if(!IsShadervisible(GetType())) {
		discardProgram->Bind();
		return;
	}
	// TODO: this is shit
	{
		state->SetAmbientColour(pddiColour(51, 43, 27));
		state->SetLightColour(pddiColour(97, 95, 70));
		state->SetLightDir(Vector(0.5, -0.5, 0.5));
	}
	simpleProgram->Bind();
	state->SetTexture(baseTex);
	state->SetMaterial(isLit, twoSided, colours);
	state->SetAlphaBlend(blendMode);
	state->SetAlphaTest(alphaTest, alphaCompare, alphaRef);
}


// TODO: these are wrong. zwrite is handled at a higher level
void
glDecalShader::PreRender(void)
{
	glDepthMask(GL_FALSE);
}
void
glDecalShader::PostRender(void)
{
	glDepthMask(GL_TRUE);
}
void
glDecalShader::SetPass(i32 pass)
{
	if(!IsShadervisible(GetType())) {
		discardProgram->Bind();
		return;
	}
	glShader::SetPass(pass);
	if(blendMode == PDDI_BLEND_ALPHA)
//		state->SetAlphaTest(true, PDDI_COMPARE_NOTEQUAL, 0.0f);
		state->SetAlphaTest(true, PDDI_COMPARE_GREATER, 0.0f);
}

void
glShadowDecalShader::PreRender(void)
{
	glDepthMask(GL_FALSE);
}
void
glShadowDecalShader::PostRender(void)
{
	glDepthMask(GL_TRUE);
}
void
glShadowDecalShader::SetPass(i32 pass)
{
	if(!IsShadervisible(GetType())) {
		discardProgram->Bind();
		return;
	}
	glShader::SetPass(pass);
	if(blendMode == PDDI_BLEND_ALPHA)
//		state->SetAlphaTest(true, PDDI_COMPARE_NOTEQUAL, 0.0f);
		state->SetAlphaTest(true, PDDI_COMPARE_GREATER, 0.0f);
}


}