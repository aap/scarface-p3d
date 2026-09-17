#include "worldgeo.h"
#include "../compositedrawable.h"
#include "../shader.h"
#include "../pddi.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace renderer
{

using namespace pure3d;

WorldGeoRenderable::WorldGeoRenderable(void)
 : Renderable(0),
   useOtherPosition(false),
   isDetails(false),
   isSkyline(false),
   drawFirst(false),
   isLowLOD(false),
   primitives(nil),
   poseIDs(nil),
   numPrimitives(0)
{
	typeMask = TYPE_WORLDGEO;
	otherPosition = Vector(0.0f, 0.0f, 0.0f);
}

WorldGeoRenderable::~WorldGeoRenderable(void)
{
	delete[] primitives;
}

void
WorldGeoRenderable::SetNumPrimitives(i32 n)
{
	assert(primitives == nil);
	primitives = new DisplayListPrimitive[n];
	poseIDs = new i32[n];
	numPrimitives = n;
}

// retail: 0x471570 --- the base, then every sub-primitive
void
WorldGeoRenderable::SetVisible(bool visible)
{
	Renderable::SetVisible(visible);
	for(i32 i = 0; i < numPrimitives; i++)
		primitives[i].SetVisible(visible);
}

// retail: 0x471510
void
WorldGeoRenderable::Hide(void)
{
	Renderable::Hide();
	for(i32 i = 0; i < numPrimitives; i++)
		primitives[i].RemoveFromList();
}

// retail: 0x4714e0 --- THE hook for "measure my distance from somewhere other than my
// origin". The point comes from the package's 0x8800009 record and is horizontal only.
bool
WorldGeoRenderable::GetDistanceRefPos(Vector *p)
{
	*p = otherPosition;
	return useOtherPosition;
}

// retail: g[0x7c0a48] / g[0x7c0a44] / g[0x7c0a40] (0x47188b..0x4718e9). Initialised in
// .data to the level-2 values, which is what we default to.
static float drawDistDetails = 120.0f;
static float drawDistShells = 1500.0f;
static float drawDistSkyline = 3000.0f;

void
SetWorldGeoDrawDistanceLevel(i32 level)
{
	switch(level) {
	case 0:
		drawDistDetails = 60.0f; drawDistShells = 150.0f; drawDistSkyline = 300.0f;
		break;
	case 1:
		drawDistDetails = 120.0f; drawDistShells = 800.0f; drawDistSkyline = 1500.0f;
		break;
	case 2:
		drawDistDetails = 120.0f; drawDistShells = 1500.0f; drawDistSkyline = 3000.0f;
		break;
	// retail leaves the globals alone for anything else
	}
}

// retail: renderer::WorldGeoRenderable::Display 0x471640 (vslot 10).
//
// details_ / cbvlitdecals_ / skyline_ / shells_ / underwater_ world geo does NOT go
// through Renderable::Display: one of those composites is a whole city block or a whole
// island shell, and one bounding sphere for the lot is useless. Instead every
// sub-drawable of the composite is distance tested, frustum culled and faded on its own,
// through its own DisplayListPrimitive, so every sub-drawable becomes its own display
// list node. See notes/renderspine.md §4.5.
//
// Two things this path does NOT do, and both are deliberate in retail:
//   * it ignores the element's drawDistMin/Max/Fade (the ZonePkg 0x8800009 numbers);
//     the band comes from the global per-kind table above and there is no near distance
//   * it ignores this->matrix; the world matrix is the composite's pose matrix table
void
WorldGeoRenderable::Display(void)
{
	// retail: g[0x7c0a10] ("draw plain world geo") and g[0x7c0a11] ("draw details /
	// skyline / shells / low LOD") are debug toggles with no writer in the image, both
	// 1, so only the "on" side of the dispatch exists here.
	if(isLowLOD || !(isDetails || isSkyline || drawFirst)) {
		Renderable::Display();
		return;
	}

	// element 0 is the whole composite. On this path it is never submitted, so drop
	// the node if something (an earlier Renderable::Display) put one there.
	if(GetNumElements() > 0)
		elements[0].prim.RemoveFromList();

	// the element-0 drawable of a world geo is always the CompositeDrawable the loader
	// found; retail gets at it the same way (GetElementDrawable(0), 0x473fc0)
	CompositeDrawable *comp = (CompositeDrawable*)GetElementDrawable(0);
	if(comp == nil || numPrimitives <= 0)
		return;
	Pose *pose = comp->GetPose();
	if(pose == nil)
		return;

	Camera *cam = View_GetCullingCamera();
	Vector camPos;
	cam->GetPosition(&camPos);

	// the whole composite's sphere decides once whether to look at the parts at all
	bool coarseVisible = cam->SphereVisible(comp->sphere.centre, comp->sphere.radius);
	// retail passes a hardcoded dt of 0.33 here (0x3ea8f5c3), on top of the one
	// Renderable::Tick already applied this frame
	float globalFade = UpdateFade(0.33f);

	float scale = cam->GetDrawDistanceScale();
	if(scale > 1.0f) scale = 1.0f;

	// retail picks the band inside the loop, out of the globals it has just rewritten;
	// the fade widths are immediates in the code
	float maxDist, fadeBand;
	if(drawFirst) {
		maxDist = drawDistShells; fadeBand = 50.0f;
	} else if(isSkyline) {
		maxDist = drawDistSkyline; fadeBand = 80.0f;
	} else {
		maxDist = drawDistDetails; fadeBand = 20.0f;
	}

	// retail: matrixStack->Push(0); LoadMatrix(0, &poseMatrix[0]). Note that it loads
	// the pose root, not this->matrix --- for map geometry both are the identity.
	const Matrix &base = *pose->GetMatrix(0);
	context->PushWorldMatrix();
	context->SetWorldMatrix(base);

	i32 numVisible = 0;
	i32 numFading = 0;
	static const char *dbgWG = getenv("P3D_DEBUGWG");
	static int dbgFrames = 0;
	if(dbgWG && dbgFrames > 3) dbgWG = nil;	// a few frames are enough
	if(dbgWG) dbgFrames++;
	for(i32 i = 0; i < numPrimitives; i++) {
		DrawableHierarchy *d = primitives[i].GetDrawable();
		const Matrix &poseMat = *pose->GetMatrix(poseIDs[i]);
		// retail: matrixStack->PushMultiply(0, &poseMatrix[poseIDs[i]]), popped at the
		// end of the iteration --- the node captures whatever is current at submit time
		context->PushWorldMatrix();
		context->MultWorldMatrix(poseMat);

		bool visible = false;
		if(coarseVisible && d) {
			// the sub-drawable's own sphere, through its own pose matrix
			Vector wc = Multiply(Multiply(d->sphere.centre, poseMat), base);
			// to the sphere SURFACE (the base Display measures to the ref point),
			// not clamped at 0: inside the sphere the distance goes negative
			float dist = (Norm(wc - camPos) - d->sphere.radius)*scale;

			if(dist <= maxDist && cam->SphereVisible(wc, d->sphere.radius)) {
				visible = true;
				// the far cross-fade band only; there is no near band here
				float alpha = 0.0f;
				float fadeOut = (dist - (maxDist - fadeBand))/fadeBand;
				if(fadeOut >= 0.0f && fadeOut <= 1.0f)
					alpha = fadeOut;
				// retail MAXes the renderable-wide fade in here; the base
				// Display lerps instead (alpha*(1-g) + g)
				if(globalFade > alpha)
					alpha = globalFade;

				// the "was I fading" state is the sub-drawable's own flag, not a
				// DisplayListElement::isFading --- there is no element here
				if(alpha > 0.0f) {
					numFading++;
					if(!d->IsFading()) {
						primitives[i].RemoveFromList();	// the node caches the list id
						d->SetFading(true);
					}
					d->SetFadeAmount(alpha);
				} else if(d->IsFading()) {
					primitives[i].RemoveFromList();
					d->SetFading(false);
					d->SetFadeAmount(0.0f);
				}
			}
		}
		if(visible)
			numVisible++;
		// P3D_DEBUGWG=<substr>: every sub-primitive decision of the matching world geos
		if(dbgWG && strstr(GetName(), dbgWG) && d) {
			Vector wc = Multiply(Multiply(d->sphere.centre, poseMat), base);
			printf("  %-24s sub %2d %-40s centre %7.1f %6.1f %7.1f r %6.1f dist %7.1f -> %d\n", GetName(), i, d->GetName(), wc.x, wc.y, wc.z, d->sphere.radius, (Norm(wc - camPos) - d->sphere.radius)*scale, visible);
		}
		primitives[i].Display(visible);

		context->PopWorldMatrix();
	}
	context->PopWorldMatrix();
	// retail touches neither timeSinceDrawn nor isMatrixDirty here: this path never
	// looks at this->matrix in the first place

	if(getenv("P3D_VERBOSE") && !debugPrinted)
		printf("%-32s %3d/%3d sub-primitives visible (%d fading)  max %6.1f fade %5.1f radius %7.1f\n",
			GetName(), numVisible, numPrimitives, numFading, maxDist, fadeBand,
			comp->sphere.radius);
}



WorldGeoLoader::WorldGeoLoader(void)
 : SimpleChunkHandler(Renderable::WORLDGEO_LOADER)
{
}

void
WorldGeoLoader::LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];
	char compName[256];

	f->GetString(name);

	WorldGeoRenderable *worldgeo = new WorldGeoRenderable;
	worldgeo->SetName(name);

	f->GetString(compName);
	u32 type = f->GetU32();
	enum {
		SHADOW = 1,
		LOW_LOD = 3,
		UNDERWATER = 5,
		CBVLITDECALS,
		INTERIORFLOORS,
		CARDSNIGHT
	};

	CompositeDrawable *composite = inventory->Find<CompositeDrawable>(compName);
	assert(composite);

//	printf("\t\t\tWorld Geo: %d %s %s\n", type, name, compName);

	bool flag1 = false;
	if(type == LOW_LOD || type == UNDERWATER || type == CBVLITDECALS || type == INTERIORFLOORS || type == CARDSNIGHT) {
		i32 n = composite->GetPrimitiveList()->GetNumPrimitives();
		for(i32 i = 0; i < n; i++) {
			DrawableContainer *draw = composite->GetPrimitiveList()->GetPrimitive(i)->GetDrawable();
			for(i32 j = 0; j < draw->GetNumElements(); j++) {
				DrawablePrimitive *prim = draw->GetElement(j)->prim;
				Shader *shader = prim->GetShader();
				// TODO: this is ugly. was it written differently perhaps?
				if(type == UNDERWATER) {
					prim->SetLayer(34);
				} else if(type == CBVLITDECALS) {
					switch(shader->GetBlendMode()) {
					case PDDI_BLEND_ADD:
					case PDDI_BLEND_SUBTRACT:
						prim->SetLayer(23);
						break;
					case PDDI_BLEND_ALPHA:
						prim->SetLayer(24);
						break;
					default: break;
					}
				} else if(type == INTERIORFLOORS) {
					if(shader->GetBlendMode() == PDDI_BLEND_ALPHA && !shader->GetIsLit())
						prim->SetLayer(25);
				} else if(shader->GetType() == Shader::SHADER_UNTEXTURED || shader->GetType() == Shader::SHADER_VERTEXFADE) {
					prim->SetLayer(33);
					flag1 = true;
				} else if(type == CARDSNIGHT) {
					prim->SetLayer(4);
				} else {
					prim->SetLayer(33);
					flag1 = true;
				}
			}
		}
	} else {
		i32 n = composite->GetPrimitiveList()->GetNumPrimitives();
		for(i32 i = 0; i < n; i++) {
			DrawableContainer *draw = composite->GetPrimitiveList()->GetPrimitive(i)->GetDrawable();
// TODO: this should go
if(draw == nil) continue;
			for(i32 j = 0; j < draw->GetNumElements(); j++) {
				DrawablePrimitive *prim = draw->GetElement(j)->prim;
				SetPrimLayerByShader(prim, flag1);
			}
		}
	}

	if(strstr(compName, "details_") == compName || strstr(compName, "cbvlitdecals_") == compName)
		worldgeo->isDetails = true;
	else if(strstr(compName, "skyline_") == compName)
		worldgeo->isSkyline = true;
	else if(strstr(compName, "shells_") == compName || strstr(compName, "underwater_") == compName)
		worldgeo->drawFirst = true;
	else if(strstr(compName, "low_LOD_") == compName) {
		worldgeo->isLowLOD = true;
		// retail draws it through the base Display, measured to the zone package's 2-D
		// point (miami_lod: 581..10000 m from a spot in north beach)
		worldgeo->distanceToRefPoint = true;
	}

	if(!worldgeo->isLowLOD) {
		i32 n = composite->GetPrimitiveList()->GetNumPrimitives();
		worldgeo->SetNumPrimitives(n);
		for(i32 i = 0; i < n; i++) {
			worldgeo->primitives[i].SetParent(worldgeo);
			worldgeo->primitives[i].SetDrawable(composite->GetPrimitiveList()->GetPrimitive(i)->GetDrawable(), false);
			worldgeo->poseIDs[i] = composite->GetPrimitiveList()->GetPrimitive(i)->id;
		}
	}
	worldgeo->SetNumElements(1);
	worldgeo->SetElement(composite, 0, false);

	// retail: flags80 &= ~1 --- untextured / vertex-fade world geo (the low-LOD city
	// and the skyline) is not distance tested at all, it is always drawn, early, and
	// the real geometry covers it
	if(flag1)
		worldgeo->doDistanceTest = false;

	*pObject = worldgeo;
	*pUID = worldgeo->GetUID();
}

}
