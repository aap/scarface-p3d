#include "renderable.h"
#include "display_list.h"
#include "../pddi.h"

// temporary hack
math::Vector camPosition;

namespace renderer
{

using namespace pure3d;

DisplayListPrimitive::DisplayListPrimitive(void)
{
	parent = nil;
	drawable = nil;
	children.Init();
	isVisible = false;
	isInList = false;
	flag4 = false;
}

void
DisplayListPrimitive::SetDrawable(DrawableHierarchy *newdrawable, bool flg)
{
	if(Display_List::Inst && isInList) {
		Display_List::Inst->RemovePrimitive(this);
		isInList = false;
	}
	flag4 = flg;
	Assign(drawable, newdrawable);
}

void
DisplayListPrimitive::SetVisible(bool visible)
{
	isVisible = visible;
	if(Display_List::Inst && isInList && !isVisible) {
		Display_List::Inst->RemovePrimitive(this);
		isInList = false;
	}
}

void
DisplayListPrimitive::Display(bool visible)
{
	isVisible = visible;
	if(Display_List::Inst == nil)
		return;

	// Remove from render list if not visible
	if(isInList && !isVisible) {
		Display_List::Inst->RemovePrimitive(this);
		isInList = false;
	}

	// TODO: some other condition

	// Render to list of visible
	if(isVisible && !isInList) {
		drawable->Display(Display_List::Inst, this);
		isInList = true;
	}
}




Renderable::Renderable(i32 numElements)
{
	flagB1 = false;
	isMatrixDirty = false;

	flag1 = true;
	doDistFade = true;
	flag4 = false;
	flag8 = false;

	matrix.Identity();
	Renderable::SetVisible(true);
	SetNumElements(numElements);
}

void
Renderable::SetNumElements(i32 n)
{
	// HACK???
	if(n == 0)
		return;

	elements.Create(n);
	for(i32 i = 0; i < n; i++) {
		elements[i].drawDist[0] = 0.0f;
		elements[i].drawDist[1] = 0.0f;
		elements[i].drawDist[2] = 0.0f;
		elements[i].isFading = false;
	}
}

void
Renderable::SetElement(DrawableHierarchy *drawable, i32 i, bool flag4)
{
	if(i >= (i32)elements.Size() || drawable == nil)
		return;

	drawable->CalcBounds();

	elements[i].prim.SetParent(this);
	elements[i].prim.SetDrawable(drawable, flag4);
	elements[i].drawDist[0] = 0.0f;
	elements[i].drawDist[1] = 100.0f;
	elements[i].drawDist[2] = 10.0f;
	elements[i].isFading = false;
}

void
Renderable::SetVisible(bool visible)
{
	isVisible = visible;
	i32 n = elements.Size();
	for(i32 i = 0; i < n; i++)
		elements[i].prim.SetVisible(visible);
}

void
Renderable::Display(void)
{
	// TODO: this is very wrong and simplified

	i32 n = elements.Size();
	for(i32 i = 0; i < n; i++) {

		// TODO: use own transform
		//       do fading
		//       &c

		Sphere sph = elements[i].prim.GetDrawable()->sphere;
		float dist = Norm(sph.centre - camPosition) - sph.radius;
		bool visible = dist < 500.0f;

		elements[i].prim.Display(visible);
	}
}

}
