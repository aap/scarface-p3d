#include "renderable.h"
#include "display_list.h"
#include "../pddi.h"
#include "../shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

namespace renderer
{

bool debugPrinted;

using namespace pure3d;

// ---------------------------------------------------------------- DisplayListPrimitive

// retail: renderer::DisplayListPrimitive::DisplayListPrimitive 0x458c60
DisplayListPrimitive::DisplayListPrimitive(void)
{
	owner = nil;
	drawable = nil;
	instanceMatrix = nil;
	children.Init();
	isVisible = false;
	isInList = false;
	checkDrawable = false;
}

// retail: renderer::DisplayListPrimitive::SetDrawable 0x458e50 --- removes the nodes
// first, because they cache &container->elements[i]
void
DisplayListPrimitive::SetDrawable(DrawableHierarchy *newdrawable, bool check)
{
	RemoveFromList();
	checkDrawable = check;
	Assign(drawable, newdrawable);
}

// retail: renderer::DisplayListPrimitive::RemoveFromList 0x458ea0
void
DisplayListPrimitive::RemoveFromList(void)
{
	if(g_displayList && isInList) {
		g_displayList->RemovePrimitiveNodes(this);
		isInList = false;
	}
}

// retail: renderer::DisplayListPrimitive::SetVisible 0x458ec0 --- sets the bit and
// withdraws if it just became invisible. It never submits; only Display does.
void
DisplayListPrimitive::SetVisible(bool visible)
{
	isVisible = visible;
	if(!isVisible)
		RemoveFromList();
}

// retail: renderer::DisplayListPrimitive::Display 0x458f00
void
DisplayListPrimitive::Display(bool visible)
{
	isVisible = visible;
	if(g_displayList == nil)
		return;

	if(isInList && !isVisible)
		RemoveFromList();
	// retail: if (checkDrawable && inList && !drawable->vslot9()) RemoveFromList();
	// --- the "has my drawable changed under me" case; we have no such vslot.
	if(isVisible && !isInList && drawable) {
		drawable->Display(g_displayList, this);
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
			} else if(!shader->IsSortedBlendMode()) {
				prim->SetLayer(11);
			} else if(shader->IsBlendAddSub()) {
				prim->SetLayer(14);
			} else {
				prim->SetLayer(12);
			}
		} else {
			if(!shader->IsSortedBlendMode()) {
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
		} else if(!shader->IsSortedBlendMode()) {
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


// ---------------------------------------------------------------- Renderable

// retail: Renderable::uniqueId counter g[0x8113f4]; it wraps but skips -1
static u32 uniqueIdCounter = 1;

// retail: renderer::Renderable::Renderable 0x474ba0 --- flags80 = 0x13
// (distance test + fade + visible)
Renderable::Renderable(i32 numElements)
{
	isInsideRoom = false;
	isMatrixDirty = false;

	typeMask = -1;
	doDistanceTest = true;
	doFade = true;
	fadeDirectionOut = false;
	hasHandle = false;
	statePropFitsInPool = false;
	shareLastElementFarDist = false;
	useBoxBoundsFromPose = false;
	distanceToRefPoint = false;

	uniqueId = uniqueIdCounter++;
	if(uniqueIdCounter == 0xffffffff) uniqueIdCounter = 1;
	fadeTime = 0.0f;
	fade = 0.0f;
	fade2 = 0.0f;
	fadeTarget = 1.0f;
	timeSinceDrawn = 0.0f;
	sceneId = 5;		// "not in any scene"

	matrix.Identity();
	Renderable::SetVisible(true);
	SetNumElements(numElements);
}

// retail: renderer::Renderable::SetNumElements 0x474ac0
void
Renderable::SetNumElements(i32 n)
{
	// HACK???
	if(n == 0)
		return;

	elements.Create(n);
	for(i32 i = 0; i < n; i++) {
		elements[i].drawDistMin = 0.0f;
		elements[i].drawDistMax = 0.0f;
		elements[i].drawDistFade = 0.0f;
		elements[i].isFading = false;
	}
}

// retail: renderer::Renderable::SetElement 0x473e70
void
Renderable::SetElement(DrawableHierarchy *drawable, i32 i, bool checkDrawable)
{
	if(i >= GetNumElements() || drawable == nil)
		return;

	drawable->CalcBounds();

	elements[i].prim.SetParent(this);
	elements[i].prim.SetDrawable(drawable, checkDrawable);
	elements[i].drawDistMin = 0.0f;
	elements[i].drawDistMax = 100.0f;
	elements[i].drawDistFade = 10.0f;
	elements[i].isFading = false;
}

// retail: renderer::Renderable::SetElementDrawDist 0x473f00
void
Renderable::SetElementDrawDist(i32 i, float min, float max, float fade)
{
	if(i >= GetNumElements())
		return;
	elements[i].drawDistMin = min;
	elements[i].drawDistMax = max;
	elements[i].drawDistFade = fade;
}

// retail: renderer::Renderable::GetElementDrawable 0x473fc0
DrawableHierarchy*
Renderable::GetElementDrawable(i32 i)
{
	return i < GetNumElements() ? elements[i].prim.GetDrawable() : nil;
}

// retail: renderer::Renderable::SetToFadeIn 0x473aa0 (the leak calls it SetFadeDist,
// but +0x68 is a fade TIME in ms, see notes/renderspine.md §2.4)
void
Renderable::SetFadeDist(float timeMs)
{
	fadeDirectionOut = false;
	fade = 1.0f;
	fadeTarget = 0.0f;
	fadeTime = timeMs;
}

// retail: renderer::Renderable::SetToFadeOut 0x473ac0
void
Renderable::SetToFadeOut(float timeMs)
{
	fadeDirectionOut = true;
	fade = 0.0f;
	fadeTarget = 1.0f;
	fadeTime = timeMs;
}

// retail: renderer::Renderable::SetFade2 0x473d40
void
Renderable::SetFade2(float f)
{
	fade2 = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

// retail: renderer::Renderable::UpdateFade 0x473d90
float
Renderable::UpdateFade(float dt)
{
	// retail divides by fadeTime unguarded: SetFadeDist(0) is "appear instantly"
	// because the division by zero makes the first step complete the fade
	float step = fadeTime > 0.0f ? 1000.0f/fadeTime*dt : 1.0e30f;
	if(!fadeDirectionOut) {
		if(fade > fadeTarget) {
			fade -= step;
			if(fade < fadeTarget) fade = fadeTarget;
		}
	} else if(fade < fadeTarget) {
		fade += step;
		if(fade > fadeTarget) fade = fadeTarget;
	}
	return fade > fade2 ? fade : fade2;
}

// retail: renderer::Renderable::Reset 0x473b70
void
Renderable::Reset(void)
{
	isInsideRoom = false;
	fadeDirectionOut = false;
	fade = 0.0f;
	fade2 = 0.0f;
	timeSinceDrawn = 0.0f;
	fadeTarget = 1.0f;
	SetVisible(true);
	i32 n = GetNumElements();
	for(i32 i = 0; i < n; i++) {
		elements[i].isFading = false;
		DrawableHierarchy *d = elements[i].prim.GetDrawable();
		if(d) { d->SetFading(false); d->SetFadeAmount(0.0f); }
	}
}

// retail: renderer::Renderable::Tick 0x4740c0 --- not virtual, and it runs even when the
// renderable is hidden
void
Renderable::Tick(TimeInfo *t)
{
	timeSinceDrawn += t->dt;
	if(doFade)
		UpdateFade(t->fadeDt);
}

// retail: vslot 8, renderer::Renderable::SetVisible 0x473c20
void
Renderable::SetVisible(bool visible)
{
	isVisible = visible;
	i32 n = GetNumElements();
	for(i32 i = 0; i < n; i++)
		elements[i].prim.SetVisible(visible);
}

// retail: vslot 9 --- the per-class pre-display hook the scene calls just before
// Display. The base is a stub (0x438400); this is where animation, wind and decal
// ageing happen in the subclasses.
void
Renderable::Update(TimeInfo *t)
{
}

// retail: vslot 11 --- immediate-mode draw, used by the non-gameplay scenes and by
// Display_List::Render for the three RenderManager-owned pools. Base is a nullsub.
void
Renderable::RenderImmediate(void)
{
}

// retail: vslot 12, renderer::Renderable::SetMatrix 0x473b40
void
Renderable::SetMatrix(const Matrix &m)
{
	matrix = m;
	isMatrixDirty = true;
}

// retail: vslot 13, renderer::Renderable::GetPosition 0x473c00
void
Renderable::GetPosition(Vector *p)
{
	*p = *matrix.GetPosition();
}

// retail: vslot 14, renderer::Renderable::Hide 0x473c80 --- drop my display list nodes
// but keep the visible flag
void
Renderable::Hide(void)
{
	i32 n = GetNumElements();
	for(i32 i = 0; i < n; i++)
		elements[i].prim.RemoveFromList();
}

// retail: vslot 15, base 0x559000 returns false
bool
Renderable::GetDistanceRefPos(Vector *p)
{
	return false;
}

// retail: vslot 10, renderer::Renderable::Display 0x4740f0 --- a state machine, not a
// draw call (notes/renderspine.md §2.2). Distance band, then frustum, then occluders,
// then the cross-fade alpha; on success it pushes the matrix and lets the element's
// DisplayListPrimitive submit itself into the Display_List.
void
Renderable::Display(void)
{
	i32 n = GetNumElements();
	if(n <= 0)
		return;
	DrawableHierarchy *d0 = elements[0].prim.GetDrawable();
	if(d0 == nil)
		return;
	Camera *cam = View_GetCullingCamera();

	// retail also builds the sphere out of the pose bounding box when
	// useBoxBoundsFromPose (state props); we have no posed renderables yet.
	Sphere lsph = d0->sphere;
	Vector wsph = Multiply(lsph.centre, matrix);

	Vector camPos;
	cam->GetPosition(&camPos);

	Vector ref;
	if(!GetDistanceRefPos(&ref)) {
		// retail: ref = matrix.row3. Every world geo renderable here keeps the identity
		// matrix, so that would measure a whole shell from the world origin.
		ref = wsph;
	}
	// not retail: retail measures to the reference POINT, which only works because the
	// classes that have huge composites (details_/shells_/skyline_ world geo) never
	// reach this function --- WorldGeoRenderable::Display (0x471640, worldgeo.cpp) culls
	// and fades their sub-primitives one by one instead. What is left here is plain
	// (unprefixed) world geo, whose matrix is the identity and which often has no
	// otherPosition, so the point would be the world origin: measure to the bounding
	// sphere's surface instead.
	float dist = Norm(ref - camPos) - (distanceToRefPoint ? 0.0f : lsph.radius);
	if(dist < 0.0f) dist = 0.0f;
	float scale = cam->GetDrawDistanceScale();
	if(scale > 1.0f) scale = 1.0f;
	dist *= scale;

	// normally only element 0 is considered; a multi-element LOD chain that shares the
	// last element's far distance considers them all
	i32 count = n;
	if((shareLastElementFarDist || !doDistanceTest) && n > 1)
		count = 1;

	for(i32 i = 0; i < count; i++) {
		DisplayListElement *el = &elements[i];
		DrawableHierarchy *d = el->prim.GetDrawable();
		if(d == nil) {
			el->prim.Display(false);
			continue;
		}
		float alpha = 0.0f;
		bool visible = true;

		if(doDistanceTest) {
			float minD = el->drawDistMin;
			float maxD = el->drawDistMax;
			if(shareLastElementFarDist && count > 1)
				maxD = elements[count-1].drawDistMax;
			float fadeBand = el->drawDistFade;
			// not retail: an element whose zone package never gave it draw distances
			// would never be drawn at all
			if(maxD <= 0.0f) maxD = 500.0f;

			if(!(dist < maxD) || !(dist >= minD))
				visible = false;
			else if(!cam->SphereVisible(wsph, lsph.radius))
				visible = false;
			// retail also runs occlude::TestSphere (0x460980) here; we have no
			// occluders (see occlude::IsBoxVisible in display_list.cpp)

			if(visible && doFade && fadeBand > 0.0f) {
				if(count > 1) {
					// multi-element LOD chain: element 0 never fades
					if(i > 0) {
						float fadeOut = (dist - (maxD - fadeBand))/fadeBand;
						float fadeIn = (minD + fadeBand*0.5f - dist)/(fadeBand*0.5f);
						if(fadeOut >= 0.0f && fadeOut <= 1.0f) alpha = fadeOut;
						else if(fadeIn >= 0.0f && fadeIn <= 1.0f) alpha = fadeIn;
					}
				} else {
					float inv = 1.0f/fadeBand;
					float fadeOut = (dist - (maxD - fadeBand))*inv;
					if(fadeOut >= 0.0f && fadeOut <= 1.0f) alpha = fadeOut;
					if(minD > 0.0f) {
						float fadeIn = (fadeBand + minD - dist)*inv;
						if(fadeIn >= 0.0f && fadeIn <= 1.0f) alpha = fadeIn;
					}
				}
				// combine with the renderable-wide fade
				float g = fade > fade2 ? fade : fade2;
				float fadeAmount = alpha*(1.0f - g) + g;
				if(fadeAmount > 0.0f) {
					if(!el->isFading) {
						el->isFading = true;
						el->prim.RemoveFromList();	// the node caches the list id
						d->SetFading(true);
					}
					d->SetFadeAmount(fadeAmount);
				} else if(el->isFading) {
					el->isFading = false;
					el->prim.RemoveFromList();
					d->SetFading(false);
					d->SetFadeAmount(0.0f);
				}
			}
		} else if(typeMask == TYPE_SKY && doFade)
			d->SetFadeAmount(fade > fade2 ? fade : fade2);

		if(getenv("P3D_VERBOSE") && !debugPrinted)
			printf("%-32s dist %8.1f min %6.1f max %8.1f fade %5.1f radius %7.1f -> %d\n",
				GetName(), dist, el->drawDistMin, el->drawDistMax, el->drawDistFade,
				lsph.radius, visible);

		if(!visible) {
			el->prim.Display(false);
			continue;
		}
		if(isMatrixDirty)
			el->prim.RemoveFromList();	// the node caches the world matrix
		timeSinceDrawn = 0.0f;
		// retail: matrixStack->Push(0); LoadMatrix(0, &this->matrix); prim.Display(true);
		// Pop(0). The node captures whatever world matrix is current, so it has to be
		// the renderable's own (native) one --- the transform of the pass is re-applied
		// by the list walks in Display_List::Render.
		context->PushWorldMatrix();
		context->SetWorldMatrix(matrix);
		el->prim.Display(true);
		context->PopWorldMatrix();
	}
	isMatrixDirty = false;
}

}
