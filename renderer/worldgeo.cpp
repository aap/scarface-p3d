#include "worldgeo.h"
#include "../compositedrawable.h"
#include "../shader.h"

#include <string.h>

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
	else if(strstr(compName, "low_LOD_") == compName)
		worldgeo->isLowLOD = true;

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
