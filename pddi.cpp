#include "pddi.h"
#include "texture.h"

#include <assert.h>
#include <string.h>

namespace pure3d
{

pddiDevice *device;
pddiContext *context;

static pddiBaseShader *lastShader;

void
pddiBaseShader::SetMaterial(i32 pass)
{
	// TODO: this is wrong
	if(lastShader)
		lastShader->PostRender();
	PreRender();
	SetPass(pass);
	lastShader = this;
}

void
pddiBaseShader::ClearCurrentShader(void)
{
	if(lastShader)
		lastShader->PostRender();
	lastShader = nil;
}

#define DISPATCH(type) \
	pddiShade##type##Table *table = Get##type##Table(); \
	if(table) { \
		while(table->param != 0) { \
			if(table->param == param) { \
				(this->*(table->func))(val); \
				return true; \
			} \
			table++; \
		} \
	} \
	printf("Couldn't find " #type " property %.4s\n", (char*)&param); \
	return false;

bool
pddiBaseShader::SetTexture(u32 param, pddiTexture *val)
{
	DISPATCH(Texture)
}
bool
pddiBaseShader::SetInt(u32 param, int val)
{
	DISPATCH(Int);
}
bool
pddiBaseShader::SetFloat(u32 param, float val)
{
	DISPATCH(Float);
}
bool
pddiBaseShader::SetColour(u32 param, pddiColour val)
{
	DISPATCH(Colour);
}
bool
pddiBaseShader::SetVector(u32 param, const pddiVector &val)
{
	DISPATCH(Vector);
}
bool
pddiBaseShader::SetMatrix(u32 param, const pddiMatrix &val)
{
	DISPATCH(Matrix);
}






struct pddiShadeAllocTable
{
	char name[64];
	pddiShadeAllocFunc func;
	const char *aux;
};

static int shaderCount;
static pddiShadeAllocTable shaders[40];

void
pddiBaseShader::InstallShader(const char *name, pddiShadeAllocFunc func, const char *aux)
{
	strcpy(shaders[shaderCount].name, name);
	shaders[shaderCount].func = func;
	shaders[shaderCount].aux = aux;
	shaderCount++;
}

pddiBaseShader*
pddiBaseShader::AllocateShader(const char *name, const char *aux)
{
	for(int i = 0; i < shaderCount; i++)
		if(strcmp(shaders[i].name, name) == 0)
			return shaders[i].func(name, shaders[i].aux ? shaders[i].aux : aux);
	assert(shaderCount > 0);
	assert(shaders[0].func);
	return shaders[0].func(name, aux);
}

}
