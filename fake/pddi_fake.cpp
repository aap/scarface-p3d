#include "pddi.h"
#include "pddi_fake.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

namespace pure3d
{

class fakeDevice : public pddiDevice
{
public:
	virtual pddiTexture *NewTexture(void);
};

pddiTexture*
fakeDevice::NewTexture(void)
{
	return new fakeTexture;
}


// this is totaly stupid
void
fakeTexture::AddMipmap(u32 format, u32 sz, u8 *data)
{
	RawImage img = { format, sz, data };
	imgdata.push_back(img);
}





// very incomplete
class fakeShader : public pddiBaseShader
{
	static pddiShadeTextureTable fakeTextureTable[];
	static pddiShadeIntTable fakeIntTable[];
	static pddiShadeColourTable fakeColourTable[];
public:
	fakeShader(void);
	~fakeShader(void);
	virtual const char *GetType(void) { return "fake"; }

	virtual pddiShadeTextureTable	*GetTextureTable(void) { return fakeTextureTable; }
	virtual pddiShadeIntTable	*GetIntTable(void) { return fakeIntTable; }
	virtual pddiShadeFloatTable	*GetFloatTable(void) { return nil; }
	virtual pddiShadeColourTable	*GetColourTable(void) { return fakeColourTable; }
	virtual pddiShadeVectorTable	*GetVectorTable(void) { return nil; }
	virtual pddiShadeMatrixTable	*GetMatrixTable(void) { return nil; }

	pddiColour ambient;
	pddiColour diffuse;
	pddiColour specular;
	pddiColour emissive;

	pddiTexture *baseTex;
	int uvMode;

	int isLit;
	int twoSided;

	int blendMode;

	void SetAmbient(pddiColour col) { ambient = col; }
	void SetDiffuse(pddiColour col) { diffuse = col; }
	void SetSpecular(pddiColour col) { specular = col; }
	void SetEmisive(pddiColour col) { emissive = col; }

	void SetTexture(pddiTexture *tex) {
		if(tex) tex->AddRef();
		if(baseTex) baseTex->Release();
		baseTex = tex;
	}
	void SetUVMode(int mode) { uvMode = mode; }
	void EnableLighting(int enable) { isLit = enable; }
	void SetTwoSided(int enable) { twoSided = enable; }
	void SetBlendMode(int mode) { blendMode = mode; }
};

fakeShader::fakeShader(void)
 : ambient(pddiColour(255, 255, 255)),
   diffuse(pddiColour(0, 0, 0)),
   specular(pddiColour(0, 0, 0)),
   emissive(pddiColour(0, 0, 0))
{
	blendMode = 0;
	baseTex = nil;
	uvMode = 0;
	twoSided = 0;
	isLit = 0;
}

fakeShader::~fakeShader(void)
{
	if(baseTex) baseTex->Release();
}

// TODO: more stuff

pddiShadeTextureTable fakeShader::fakeTextureTable[] = {
	{ PDDI_SP_BASETEXT, SHADE_TEXTURE(&fakeShader::SetTexture)  },
	{ 0, nil }
};
pddiShadeIntTable fakeShader::fakeIntTable[] = {
	{ PDDI_SP_UVMODE, SHADE_INT(&fakeShader::SetUVMode)  },
	{ PDDI_SP_ISLIT, SHADE_INT(&fakeShader::EnableLighting)  },
	{ PDDI_SP_TWOSIDED, SHADE_INT(&fakeShader::SetTwoSided)  },
	{ PDDI_SP_BLENDMODE, SHADE_INT(&fakeShader::SetBlendMode)  },
	{ 0, nil }
};
pddiShadeColourTable fakeShader::fakeColourTable[] = {
	{ PDDI_SP_AMBIENT, SHADE_COLOUR(&fakeShader::SetAmbient)  },
	{ PDDI_SP_DIFFUSE, SHADE_COLOUR(&fakeShader::SetDiffuse)  },
	{ PDDI_SP_SPECULAR, SHADE_COLOUR(&fakeShader::SetSpecular)  },
	{ PDDI_SP_EMISSIVE, SHADE_COLOUR(&fakeShader::SetEmisive)  },
	{ 0, nil }
};


static pddiBaseShader *AllocDummy(const char *name, const char *aux) { return new fakeShader; }

void
fakeShaderSetup(void)
{
	pddiBaseShader::InstallShader("environment", AllocDummy, nil);
	pddiBaseShader::InstallShader("error", AllocDummy, nil);
	pddiBaseShader::InstallShader("simple", AllocDummy, nil);
	pddiBaseShader::InstallShader("specular", AllocDummy, nil);
	pddiBaseShader::InstallShader("pointsprite", AllocDummy, nil);
	pddiBaseShader::InstallShader("layered", AllocDummy, nil);
	pddiBaseShader::InstallShader("fbeffectsshader", AllocDummy, nil);
	pddiBaseShader::InstallShader("shadow", AllocDummy, nil);
	pddiBaseShader::InstallShader("decal", AllocDummy, nil);
	pddiBaseShader::InstallShader("foam", AllocDummy, nil);
	pddiBaseShader::InstallShader("nightlight", AllocDummy, nil);
	pddiBaseShader::InstallShader("untextured", AllocDummy, nil);
	pddiBaseShader::InstallShader("character", AllocDummy, nil);
	pddiBaseShader::InstallShader("cbvlit", AllocDummy, nil);
	pddiBaseShader::InstallShader("vehicle", AllocDummy, nil);
	pddiBaseShader::InstallShader("shadowdecal", AllocDummy, nil);
	pddiBaseShader::InstallShader("vertexfade", AllocDummy, nil);
}

void
initFake(void)
{
	device = new fakeDevice();
	fakeShaderSetup();
}


}
