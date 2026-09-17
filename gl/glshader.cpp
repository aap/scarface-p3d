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
	isFogged = true;	// pddi fogs everything unless the shader says otherwise
	colours.ambient = pddiColour(255, 255, 255);
	alphaTest = false;
	alphaCompare = PDDI_COMPARE_GREATEREQUAL;
	alphaRef = 0.5f;
	fade = 0.0f;

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
	{ PDDI_SP_ISFOGGED, SHADE_INT(&glShader::EnableFog)  },
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
	{ PDDI_SP_FADE, SHADE_FLOAT(&glShader::SetFade)  },
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
	// the lights are context state now: renderer::LightManager fills the pddi slots once
	// a frame out of the game's own pure3d::LightGroups (renderer/lighting.cpp)
	simpleProgram->Bind();
	state->SetTexture(baseTex);
	state->SetMaterial(isLit, twoSided, colours);
	state->SetAlphaBlend(blendMode);
	// retail d3dSimpleShader::SetPass 0x65c043: while the instancing extension's
	// depth/alpha pass is running (g[0x830a18] && g[0x830a30] && g[0x830a31]), an
	// unlit alpha-blend shader is drawn alpha-TESTED at GREATEREQUAL 228/255 instead
	// of blended --- only the near-opaque core of a leaf goes into the depth buffer
	// (0x65c072 loads the ref from f[0x7f0908]; 0x65c059 is the !isLit test).
	if(pddiInstancedDepthPass && blendMode == PDDI_BLEND_ALPHA && !isLit)
		state->SetAlphaTest(true, PDDI_COMPARE_GREATEREQUAL, PDDI_INSTANCED_DEPTH_REF/255.0f);
	else
		state->SetAlphaTest(alphaTest, alphaCompare, alphaRef);
	state->SetVertexFade(0.0f, 0.0f, false);
	// the cross-fade of PDDI_SP_FADE. The shaders apply it to alpha AFTER the alpha
	// test, so it only bites where something is actually blended --- which is all the
	// viewer needs for the shadow decals and the other blended fading lists. Retail
	// goes further and switches a fading OPAQUE shader to alpha blending as well
	// (d3dSimpleShader::SetPass, notes/shaderstate.md); that is the LOD cross-fade for
	// solid geometry and is a job of its own.
	state->SetFade(fade);
	state->SetFogged(isFogged);
}

glVertexFadeShader::glVertexFadeShader(void)
{
	colours.ambient = pddiColour(255, 255, 255);
	colours.emissive = pddiColour(0, 0, 0);
}

void
glVertexFadeShader::SetPass(i32 pass)
{
	glShader::SetPass(pass);
	if(IsShadervisible(GetType())) {
		state->SetVertexFade(200.0f, 250.0f, true);
		// retail d3dVertexFadeShader::SetPass (0x709a30) hard-wires three things
		// regardless of the shader's own params: the blend mode (table_7ec848[1] =
		// PDDI_BLEND_ALPHA at 0x709bbc --- the data says BLMD 1 anyway) and, at
		// 0x709bc4..0x709c2c, D3DRS_ALPHATESTENABLE = 1, D3DRS_ALPHAFUNC =
		// table[GREATEREQUAL] = D3DCMP_GREATEREQUAL and D3DRS_ALPHAREF = 0x14.
		// That alpha test is what keeps the low-LOD hull out of the depth buffer:
		// the list it lands in (2) is drawn blended with z-write ON, and within the
		// fade band it is completely transparent, so without the test it writes
		// depth over the whole silhouette of the island and everything drawn after
		// it --- the eco-prop foliage above all --- disappears inside that hull.
		state->SetAlphaTest(true, PDDI_COMPARE_GREATEREQUAL, 20.0f/255.0f);
	}
}


// z-write is display-list state, not shader state: on PC the only per-draw writer of
// D3DRS_ZWRITEENABLE is d3dContext::SetZWrite (0x64b990, context vslot +0x118), and the
// only shader that calls it is d3dShadowDecalShader (below). d3dDecalShader::SetPass
// (0x709380) leaves it alone --- the decal lists are z-write-bracketed by
// Display_List::Render instead (lists 3, 17, 18, 4, 75, 5, 19, 20, 6). So no PreRender /
// PostRender here. Retail's alpha test for a blended decal is NOTEQUAL with ref 0
// (0x7093a0: D3DRS_ALPHATESTENABLE = 1, 0x7093c5: ALPHAFUNC = table[0x7ec920] =
// D3DCMP_NOTEQUAL, 0x7093e9: ALPHAREF = 0).
void
glDecalShader::SetPass(i32 pass)
{
	if(!IsShadervisible(GetType())) {
		discardProgram->Bind();
		return;
	}
	glShader::SetPass(pass);
	if(blendMode == PDDI_BLEND_ALPHA)
		state->SetAlphaTest(true, PDDI_COMPARE_GREATER, 0.0f);
}

// The shadow decal shader, on the other hand, really does own its z-write in retail:
// d3dShadowDecalShader::SetPass tail-jumps ctx->SetZWrite(false) (0x709342) and
// ::PostRender (0x7090f0) calls ctx->SetZWrite(true). Go through the context so its
// cached flag stays in step with the GL state.
void
glShadowDecalShader::PostRender(void)
{
	context->SetZWrite(true);
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
		state->SetAlphaTest(true, PDDI_COMPARE_GREATER, 0.0f);
	// retail: the decals go into a cleared alpha mask by alpha blending, which squares
	// the coverage, and the frame is multiplied by the mask (re/notes/shadows.md)
	state->SetShadowDecal(true);
	context->SetZWrite(false);
}


}