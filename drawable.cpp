#include "drawable.h"
#include "shader.h"
#include "pddi.h"
#include "displaylist.h"

#include <stdlib.h>


namespace pure3d
{

int cmpDrawablePrim(const void *p1, const void *p2)
{
	if(((PrimEntry*)p1)->prim == nil) return 1;
	if(((PrimEntry*)p2)->prim == nil) return -1;
	Shader *sh1 = ((PrimEntry*)p1)->prim->GetShader();
	Shader *sh2 = ((PrimEntry*)p2)->prim->GetShader();
	pddiBlendMode bm1 = sh1->GetBlendMode();
	pddiBlendMode bm2 = sh2->GetBlendMode();
	return bm1 - bm2;
}

DrawableContainer::DrawableContainer(i32 nPrim)
 : parent(this), prims(nPrim)
{
	// unknown = 0.5f
}

void
DrawableContainer::DrawPrimitives(DisplayList *list, GameDrawableInfo *info)
{
	if(list) {
		list->AddContainer(this, nil, info);
		return;
	}
	// TODO: this is completely wrong

	// total HACK
	qsort(&prims[0], prims.Size(), sizeof(PrimEntry), cmpDrawablePrim);

	int n = prims.Size();
	for(int i = 0; i < n; i++) {
		prims[i].Display();
	}
}

void
DrawableContainer::SetFlags(void)
{
	int n = prims.Size();
	for(int i = 0; i < n; i++) {
// TODO: remove this once we can
if(prims[i].prim == nil)
continue;
		if(prims[i].prim->IsLit())
			isLit = true;
		if(prims[i].prim->IsALUM())
			isALUM = true;
		Shader *sh = prims[i].prim->GetShader();
		if(sh && sh->GetBlendMode() == PDDI_BLEND_ALPHA)
			alphaBlend = true;
	}
}

void
DrawableContainer::CalcBounds(void)
{
	u32 n = prims.Size();
	if(n == 0) {
		box = Box3D(Vector(0.0f, 0.0f, 0.0f), Vector(1.0f, 1.0f, 1.0f));
	} else {
// TODO: remove once we can load everything
if(prims[0].prim == nil)
return;
		prims[0].prim->UpdateBounds();
		box = prims[0].prim->box;
		for(u32 i = 1; i < n; i++) {
assert(prims[i].prim);
			prims[i].prim->UpdateBounds();
			Box3D b = prims[i].prim->box;
			box.ContainPoint(b.low);
			box.ContainPoint(b.high);
		}
	}
	sphere = box.GetSphere();
}


void
PrimEntry::Display(void)
{
	if(prim == nil) {
//		fprintf(stderr, "No primitive\n");
		return;
	}

	/* TODO: walk children here
	int n = children.Size();
	for(int i = 0; i < n; i++)
		...
	*/
	prim->Display();
}

}
