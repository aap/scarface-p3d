#include "shadow.h"
#include "../drawable.h"

#include <stdio.h>
#include <stdlib.h>

namespace renderer
{

using namespace pure3d;

// retail: g[0x007c0b46]
bool g_buildingShadowsEnabled = true;

// retail: renderer::ShadowRenderable::ShadowRenderable 0x00474cf0 --- Renderable(0),
// typeMask 0x100, flags80 &= ~2 (a shadow never fades)
ShadowRenderable::ShadowRenderable(void)
 : Renderable(0)
{
	typeMask = TYPE_SHADOW;
	doFade = false;
	isBuildingShadow = false;
	flagB = false;
	f0 = 0.0f;
	drawDist = 0.0f;
	pose = nil;
	composite = nil;
}

ShadowRenderable::~ShadowRenderable(void)
{
	Release(pose);
	Release(composite);
}

// retail: renderer::ShadowRenderable::Update 0x00474cd0 --- with the global switch on
// (which it is) this does nothing at all; 0x473cc0, the other branch, is the same helper
// StatePropRenderable::Update uses and is not reversed.
void
ShadowRenderable::Update(TimeInfo *t)
{
}

// retail: renderer::ShadowRenderable::Display 0x004751c0
void
ShadowRenderable::Display(void)
{
	// no pose means no composite: draw nothing
	if(pose == nil)
		return;
	if(isBuildingShadow) {
		if(g_buildingShadowsEnabled)
			Renderable::Display();
		else
			Hide();
		isMatrixDirty = false;
		return;
	}
	// The other 1.3 KB of the retail function is the blob shadow under a car or an
	// NPC: it measures the camera, runs the frustum and occluder tests itself and
	// projects a quad of +0xb0 x +0xb4 metres onto the ground. The viewer has no
	// characters and no vehicles, so nothing can reach it.
	Hide();
}

ShadowLoader::ShadowLoader(void)
 : SimpleChunkHandler(Renderable::SHADOW_LOADER)
{
}

// retail: renderer::ShadowLoader::LoadObject 0x00475930 (re/notes/shadows.md §3.1)
void
ShadowLoader::LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];
	char compName[256];

	f->GetString(name);
	ShadowRenderable *shadow = new ShadowRenderable;
	shadow->SetName(name);

	f->GetString(compName);
	CompositeDrawable *composite = inventory->Find<CompositeDrawable>(compName);

	shadow->isBuildingShadow = f->GetU32() != 0;
	shadow->flagB = f->GetU32() != 0;
	shadow->f0 = f->GetFloat();
	shadow->drawDist = f->GetFloat();

	if(shadow->isBuildingShadow) {
		// a building shadow is never distance tested: it is submitted from any
		// distance and the display list culls it per node
		shadow->doDistanceTest = false;
		shadow->SetNumElements(1);
	} else
		shadow->SetNumElements(2);

	if(composite == nil) {
		fprintf(stderr, "warning : composite \"%s\" not found while loading shadow from file %s\n",
			compName, f->GetName());
		delete shadow;
		return;
	}

	// Every primitive of the composite goes to layer 2 --- the shadow layer, the only
	// one that reaches the stencil volume lists 61..64. Retail also pushes the
	// shadow-casting light's direction into each primitive here (prim+0x5c..+0x64 from
	// GetShadowLight(scene), 0x46b4d0) so the volume can be extruded; the viewer has
	// no ShadowMesh primitive to extrude, so there is nothing to push it into.
	bool anyShadowMesh = false;
	u32 numResolved = 0;
	CompositeDrawable::ActivePrimitiveList *list = composite->GetPrimitiveList();
	for(u32 i = 0; list && i < list->GetNumPrimitives(); i++) {
		DrawableContainer *draw = list->GetPrimitive(i)->GetDrawable();
		if(draw == nil)
			continue;
		numResolved++;
		for(i32 j = 0; j < draw->GetNumElements(); j++) {
			DrawablePrimitive *prim = draw->GetElement(j)->prim;
			if(prim == nil)
				continue;
			if(prim->GetSomeMask() == 0x20)
				anyShadowMesh = true;
			prim->SetLayer(2);
		}
	}

	Assign(shadow->pose, composite->GetPose());
	Assign(shadow->composite, composite);

	// 0x7495ec == 0.2f: the fade band is a fifth of the far distance. With the
	// distance test off (building shadows) this band is never tested.
	float d = anyShadowMesh ? 25.0f : 50.0f;
	shadow->SetElement(composite, 0, !shadow->isBuildingShadow);
	shadow->SetElementDrawDist(0, 0.0f, d, d*0.2f);

	// retail then resolves element 1 of a car/NPC shadow to one of two built-in blob
	// geometries (the names at 0x738450 / 0x738438, i.e. characterShadowDecalShape and
	// vehicleShadowDecalShape) and derives the x/y half sizes from its vertex bounds.

	// The drawables of a building shadow composite are 0x0001001a ShadowMesh chunks and
	// the viewer has no loader for them, so numResolved is 0 and the shadow draws
	// nothing. See re/notes/shadows.md §6 for what drawing them would take.
	if(getenv("P3D_VERBOSE"))
		printf("shadow %s: composite %s, %u/%u drawables%s, building=%d, dist=%.0f\n",
			name, compName, numResolved, list ? list->GetNumPrimitives() : 0,
			anyShadowMesh ? " (shadow meshes)" : "",
			shadow->isBuildingShadow, shadow->drawDist);

	*pObject = shadow;
	*pUID = shadow->GetUID();
}

}
