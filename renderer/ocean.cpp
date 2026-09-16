#include "ocean.h"
#include "lighting.h"
#include "view.h"
#include "../shader.h"
#include "../texture.h"
#include "../pddi.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

namespace renderer
{

using namespace pure3d;

OceanRenderable *gOcean = nil;
bool g_oceanEnabled = true;


// ---------------------------------------------------------------- OceanPrimitive

// retail: renderer::OceanPrimitive::OceanPrimitive 0x00470820. The layer is what puts
// the ocean into Display_List list 60 (the layer->list table, case 27); that list is
// walked in group 12 of Render(), with z-write on and fog left ON --- the only lists
// retail forces fog off for are the sky (46/47), the env/reflection pass (9/10), the
// night lights (11) and the camera-locked list 76 (re/notes/fog.md §4).
OceanPrimitive::OceanPrimitive(Ocean *o)
 : ocean(nil)
{
	Assign(ocean, o);
	SetLayer(27);
	UpdateBounds();
}

OceanPrimitive::~OceanPrimitive(void)
{
	Release(ocean);
}

Shader*
OceanPrimitive::GetShader(void) const
{
	return ocean ? ocean->shader : nil;
}

void
OceanPrimitive::SetShader(Shader *sh)
{
	if(ocean)
		ocean->SetShader(sh);
}

bool
OceanPrimitive::IsALUM(void)
{
	Shader *sh = GetShader();
	return sh && sh->GetALUM();
}

// retail: OceanPrimitive::CalcBounds 0x004707c0 --- two lines:
//	View_GetCamera()->GetPosition(&this->sphere.centre);
//	this->sphere.radius = 100000.0f;
// i.e. the bounding sphere is re-centred on the camera every frame and is big enough
// that Display_List::IsNodeVisible can never cull it.
void
OceanPrimitive::UpdateBounds(void)
{
	if(ocean == nil)
		return;
	Vector p;
	View_GetCullingCamera()->GetPosition(&p);
	ocean->cameraPosition = p;
	sphere = ocean->GetBounds();
	box = Box3D(sphere.centre - Vector(sphere.radius, sphere.radius, sphere.radius),
	            sphere.centre + Vector(sphere.radius, sphere.radius, sphere.radius));
}

// retail: OceanPrimitive::Display 0x004707a0 --- `if(+0x38) jmp 0x689aa0`, i.e. the
// whole draw is the pure3d ocean object's.
void
OceanPrimitive::Display(void)
{
	if(ocean == nil)
		return;
	// Both ocean shaders are unlit, so the pddi light slots never reach them; hand the
	// frame's ambient and its directional lights to the grid builder, which does the
	// same sum in the vertex colour (ocean.cpp). The zone group has two of them, the
	// sun and a fill light pointing the other way, and they are in the list in the
	// order LightManager collected them --- taking only the first would pick the fill.
	// The local point lights are left out: they never reach the water in practice.
	LightManager *lm = gLightManager;
	if(lm && lm->enabled) {
		ocean->lightAmbient = Vector(lm->ambient.R()/255.0f, lm->ambient.G()/255.0f,
		                             lm->ambient.B()/255.0f);
		i32 n = 0;
		for(u32 i = 0; i < lm->active.size() && n < Ocean::MAX_LIGHTS; i++) {
			Light *l = lm->active[i].light;
			if(l->type != Light::DIRECTIONAL)
				continue;
			pddiColour c = lm->active[i].colour;
			ocean->lightColour[n] = Vector(c.R()/255.0f, c.G()/255.0f, c.B()/255.0f);
			// the lights are native and so is the grid, so no x flip here
			ocean->lightDirection[n] = l->direction;
			n++;
		}
		ocean->numLights = n;
	}
	ocean->Display();
}


// ---------------------------------------------------------------- OceanContainer

// retail: renderer::OceanContainer::OceanContainer 0x00470a40 --- one element and
// sortKey 0.5 (observed, re/notes/renderable_classes.md §2).
OceanContainer::OceanContainer(Ocean *ocean)
 : DrawableContainer(1)
{
	sortKey = 0.5f;
	OceanPrimitive *prim = new OceanPrimitive(ocean);
	GetElement(0)->SetPrimitive(prim);
	SetFlags();
	CalcBounds();
}

void
OceanContainer::CalcBounds(void)
{
	OceanPrimitive *prim = GetPrimitive();
	if(prim == nil)
		return;
	prim->UpdateBounds();
	box = prim->box;
	sphere = prim->sphere;
}


// ---------------------------------------------------------------- OceanRenderable

// retail: renderer::OceanRenderable::OceanRenderable 0x00470ad0
//	Renderable::Renderable(1); typeMask = 0x40;
//	container = new OceanContainer(a, b, c); SetElement(container, 0, false);
//	flags80 &= ~3;              // no distance test, no fade
OceanRenderable::OceanRenderable(Ocean *ocean)
 : Renderable(1),
   container(nil)
{
	typeMask = TYPE_OCEAN;
	container = new OceanContainer(ocean);
	container->AddRef();
	SetElement(container, 0, false);
	doDistanceTest = false;
	doFade = false;
	SetName("oceanrenderable");
}

OceanRenderable::~OceanRenderable(void)
{
	Release(container);
}

// retail: renderer::OceanRenderable::Update 0x00470be0. Retail advances the wave
// simulation here; we advance the phase clock and re-centre the bounds on the camera,
// which is what OceanPrimitive::CalcBounds does in retail.
void
OceanRenderable::Update(TimeInfo *t)
{
	Ocean *o = GetOcean();
	if(o == nil)
		return;
	o->Tick(t->dt*0.001f);
	container->GetPrimitive()->UpdateBounds();
}

void
OceanRenderable::Display(void)
{
	if(g_oceanEnabled)
		Renderable::Display();
	else
		Hide();
}


// ---------------------------------------------------------------- the factory

// retail: renderer::OceanRenderable_CreateInstance 0x00466940. The game calls it from
// OceanObject::OnInit (leak: ocean/oceanobject.cpp) with the three texture names of the
// OceanTemplate and then pushes the rest of the template in through the setters.
OceanRenderable *
OceanRenderable_CreateInstance(const char *reflectionTextureName,
                               const char *detailTextureName,
                               const char *foamTextureName,
                               LoadInventory *inventory)
{
	if(inventory == nil)
		return nil;
	Ocean *ocean = new Ocean;
	ocean->AddRef();
	ocean->SetName("ocean");

	// The detail pass. Common.p3d has `ocean_text` (shader class "simple",
	// TEX = water_01.bmp, ALUM 1, no LIT and no BLMD, i.e. unlit and opaque) and
	// `ocean_foam`. We blend the detail texture over the reflection pass at
	// DetailOpacity, which needs an alpha blend the file does not ask for --- nothing
	// else in the game uses `ocean_text`, and the ocean owns it, exactly as retail's
	// ocean owns its own d3d effects.
	Shader *sh = inventory->Find<Shader>("ocean_text");
	if(sh == nil)
		printf("ocean: no shader <ocean_text>\n");
	else
		sh->SetBlendMode(PDDI_BLEND_ALPHA);
	ocean->SetShader(sh);

	// ...and the untextured first pass, which is retail's reflection pass without a
	// reflection (there is no such shader in any p3d: the ocean's own passes are d3d
	// effects, `ocean_pass1` / `ocean_pass2`)
	Shader *base = new Shader("simple");
	base->SetName("ocean_base");
	base->SetBlendMode(PDDI_BLEND_NONE);
	ocean->SetBaseShader(base);

	Texture *reflection = reflectionTextureName ? inventory->Find<Texture>(reflectionTextureName) : nil;
	Texture *detail = detailTextureName ? inventory->Find<Texture>(detailTextureName) : nil;
	Texture *foam = foamTextureName ? inventory->Find<Texture>(foamTextureName) : nil;
	if(detailTextureName && detail == nil)
		printf("ocean: no detail texture <%s>\n", detailTextureName);
	ocean->SetTextures(reflection, detail, foam);
	// the shader already carries water_01.bmp; a template that names a different
	// detail texture (OceanTemplateNightDefault, OceanTemplateRiver) overrides it
	if(detail && sh)
		sh->SetTexture(PDDI_SP_BASETEX, detail);

	OceanRenderable *r = new OceanRenderable(ocean);
	ocean->Release();
	return r;
}

}
