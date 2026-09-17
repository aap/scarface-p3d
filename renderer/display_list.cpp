// renderer::Display_List --- the retained draw list.
//
// See re/notes/displaylist.md for where all of this comes from. The short version:
// a node is a self-contained draw record that is submitted once and then lives in one
// of 84 buckets until its owner says it is no longer visible. A bucket is "a set of
// primitives that want exactly the same pddi state and the same place in the draw
// order"; Render() walks the buckets in a hand-written order, setting state once per
// bucket. Every list walk culls again per node, because the list is not "visible
// geometry", it is "geometry that was visible when it was submitted".

#include "display_list.h"
#include "renderable.h"
#include "../shader.h"
#include "../pddi.h"
#include <stdlib.h>
#include <vector>

namespace occlude
{

using namespace math;

// retail: occlude::IsBoxVisible 0x458bf0 --- for every enabled occluder, if the box is
// entirely behind every one of its planes (occlude::Occluder::TestBox 0x458ba0 returning
// 2) the box is hidden. We have no occluders, so nothing is ever hidden.
bool
IsBoxVisible(const Box3D &box)
{
	return true;
}

}

namespace renderer
{

// debug temporary (the explorer reads these)
bool displistvisible[NUM_DISPLAY_LISTS];
int displistsize[NUM_DISPLAY_LISTS];
int listorder[1000];
int nlists;

using namespace pure3d;

// retail: g_displayList g[0x8108a0] / renderer::SetGlobalDisplayList 0x4589d0
Display_List *g_displayList;

void
SetGlobalDisplayList(Display_List *list)
{
	g_displayList = list;
}

// retail: Display_List::Display_List 0x45f110 --- malloc(0x3f0) for the 84 LinkedLists,
// clear listDirty[84], allocate numNodes*0x70 in one block and thread them onto the free
// list, writing node->self as it goes.
Display_List::Display_List(i32 numNodes)
 : DisplayList(1, 1)
{
	nodeStore.Allocate(numNodes);
	lists.Allocate(NUM_DISPLAY_LISTS);
	for(u32 i = 0; i < NUM_DISPLAY_LISTS; i++) {
		lists[i].Init();
		listDirty[i] = false;
displistvisible[i] = true;
	}
displistvisible[9] = displistvisible[10] = false;	// env
	freeList.Init();
	for(i32 i = 0; i < numNodes; i++) {
		DisplayListNode *nd = &nodeStore[i];
		nd->parent = nil;
		nd->elem = nil;
		nd->container = nil;
		nd->shader = nil;
		nd->myList = nil;
		nd->sortKey = 0.0f;
		nd->sortKey2 = 0.0f;
		nd->linkParent.Init();
		freeList.Insert(&nd->link);
		nd->self = nd;
	}
	cameraInsideVolume = false;
	cameraIndoors = false;
	shadowVolumeColour = 0xff191919;
}


// ---------------------------------------------------------------- submission

// retail: renderer::Display_List::AddContainer 0x45d360 (vslot 7)
void
Display_List::AddContainer(DrawableContainer *container, Matrix *matrix, GameDrawableInfo *info)
{
	for(i32 i = 0; i < container->GetNumElements(); i++)
		AddContainerElement(container, i, matrix, info);
}

// retail: renderer::Display_List::AddContainerElement 0x45d3b0 (vslot 8)
void
Display_List::AddContainerElement(DrawableContainer *container, i32 idx, Matrix *matrix, GameDrawableInfo *info)
{
	bool isFading = container->IsFading();
	ListLink<DisplayListNode> *link = freeList.anchor.next;
	if(link == nil)
		return;		// silently dropped when the pool is empty
	DisplayListNode *nd = (DisplayListNode*)link;
	DisplayListPrimitive *owner = (DisplayListPrimitive*)info;

	// not retail: eco prop placements share one drawable and carry their own matrix
	// here; retail hands them to the instancing extension instead (notes/instances.md).
	if(matrix == nil && owner && owner->GetInstanceMatrix())
		matrix = (Matrix*)owner->GetInstanceMatrix();
	// nd->matrix = matrix * <current world matrix>. The world matrix is the one
	// Renderable::Display loaded, i.e. the renderable's own matrix (times the pose
	// matrix, which CompositeDrawable::Display pushed).
	if(matrix)
		nd->matrix = Multiply(*matrix, context->GetWorldMatrix());
	else
		nd->matrix = context->GetWorldMatrix();

	nd->container = container;
	nd->elem = container->GetElement(idx);
	DrawablePrimitive *prim = nd->elem->prim;
	if(prim == nil)
		return;
	Shader *shader = prim->GetShader();
	if(shader == nil)
		return;
	nd->shader = shader;
	nd->sortKey = container->sortKey;
	nd->sortKey2 = 0.0f;

	u32 shaderType = shader->GetType();
	u32 type = prim->GetSomeMask();
	bool isLit = prim->IsLit();
	bool isALUM = prim->IsALUM();
	i32 listID = -1;
	// the 45-case jump table at 0x45d8dc, see notes/renderspine.md §3.3
	switch(prim->GetLayer()) {
	case 0:
		if(isFading)
			listID = isLit ? 51 : isALUM ? 56 : 55;
		else
			listID = isLit ? 49 : isALUM ? 53 : 52;
		if(isFading) nd->sortKey = 1.0f;
		break;
	case 1:
		if(type == 2 || type == 4 || type == 8) {
			listID = type == 2 ? 66 : 65;
			// 0x45d639 / 0x45d645: water gets its own key class
			nd->sortKey = shader->IsSortedBlendMode() ? 1.0f :
				shader->IsModulateBlendMode() ? 0.5f : 0.0f;
		} else if(shaderType == Shader::SHADER_SHADOWDECAL) {
			listID = 77;
			if(isFading) nd->sortKey = 1.0f;
		} else if(shaderType == Shader::SHADER_DECAL) {
			listID = 75;
			if(isFading) nd->sortKey = 1.0f;
		} else {
			listID = isLit ? 50 : 54;
			if(isFading) nd->sortKey = 1.0f;
		}
		break;
	case 2:
		// Shadows, read off 0x45d66f (re/notes/shadows.md §4). Retail first writes
		// 6.0f into prim->+0x48 --- SHR's tShadow::SetVolumeLength, the length of the
		// extruded shadow volume (shadow.hpp: 0 means "guess it from the extruded
		// bounding volume") --- and then splits three ways on the primitive itself,
		// NOT on the shader:
		//   building shadow (prim+0x68, set by ShadowLoader)  -> 61
		//   a shadow mesh/skin (GetSomeMask() == 0x20)        -> inside a room ? 62 : 63
		//   anything else (the car/NPC blob quads)            -> inside a room ? 62 : 64
		// We have no ShadowMesh primitive to give a volume length to, so that write is
		// left out (it used to go into SetFade, which is a different field and now
		// reaches the shader as PDDI_SP_FADE).
		if(prim->isBuildingShadow)
			listID = 61;
		else {
			bool insideRoom = owner && owner->GetOwner() && owner->GetOwner()->isInsideRoom;
			if(insideRoom)
				listID = 62;
			else
				listID = type == 0x20 ? 63 : 64;
		}
		break;
	case 3: listID = 11; break;
	case 4: listID = 12; break;
	case 5: listID = isFading ? 14 : 13; break;
	case 6: listID = isFading ? 16 : 15; break;
	case 7:
		if(isFading)
			listID = isALUM ? 40 : 41;
		else
			listID = isALUM ? 35 : 36;
		break;
	case 8: listID = isALUM ? 38 : 37; break;
	// case 9: dropped
	case 10: listID = 39; break;
	case 11: listID = isFading ? 32 : 27; break;
	case 12: listID = 29; break;
	case 13: listID = isFading ? 31 : 28; break;
	case 14: listID = 30; break;
	case 15: listID = isFading ? 25 : 21; break;
	case 16: listID = 23; break;
	case 17: listID = isFading ? 26 : 22; break;
	case 18: listID = 24; break;
	case 19: listID = isFading ? 45 : 42; break;
	case 20: listID = isFading ? 44 : 43; break;
	case 21: listID = isFading ? 6 : 4; break;
	case 22: listID = isFading ? 5 : 3; break;
	case 23: listID = isFading ? 20 : 18; break;
	case 24: listID = isFading ? 19 : 17; break;
	case 25: listID = isFading ? 34 : 33; break;
	case 26: listID = isFading ? 10 : 9; break;
	case 27: listID = 60; break;
	case 28: listID = 76; break;
	case 29: listID = isFading ? 1 : 0; break;
	case 30: listID = 57; break;
	case 31: listID = isFading ? 68 : 67; break;
	case 32:
		if(shader->IsSortedBlendMode()) {
			listID = 70;
			if(isFading) nd->sortKey = 0.0f;
		} else
			listID = isFading ? 71 : 69;
		if(isFading && listID != 70) nd->sortKey = 1.0f;
		break;
	case 33: listID = shaderType == Shader::SHADER_VERTEXFADE ? 2 : 59; break;
	case 34:
		if(shaderType == Shader::SHADER_CBVLIT && shader->GetBlendMode() == PDDI_BLEND_ALPHA)
			listID = isFading ? 81 : 79;
		else
			listID = isFading ? 80 : 78;
		break;
	case 35:
		listID = isLit || isALUM ? 50 : 54;
		if(isFading) nd->sortKey = 1.0f;
		break;
	case 36: listID = 58; break;
	case 37: listID = isFading ? 8 : 7; break;
	case 38: listID = 47; break;
	case 39: listID = 46; break;
	case 40: listID = 72; break;
	case 41: listID = 73; break;
	case 42: listID = 74; break;
	// case 43: dropped
	case 44: listID = isFading ? 83 : 82; break;
	default:
		return;		// layers 9, 43 and anything above 44 are dropped
	}
	if(listID < 0)
		return;

	freeList.Remove(&nd->link);
	if(owner) {
		nd->parent = owner;
		owner->children.Insert(&nd->linkParent);
	} else
		nd->parent = nil;	// orphan, recycled by FreeOrphanNodes
	lists[listID].Insert(&nd->link);
	nd->myList = &lists[listID];
	listDirty[listID] = true;
}

// retail: renderer::Display_List::RemovePrimitiveNodes 0x458a20. Walks the owner's
// children, so it only ever holds a pointer to node->linkParent and has to get back to
// the node through node->self (+0x6c).
void
Display_List::RemovePrimitiveNodes(DisplayListPrimitive *prim)
{
	ListLink<DisplayListNode> *next;
	for(ListLink<DisplayListNode> *link = prim->children.anchor.next;
	    link;
	    link = next) {
		next = link->next;
		DisplayListNode *nd = *(DisplayListNode**)(link+1);	// link == &nd->linkParent, +8 == nd->self
		nd->myList->Remove(&nd->link);
		freeList.Insert(&nd->link);
		nd->myList = &freeList;
		prim->children.Remove(link);
		nd->linkParent.Init();
		nd->parent = nil;
	}
}

// retail: renderer::Display_List::FreeOrphanNodes 0x45b0c0, called once at the very end
// of Render(). Everything submitted with no owner (immediate-mode callers) is drawn for
// one frame and then recycled.
void
Display_List::FreeOrphanNodes(void)
{
	for(u32 i = 0; i < NUM_DISPLAY_LISTS; i++) {
		ListLink<DisplayListNode> *next;
		for(ListLink<DisplayListNode> *link = lists[i].anchor.next; link; link = next) {
			next = link->next;
			DisplayListNode *nd = (DisplayListNode*)link;
			if(nd->parent == nil) {
				lists[i].Remove(&nd->link);
				freeList.Insert(&nd->link);
				nd->myList = &freeList;
			}
		}
	}
}


// ---------------------------------------------------------------- culling

// retail: renderer::Display_List::IsNodeVisible 0x45b110, called from 46 sites --- every
// list walk culls every node again, with the PRIMITIVE's own bounds (not the
// renderable's) transformed by the node matrix.
bool
Display_List::IsNodeVisible(Camera *cam, PrimEntry *elem, const Matrix *m)
{
	DrawablePrimitive *prim = elem->prim;
	if(prim == nil)
		return false;
	Sphere s = prim->sphere;
	if(s.radius <= 0.0f)
		return true;		// no bounds in the file: never cull
	// retail adds the matrix translation only (no rotation, no scale) and so gets a
	// slightly wrong bound for rotated or scaled instances; we do the real transform
	// and scale the radius by the largest row length.
	Vector c = Multiply(s.centre, *m);
	float sx = NormSq(*(Vector*)&m->e[0]);
	float sy = NormSq(*(Vector*)&m->e[4]);
	float sz = NormSq(*(Vector*)&m->e[8]);
	float scale = sx > sy ? sx : sy;
	if(sz > scale) scale = sz;
	float r = s.radius * sqrtf(scale);
	if(!cam->SphereVisible(c, r))
		return false;
	Box3D b = prim->box;
	b.low = b.low + Vector(m->e[12], m->e[13], m->e[14]);
	b.high = b.high + Vector(m->e[12], m->e[13], m->e[14]);
	return occlude::IsBoxVisible(b);
}


// ---------------------------------------------------------------- the sort pass

// retail: LinkedList::Sort 0x6e8a20 --- collect the node pointers, qsort them, relink
// head/next/prev in the sorted order (it does NOT re-insert at the head, so the sorted
// order is the walk order).
typedef int (*NodeCmp)(const void*, const void*);

static void
SortList(LinkedList<DisplayListNode> &list, NodeCmp cmp)
{
	if(list.length < 2)
		return;
	static std::vector<DisplayListNode*> nodes;
	nodes.clear();
	nodes.reserve(list.length);
	for(ListLink<DisplayListNode> *link = list.anchor.next; link; link = link->next)
		nodes.push_back((DisplayListNode*)link);
	qsort(&nodes[0], nodes.size(), sizeof(DisplayListNode*), cmp);
	i32 length = list.length;
	list.Init();
	list.length = length;
	list.anchor.next = &nodes[0]->link;
	list.anchor.prev = &nodes[nodes.size()-1]->link;
	for(u32 i = 0; i < nodes.size(); i++) {
		nodes[i]->link.prev = i > 0 ? &nodes[i-1]->link : nil;
		nodes[i]->link.next = i+1 < nodes.size() ? &nodes[i+1]->link : nil;
	}
}

// retail: renderer::Display_List::CmpKey 0x4589e0 --- descending sortKey, never 0
static int
CmpKey(const void *pa, const void *pb)
{
	const DisplayListNode *a = *(DisplayListNode**)pa, *b = *(DisplayListNode**)pb;
	return a->sortKey < b->sortKey ? 1 : -1;
}

// retail: renderer::Display_List::CmpKeyThenDepth 0x458ff0 --- descending sortKey, then
// descending sortKey2, i.e. FAR TO NEAR, which is what alpha blending needs
static int
CmpKeyThenDepth(const void *pa, const void *pb)
{
	const DisplayListNode *a = *(DisplayListNode**)pa, *b = *(DisplayListNode**)pb;
	if(fabsf(a->sortKey - b->sortKey) > 0.001f)
		return a->sortKey < b->sortKey ? 1 : -1;
	if(fabsf(a->sortKey2 - b->sortKey2) <= 0.0001f)
		return 0;
	return a->sortKey2 < b->sortKey2 ? 1 : -1;
}

// retail: renderer::Display_List::CmpShader 0x459060 --- pure material batching. Retail
// keys on shader->+0x10; we have no such field, so the Shader pointer is the key.
static int
CmpShader(const void *pa, const void *pb)
{
	const DisplayListNode *a = *(DisplayListNode**)pa, *b = *(DisplayListNode**)pb;
	if(a->shader == nil) return 0;
	if(b->shader == nil) return 1;
	return a->shader < b->shader ? -1 : 1;
}

// retail: renderer::Display_List::CmpKeyThenMaterial 0x459090 --- descending sortKey,
// then by the shader's texture/material id (shader->+0xc->vslot4), then by shader
// address. We only have the address.
static int
CmpKeyThenMaterial(const void *pa, const void *pb)
{
	const DisplayListNode *a = *(DisplayListNode**)pa, *b = *(DisplayListNode**)pb;
	if(fabsf(a->sortKey - b->sortKey) > 0.001f)
		return a->sortKey < b->sortKey ? 1 : -1;
	if(a->shader == nil) return 0;
	if(b->shader == nil) return 1;
	if(a->shader == b->shader) return 0;
	return a->shader < b->shader ? -1 : 1;
}

// retail: renderer::Display_List::ComputeDepthKeys 0x459120.
// Retail transforms the primitive's bounding sphere centre by the node matrix and stores
// the z of the result; notes/displaylist.md calls that view-space z, but its matrix slot
// 0 holds the world matrix only (§0), so the two readings disagree --- we take the real
// view depth, since that is what a far-to-near sort needs. gmath looks down -z, so
// negate to keep retail's "larger sortKey2 == farther away".
void
Display_List::ComputeDepthKeys(i32 listID)
{
	Matrix worldView = Multiply(context->GetWorldMatrix(), context->GetViewMatrix());
	for(ListLink<DisplayListNode> *link = lists[listID].anchor.next; link; link = link->next) {
		DisplayListNode *nd = (DisplayListNode*)link;
		DrawablePrimitive *prim = nd->elem->prim;
		if(prim == nil) continue;
		Vector c = Multiply(Multiply(prim->sphere.centre, nd->matrix), worldView);
		nd->sortKey2 = -c.z;
	}
}

// retail: renderer::Display_List::ComputeDepthKeysList66 0x4591e0 --- the same thing
// hard-wired to the water surface, transforming the local origin instead of the sphere
// centre (the water quads' spheres are useless, their pivot is not).
void
Display_List::ComputeDepthKeysList66(void)
{
	Matrix worldView = Multiply(context->GetWorldMatrix(), context->GetViewMatrix());
	for(ListLink<DisplayListNode> *link = lists[66].anchor.next; link; link = link->next) {
		DisplayListNode *nd = (DisplayListNode*)link;
		Vector c = Multiply(Multiply(Vector(0.0f, 0.0f, 0.0f), nd->matrix), worldView);
		nd->sortKey2 = -c.z;
	}
}

#define SORT(list, cmp)	if(listDirty[list]) { SortList(lists[list], cmp); listDirty[list] = false; }
#define SORTZ(list)	if(listDirty[list]) { ComputeDepthKeys(list); SortList(lists[list], CmpKeyThenDepth); listDirty[list] = false; }
// the lists whose key depends on the camera are sorted every frame, dirty or not
#define SORTALWAYS(list, cmp)	SortList(lists[list], cmp)
#define SORTZALWAYS(list)	do { ComputeDepthKeys(list); SortList(lists[list], CmpKeyThenDepth); listDirty[list] = false; } while(0)

// retail: renderer::Display_List::SortAllLists 0x45ad10, called from GamePlayScene::Update
// after every renderable has submitted and before Render(). 69 of the 84 lists are sorted;
// 15, 16, 23, 48, 57, 58, 60..64, 72, 73, 74 and 76 never are (they are drawn in reverse
// submit order).
void
Display_List::SortAllLists(void)
{
	Camera *cam = View_GetRenderingCamera();
	Vector camPos;
	cam->GetPosition(&camPos);
	// retail: cameraInsideVolume = TestInteriorVolumes(gLightManager, camPos) 0x45fad0
	cameraInsideVolume = false;

	SORT(46, CmpKey);	SORT(47, CmpKey);
	SORT(59, CmpShader);
	// retail: SortGroup_36_41_42_45 0x459780
	SORT(36, CmpShader);	SORTZ(41);	SORT(42, CmpShader);	SORTZ(45);
	SORT(33, CmpShader);	SORT(34, CmpShader);
	SORT(13, CmpShader);	SORT(14, CmpShader);
	// retail: SortGroup_Buildings 0x459560
	SORT(21, CmpShader);	SORTZ(25);	SORT(22, CmpShader);	SORTZ(26);
	SORT(27, CmpShader);	SORTZ(32);	SORT(28, CmpShader);	SORTZ(31);
	SORT(43, CmpShader);	SORTZ(44);	SORT(35, CmpShader);	SORTZ(40);
	// retail: SortGroup_78_80_79_81 0x4593c0
	SORT(78, CmpKeyThenMaterial);	SORTZ(80);
	SORT(79, CmpKeyThenMaterial);	SORTZ(81);
	// retail: SortGroup_4_3_18_17_75 0x4592c0
	SORT(4, CmpKeyThenMaterial);	SORT(3, CmpKeyThenMaterial);
	SORT(18, CmpKeyThenMaterial);	SORT(17, CmpKeyThenMaterial);
	SORT(75, CmpShader);
	// retail: SortGroup_7_8_77 0x459500
	SORT(7, CmpShader);	SORT(8, CmpShader);	SORT(77, CmpShader);
	SORT(9, CmpShader);	SORT(10, CmpShader);
	SORT(67, CmpShader);	SORT(68, CmpShader);
	SORTALWAYS(69, CmpKeyThenMaterial);
	SORTALWAYS(82, CmpKeyThenMaterial);
	// retail: SortGroup_52_53_56_55 0x4596f0
	SORT(52, CmpShader);	SORT(53, CmpShader);	SORTZ(56);	SORTZ(55);
	SORT(49, CmpShader);
	SORTZ(51);
	SORT(0, CmpKeyThenMaterial);	SORT(1, CmpKeyThenMaterial);
	// retail: SortGroup_5_6_19_20 0x459350
	SORT(5, CmpKeyThenMaterial);	SORT(6, CmpKeyThenMaterial);
	SORT(19, CmpKeyThenMaterial);	SORT(20, CmpKeyThenMaterial);
	SORT(2, CmpShader);
	// retail: SortGroup_29_50 0x459460
	SORTZ(29);	SORTZ(50);
	SORTZALWAYS(71);	SORTZALWAYS(70);	SORTZALWAYS(83);
	// retail gates these two on the environment manager's night flag
	SORT(11, CmpShader);	SORT(12, CmpShader);
	SORTZ(54);
	// retail: SortGroup_37_38 0x4594b0
	SORTZ(37);	SORTZ(38);
	// retail: SortGroup_30_39_24 0x459260
	SORT(30, CmpShader);	SORT(39, CmpShader);	SORT(24, CmpShader);
	SORTZALWAYS(65);
	ComputeDepthKeysList66();	SortList(lists[66], CmpKeyThenDepth);	listDirty[66] = false;
	// retail then prepares the three RenderManager-owned pools and rebuilds the
	// occluder set: occlude::Build(GetCurrentCamera(), 150.0f, ...) 0x461270
}

#undef SORT
#undef SORTZ
#undef SORTALWAYS
#undef SORTZALWAYS


// ---------------------------------------------------------------- the list walks

// retail: renderer::Display_List::RenderList 0x459910 --- the only generic walker (lists
// 19, 49, 51, 52, 53, 54, 55, 56, 75, 78, 79, 80, 81) and the only one that does not cull.
void
Display_List::RenderList(i32 listID, bool applyContainerFade)
{
if(nlists < (int)nelem(listorder)) listorder[nlists++] = listID;
displistsize[listID] = lists[listID].length;
if(!displistvisible[listID])
return;
	for(ListLink<DisplayListNode> *link = lists[listID].anchor.next; link; link = link->next) {
		DisplayListNode *nd = (DisplayListNode*)link;
		DrawablePrimitive *prim = nd->elem->prim;
		bool unlit = !prim->IsLit() && !prim->IsALUM();
		if(applyContainerFade) prim->SetFade(nd->container->GetFadeAmount());
		if(unlit) prim->SetTint(nd->container->GetTint());
		// retail: PushMatrix(0); MultMatrix(0, &nd->matrix) --- the stack top is the
		// transform of the pass (identity in Render(), the mirror in RenderReflection),
		// here it is the viewer's x flip.
		context->PushWorldMatrix();
		context->MultWorldMatrix(nd->matrix);
		nd->elem->Display();
		context->PopWorldMatrix();
		if(unlit) prim->SetTint(1.0f);
		if(applyContainerFade) prim->SetFade(0.0f);
	}
}

// The shape all ~35 specialised renderers share (notes/displaylist.md §4): they are the
// same loop, open-coded per list so that the pddi state and the shader-mode extension
// calls can be hoisted out of it. Everything that differs between them lives in the
// callers below.
void
Display_List::RenderCulledList(i32 listID, bool applyContainerFade)
{
if(nlists < (int)nelem(listorder)) listorder[nlists++] = listID;
displistsize[listID] = lists[listID].length;
if(!displistvisible[listID])
return;
	if(lists[listID].anchor.next == nil)
		return;
	Camera *cam = View_GetCullingCamera();
	for(ListLink<DisplayListNode> *link = lists[listID].anchor.next; link; link = link->next) {
		DisplayListNode *nd = (DisplayListNode*)link;
		if(!IsNodeVisible(cam, nd->elem, &nd->matrix))
			continue;
		DrawablePrimitive *prim = nd->elem->prim;
		if(applyContainerFade) prim->SetFade(nd->container->GetFadeAmount());
		context->PushWorldMatrix();
		context->MultWorldMatrix(nd->matrix);
		prim->Display();
		context->PopWorldMatrix();
		if(applyContainerFade) prim->SetFade(0.0f);
	}
}


// ---------------------------------------------------------------- the list renderers
//
// The pddi state in these is the SetZWrite / SetColourWrite / fog / stencil bracketing of
// notes/displaylist.md §1; the ext(0x10b) shader-mode pairs and the instancing extension
// are the fixed-function multi-pass trickery of the 2006 D3D8 path and are left out.

// retail: 0x459a00. Drawn first, z-test and z-write off, fog off, and translated to the
// rendering camera's x/z --- with y left at 0 --- so the sky box turns with the camera
// but does not rise and fall with it. 46 is the clear sky, 47 the rainy one, and only
// 47 gets the container fade, which is the cross-fade between the two.
// The translation goes on the stack BEFORE the node matrix, i.e. it is applied to the
// vertex after it, in the space the pass transform maps from: the viewer's x flip is
// that pass transform, so the camera position here is the native one, exactly like the
// node matrices (renderer/README.md, "Coordinates in the viewer").
// Neither list is culled: retail walks them straight through.
void
Display_List::RenderTranslatedList(i32 listID, const Matrix &translate, bool applyContainerFade)
{
if(nlists < (int)nelem(listorder)) listorder[nlists++] = listID;
displistsize[listID] = lists[listID].length;
if(!displistvisible[listID])
return;
	for(ListLink<DisplayListNode> *link = lists[listID].anchor.next; link; link = link->next) {
		DisplayListNode *nd = (DisplayListNode*)link;
		DrawablePrimitive *prim = nd->elem->prim;
		if(applyContainerFade) prim->SetFade(nd->container->GetFadeAmount());
		context->PushWorldMatrix();
		context->MultWorldMatrix(translate);
		context->MultWorldMatrix(nd->matrix);
		prim->Display();
		context->PopWorldMatrix();
		if(applyContainerFade) prim->SetFade(0.0f);
	}
}

void
Display_List::RenderSky(void)
{
	Vector camPos;
	View_GetRenderingCamera()->GetPosition(&camPos);
	Matrix translate;
	translate.Identity();
	translate.SetPosition(Vector(camPos.x, 0.0f, camPos.z));

	bool fog = context->IsFogEnabled();
	context->EnableFog(false);
	context->SetZTest(false);
	context->SetZWrite(false);
	RenderTranslatedList(46, translate, false);
	RenderTranslatedList(47, translate, true);
	context->SetZWrite(true);
	context->SetZTest(true);
	context->EnableFog(fog);
}

void Display_List::RenderLowLOD59(void)			{ RenderCulledList(59, false); }		// retail: 0x45d140
void Display_List::RenderUnlitBlendAndLayered_36_42(void) { RenderCulledList(36, false); RenderCulledList(42, false); }	// retail: 0x45ccd0
void Display_List::RenderInstanced73(void)		{ RenderCulledList(73, false); }		// retail: 0x45a680 (two passes with the instancing extension)
void Display_List::RenderInstanced74(void)		{ RenderCulledList(74, false); }		// retail: 0x45a620
void Display_List::RenderInstanced72(void)		{ RenderCulledList(72, false); }		// retail: 0x45a4c0 (three passes)

// retail: 0x45c070, called with fadingFirst=false from the opaque pass and true from the
// fading pass
void
Display_List::RenderCbvAlphaTest_26_22(bool fadingFirst)
{
	if(fadingFirst) {
		RenderCulledList(26, true);
		RenderCulledList(22, false);
	} else {
		RenderCulledList(26, false);
		RenderCulledList(22, false);
	}
}

// retail: 0x45c800
void
Display_List::RenderOpaqueBuildings_21_27_28_35_43(void)
{
	RenderCulledList(21, false);
	RenderCbvAlphaTest_26_22(false);
	RenderCulledList(27, false);
	RenderCulledList(28, false);
	RenderCulledList(35, false);
	RenderCulledList(43, false);
}

// retail: 0x45dcf0 --- stencil-tested, z-write off, alpha-write off
void
Display_List::RenderEnv_9_10(void)
{
	// retail turns the fog back on with a hard-coded EnableFog(true) here (0x45e902),
	// unlike the sky and list 76, which restore the saved flag
	context->EnableFog(false);
	context->SetZWrite(false);
	context->SetColourWrite(true, true, true, false);
	RenderCulledList(9, false);
	RenderCulledList(10, true);
	context->SetColourWrite(true, true, true, true);
	context->SetZWrite(true);
	context->EnableFog(true);
}

void Display_List::RenderSpecular_13_15(void)		{ RenderCulledList(13, false); RenderCulledList(15, false); }	// retail: 0x45deb0
void Display_List::RenderUnderwater_78_79(void)		{ RenderList(78, false); RenderList(79, false); }		// retail: 0x45ac10

// retail: 0x45b590 --- decals, z-write off (the bracket is in Render()), fog off around
// 18 and 4
void
Display_List::RenderDecals_3_17_18_4_75(void)
{
	RenderCulledList(3, false);
	RenderCulledList(17, false);
	context->EnableFog(false);
	RenderCulledList(18, false);
	context->EnableFog(true);

	context->EnableFog(false);
	RenderCulledList(4, false);
	context->EnableFog(true);
	RenderList(75, true);
}

void Display_List::RenderInteriorFloors_33_34(void)	{ RenderCulledList(33, false); RenderCulledList(34, true); }	// retail: 0x45cfb0
void Display_List::RenderProjectedShadows62(void)	{ RenderCulledList(62, false); }				// retail: 0x45bce0

// retail: 0x45caf0 --- the static shadow decals: the dark blobs the artists baked into
// the ground geometry under trees, awnings and walls, as prim groups whose pddi shader is
// "shadowdecal" (layer 37 -> list 7, or 8 while their world geo cross-fades, plus the
// layer-1 case in 77). z-write off for the whole function, per-node culling for 7 and 8,
// the container fade applied to 8 and 77.
//
// Retail wraps the whole pass in pddiExtStaticShadowGen::Begin/End (extension 0x108) and
// draws it into a screen-sized A8R8G8B8 render target with colour write = alpha only;
// End() then multiplies the frame by that alpha with one full-screen quad
// (SRCBLEND ZERO, DESTBLEND SRCALPHA). See re/notes/shadows.md §2.
//
// NOT retail (the one deviation): we have no render target to accumulate a mask in, so
// the decals blend straight onto the ground with their own alpha. For a single decal
// layer that is the same arithmetic --- dest *= (1-a) is mix(dest, black, a) --- and the
// decal textures are exactly that: black shapes with alpha = coverage. What the mask
// buys retail is that overlapping decals do not darken twice and that nothing drawn
// later in the frame can paint over them.
//
// Retail also gates the pass on g[0x7bfb55] (1 in the retail image) and on lists 7/8
// being non-empty, because Begin/End are not free; here the pass costs nothing when the
// lists are empty and the View tab's per-list switches do the gating.
void
Display_List::RenderShadowDecals_7_8_77(void)
{
	context->SetZWrite(false);
	RenderCulledList(7, false);
	RenderCulledList(8, true);
	RenderList(77, true);
	context->SetZWrite(true);
}

// retail: 0x45aa50 / 0x45a890 / 0x45a770 / 0x45ab20 / 0x45a7f0 / 0x45a910 / 0x45a9b0 ---
// all the same shape: per node, SelectLightSet(node->parent->owner, indoors) (0x45fb30)
// and then the draw. We have no light sets.
void Display_List::RenderLightSet67(void)		{ RenderCulledList(67, false); }
void Display_List::RenderLightSet69(void)		{ RenderCulledList(69, false); }
void Display_List::RenderLightSet82(void)		{ RenderCulledList(82, false); }
void Display_List::RenderLightSet68(void)		{ RenderCulledList(68, true); }
void Display_List::RenderLightSet83(void)		{ RenderCulledList(83, true); }
void Display_List::RenderLightSet71(void)		{ RenderCulledList(71, true); }
void Display_List::RenderLightSet70(void)		{ RenderCulledList(70, true); }

// retail: 0x459be0 --- a pure z-fill, colour write completely off
void
Display_List::RenderDepthOnly58(void)
{
	context->SetColourWrite(false, false, false, false);
	RenderCulledList(58, false);
	context->SetColourWrite(true, true, true, true);
}

void Display_List::RenderVertexFade2(void)		{ RenderCulledList(2, false); }					// retail: 0x45d1f0
void Display_List::RenderSpecularFading_14_16(void)	{ RenderCulledList(14, true); RenderCulledList(16, true); }	// retail: 0x45e030

// retail: 0x45b870 --- the fading decals; fog off around 20 and 6
void
Display_List::RenderDecalsFading_5_19_20_6(void)
{
	RenderCulledList(5, true);
	RenderList(19, true);
	context->EnableFog(false);
	RenderCulledList(20, true);
	context->EnableFog(true);

	context->EnableFog(false);
	RenderCulledList(6, true);
	context->EnableFog(true);
}

// retail: 0x45db80 --- night lights, fog forced off
void
Display_List::RenderNightLights11(void)
{
	context->EnableFog(false);
	RenderCulledList(11, true);
	context->EnableFog(true);
}
// retail: 0x45da30
void Display_List::RenderCardsNight12(void)		{ RenderCulledList(12, true); }

// retail: 0x45e240
void
Display_List::RenderFadingBlend_44_25_32_31_40(void)
{
	RenderCulledList(44, true);
	RenderCulledList(25, true);
	RenderCbvAlphaTest_26_22(true);
	RenderCulledList(32, true);
	RenderCulledList(31, true);
	RenderCulledList(40, true);
}

void Display_List::RenderUnderwaterFading_80_81(void)	{ RenderList(80, true); RenderList(81, true); }	// retail: 0x45ac90
void Display_List::RenderCbvDefault23(void)		{ RenderCulledList(23, true); }			// retail: 0x45c280
void Display_List::RenderLit_29_50(void)		{ RenderCulledList(29, false); RenderList(50, true); }	// retail: 0x45bf30

// retail: 0x45a010 --- the stencil shadow volumes, tinted with shadowVolumeColour
void
Display_List::RenderShadowVolumes_61_63_64(void)
{
	RenderCulledList(61, false);
	RenderCulledList(63, false);
	RenderCulledList(64, false);
}

// retail: 0x45b240 --- the second specular pass over the same two lists
void Display_List::RenderSpecularPass2_15_13(void)	{ RenderCulledList(15, false); RenderCulledList(13, false); }

void Display_List::RenderFading_41_45(void)		{ RenderCulledList(41, true); RenderCulledList(45, true); }	// retail: 0x45ce00
void Display_List::RenderUnlit_38_37(void)		{ RenderCulledList(38, true); RenderCulledList(37, true); }	// retail: 0x45c3c0

// retail: 0x45c590 --- additive/blend-add (neons, tunnel lights), z-write off around the
// last three lists in Render()
void
Display_List::RenderAdditive_39_30_24(void)
{
	RenderCulledList(39, true);
	RenderCulledList(30, true);
	RenderCulledList(24, true);
}

void Display_List::RenderWater65(void)			{ RenderCulledList(65, true); }		// retail: 0x459c80
void Display_List::RenderWaterSurface66(void)		{ RenderList(66, true); }		// retail: 0x459f70

// retail: 0x459810 --- camera-locked, fog off, matrix translated to the camera
// retail: 0x459810 --- the same camera-locked walk as RenderSky, but translated to the
// camera's FULL position (y included), so the sun, the flares and the stars sit at
// infinity. Drawn near the end of the frame with fog off.
void
Display_List::RenderCameraLocked76(void)
{
	Vector camPos;
	View_GetRenderingCamera()->GetPosition(&camPos);
	Matrix translate;
	translate.Identity();
	translate.SetPosition(camPos);
	bool fog = context->IsFogEnabled();
	context->EnableFog(false);
	RenderTranslatedList(76, translate, true);
	context->EnableFog(fog);
}


// ---- group (B): drawn here when the camera is outside, last when it is inside -------

void
Display_List::RenderOutdoor_36_42_73_52_53(void)
{
	RenderUnlitBlendAndLayered_36_42();
	RenderInstanced73();
	RenderList(52, false);
	// retail then fills the whole frame buffer's alpha channel with 1 through the quad
	// clear path (SetColourWrite(0,0,0,1); SetClearColour(0x01000000); Clear(COLOUR)) and
	// protects it with SetColourWrite(1,1,1,0) in almost every later pass. Our backend
	// does not use the frame buffer alpha as a mask, so the clear is left out.
	RenderList(53, false);
}

void
Display_List::RenderOutdoor_Decals_Floors_Shadows(void)
{
	context->SetZWrite(false);
	RenderDecals_3_17_18_4_75();
	context->SetZWrite(true);
	RenderInteriorFloors_33_34();
	RenderProjectedShadows62();
}

void
Display_List::RenderOutdoor_49_67_69_82(void)
{
	RenderList(49, false);
	RenderLightSet67();
	RenderLightSet69();
	RenderLightSet82();
}

void Display_List::RenderOutdoor_54(void)	{ RenderList(54, true); }
void Display_List::RenderOutdoor_68_83(void)	{ RenderLightSet68(); RenderLightSet83(); }

void
Display_List::RenderOutdoorGroups(void)
{
	RenderOutdoor_36_42_73_52_53();
	RenderOutdoor_Decals_Floors_Shadows();
	RenderOutdoor_49_67_69_82();
	RenderOutdoor_54();
	RenderOutdoor_68_83();
}


// ---------------------------------------------------------------- the frame

// retail: renderer::Display_List::Render 0x45e680 (vslot 9), ~110 list walks in a fixed
// order. The numbered groups are the ones in notes/displaylist.md §1.
void
Display_List::Render(void)
{
nlists = 0;
	bool indoors = cameraIndoors;

	// 1. sky, z-test and z-write off, fog off
	RenderSky();
	// 2. low-LOD skyline
	RenderLowLOD59();
	// 3. (B) unlit blend / layered, instanced, the two unclassified opaque lists
	if(!indoors) RenderOutdoor_36_42_73_52_53();
	// 4. the opaque building buckets
	RenderOpaqueBuildings_21_27_28_35_43();
	// 5. instanced
	RenderInstanced74();
	// 6. stencil on, alpha-write off, fog off: env / reflection
	RenderEnv_9_10();
	// 7. specular road/ground
	RenderSpecular_13_15();
	// 8. underwater
	RenderUnderwater_78_79();
	// 9. (B) decals (z-write off), interior floors, projected shadows
	if(!indoors) RenderOutdoor_Decals_Floors_Shadows();
	// 10. shadow decals, gated on the StaticShadowGen extension in retail
	RenderShadowDecals_7_8_77();
	// 11. (B) the lit world and the light-set lists
	if(!indoors) RenderOutdoor_49_67_69_82();
	// 12. the player shadow z-fill and the one singleton of list 60
	RenderDepthOnly58();
	RenderCulledList(60, false);	// retail draws only lists[60].head, the one singleton
	// 13. vertex fade, fading specular, fading decals (z-write off)
	RenderVertexFade2();
	RenderSpecularFading_14_16();
	context->SetZWrite(false);
	RenderDecalsFading_5_19_20_6();
	context->SetZWrite(true);
	// 14. night lights, z-write off, fog forced off
	context->SetZWrite(false);
	RenderNightLights11();
	context->SetZWrite(true);
	// 15. instanced eco props
	RenderInstanced72();
	// 16. the fading blend buckets
	RenderFadingBlend_44_25_32_31_40();
	RenderUnderwaterFading_80_81();
	RenderCbvDefault23();
	RenderLit_29_50();
	// 17. cards-night, z-write off
	context->SetZWrite(false);
	RenderCardsNight12();
	context->SetZWrite(true);
	// 18. (B) the unclassified fading world
	if(!indoors) RenderOutdoor_54();
	RenderList(51, true);
	// 19. retail draws the skid marks here (renderMgr->skidmarks->RenderImmediate())
	//     between two SetColourWrite calls that protect the alpha mask
	// 20. (B)
	if(!indoors) RenderOutdoor_68_83();
	// 21. the last light set, the stencil shadow volumes, the second specular pass
	RenderLightSet71();
	RenderShadowVolumes_61_63_64();
	RenderSpecularPass2_15_13();
	// 22. when the camera is indoors the whole outside world is drawn here instead
	if(indoors) RenderOutdoorGroups();
	// 23. the remaining blend buckets; z-write off for the additive ones
	RenderFading_41_45();
	RenderUnlit_38_37();
	context->SetZWrite(false);
	RenderAdditive_39_30_24();
	context->SetZWrite(true);
	// 24. retail draws the decal pool here, then the water
	RenderWater65();
	RenderLightSet70();
	RenderWaterSurface66();
	// retail then draws the tracer pool with z-write off
	// 25. the rest of the unclassified fading world
	RenderList(55, true);
	RenderList(56, true);
	// 26. camera-locked, fog off
	RenderCameraLocked76();
	// 27. the ext(0x103) post-process
	// 28. recycle the immediate-mode nodes and measure the frame
	FreeOrphanNodes();
}

}
