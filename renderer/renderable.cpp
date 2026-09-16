#include "renderable.h"
#include "display_list.h"
#include "../pddi.h"
#include "../shader.h"
#include <stdio.h>
#include <stdlib.h>

static bool debugPrinted;

// temporary hack
math::Vector camPosition;

namespace renderer
{

using namespace pure3d;

DisplayListPrimitive::DisplayListPrimitive(void)
{
	parent = nil;
	drawable = nil;
	instanceMatrix = nil;
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




// Layer assignment for a "normal" world geo / prop primitive, by shader type
// (retail WorldGeoLoader::LoadObject 0x471ec0, normal branch). Sets flag1 for
// untextured/vertexfade prims.
void
SetPrimLayerByShader(DrawablePrimitive *prim, bool &flag1)
{
	Shader *shader = prim->GetShader();
	u32 type = prim->GetSomeMask();
	if(type == 2 || type == 4 || type == 8) {
		prim->SetLayer(1);
	} else switch(shader->GetType()) {
	case Shader::SHADER_UNTEXTURED:
	case Shader::SHADER_VERTEXFADE:
		prim->SetLayer(33);
		flag1 = true;
		break;
	case Shader::SHADER_SPECULAR:
		prim->SetLayer(5);
		break;
	case Shader::SHADER_SPECULAR_MCBV:
		prim->SetLayer(6);
		break;
	case Shader::SHADER_FOAM:
		prim->SetLayer(29);
		break;
	case Shader::SHADER_NIGHTLIGHT:
		prim->SetLayer(3);
		break;
	case Shader::SHADER_SHADOWDECAL:
		prim->SetLayer(37);
		break;
	case Shader::SHADER_DECAL:
		switch(shader->GetBlendMode()) {
		case PDDI_BLEND_ADD:
		case PDDI_BLEND_SUBTRACT:
			prim->SetLayer(21);
			break;
		default:
			prim->SetLayer(22);
			break;
		}
		break;
	case Shader::SHADER_ENV:
		prim->SetLayer(26);
		break;
	case Shader::SHADER_SIMPLE:
		if(shader->GetIsLit()) {
			if(shader->GetAlphaTest() && shader->GetBlendMode() == PDDI_BLEND_NONE) {
				prim->SetLayer(13);
			} else if(!shader->IsXXXBlendMode()) {
				prim->SetLayer(11);
			} else if(shader->IsBlendAddSub()) {
				prim->SetLayer(14);
			} else {
				prim->SetLayer(12);
			}
		} else {
			if(!shader->IsXXXBlendMode()) {
				prim->SetLayer(7);
			} else if(shader->IsBlendAddSub()) {
				prim->SetLayer(10);
			} else {
				prim->SetLayer(8);
			}
		}
		break;
	case Shader::SHADER_CBVLIT:
		if(shader->GetAlphaTest() && shader->GetBlendMode() == PDDI_BLEND_NONE) {
			prim->SetLayer(17);
		} else if(!shader->IsXXXBlendMode()) {
			prim->SetLayer(15);
		} else if(shader->IsBlendAddSub()) {
			prim->SetLayer(18);
		} else {
			prim->SetLayer(16);
		}
		break;
	case Shader::SHADER_LAYERED:
		if(shader->GetIsLit())
			prim->SetLayer(20);
		else
			prim->SetLayer(19);
		break;
	default: break;
	}
}

Renderable::Renderable(i32 numElements)
{
	flagB1 = false;
	isMatrixDirty = false;

	typeMask = -1;
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
Renderable::SetElementDrawDist(i32 i, float min, float max, float fade)
{
	if(i >= (i32)elements.Size())
		return;
	elements[i].drawDist[0] = min;
	elements[i].drawDist[1] = max;
	elements[i].drawDist[2] = fade;
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

		// retail transforms the element bound sphere and tests min/max/fade;
		// the draw distances come from the ZonePkg entries (0x8800009).
		Sphere sph = elements[i].prim.GetDrawable()->sphere;
		float dist = Norm(sph.centre - camPosition) - sph.radius;
		if(dist < 0.0f) dist = 0.0f;	// inside the sphere
		float maxDist = elements[i].drawDist[1] > 0.0f ? elements[i].drawDist[1] : 500.0f;
		bool visible = dist >= elements[i].drawDist[0] && dist < maxDist;
		if(getenv("P3D_VERBOSE") && !debugPrinted)
			printf("%-32s dist %8.1f min %6.1f max %8.1f fade %5.1f radius %7.1f -> %d\n", GetName(), dist, elements[i].drawDist[0], maxDist, elements[i].drawDist[2], sph.radius, visible);

		elements[i].prim.Display(visible);
	}
}

}
