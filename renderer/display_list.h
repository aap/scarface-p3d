#pragma once

#include "../list.h"
#include "../displaylist.h"
#include "view.h"

namespace occlude
{

// retail: occlude::IsBoxVisible 0x458bf0, over the g_occluders array (0x810d10,
// count g[0x810d04]) built by occlude::Build (0x461270) at the end of SortAllLists.
// We load no 0x0880000a occluder chunks yet, so nothing is ever occluded; the hook
// is here because Display_List::IsNodeVisible is the only caller and it should read
// like retail.
bool IsBoxVisible(const math::Box3D &box);

}

namespace renderer
{

using namespace pure3d;

class DisplayListPrimitive;

// retail: renderer::Display_List::Node, stride 0x70. All the nodes live in one flat
// array allocated by the ctor; submitting = unlink from the free list and link into
// lists[listID], un-submitting = the reverse. Nothing is allocated per frame, and the
// world matrix is baked in at submit time (which is why every fade or matrix change
// has to call DisplayListPrimitive::RemoveFromList).
struct DisplayListNode
{
	ListLink<DisplayListNode> link;			// +0x00  in myList, or in freeList
	Matrix matrix;					// +0x08  world matrix at submit time
	float sortKey;					// +0x48  class key (container sortKey, or per layer)
	float sortKey2;					// +0x4c  view depth, written by ComputeDepthKeys
	PrimEntry *elem;				// +0x50  &container->elements[idx]
	DrawableContainer *container;			// +0x54
	Shader *shader;					// +0x58  the batching key
	DisplayListPrimitive *parent;			// +0x5c  nil == orphan
	LinkedList<DisplayListNode> *myList;		// +0x60  &lists[listID]
	ListLink<DisplayListNode> linkParent;		// +0x64  in parent->children
	DisplayListNode *self;				// +0x6c  == this, so linkParent can walk back
};

enum { NUM_DISPLAY_LISTS = 84 };

// retail: renderer::Display_List : pure3d::Entity, vtable 0x00737738, ctor 0x45f110,
// >= 0xcc bytes. See re/notes/displaylist.md.
class Display_List : public DisplayList
{
public:
	Array2<LinkedList<DisplayListNode>> lists;	// +0x30  [NUM_DISPLAY_LISTS]
	// +0x3c: NOT "has content" --- AddContainerElement sets it, SortAllLists sorts the
	// list and clears it again, so an untouched list is not re-sorted.
	bool listDirty[NUM_DISPLAY_LISTS];
	LinkedList<DisplayListNode> freeList;		// +0x90
	Array<DisplayListNode> nodeStore;		// +0x9c
	bool cameraInsideVolume;			// +0xaa  recomputed by SortAllLists
	// +0xc8, the tint RenderShadowVolumes_61_63_64 draws the stencil volumes with
	u32 shadowVolumeColour;
	// the one big conditional of Render(): when the camera is inside a building the
	// outside world is drawn after the interior. retail derives it from
	// renderMgr->env->IsCameraIndoors() (0x46a520) and two more tests; nothing sets
	// it here yet.
	bool cameraIndoors;

	CLASSNAME(Display_List);
	Display_List(i32 numNodes);

	// ---- submission (Entity vslots 7, 8) --------------------------------------
	// retail: renderer::Display_List::AddContainer 0x45d360
	virtual void AddContainer(DrawableContainer *container, Matrix *matrix, GameDrawableInfo *info);
	// retail: renderer::Display_List::AddContainerElement 0x45d3b0
	virtual void AddContainerElement(DrawableContainer *container, i32 idx, Matrix *matrix, GameDrawableInfo *info);
	// retail: renderer::Display_List::RemovePrimitiveNodes 0x458a20
	void RemovePrimitiveNodes(DisplayListPrimitive *prim);
	// retail: renderer::Display_List::FreeOrphanNodes 0x45b0c0
	void FreeOrphanNodes(void);

	// ---- culling ---------------------------------------------------------------
	// retail: renderer::Display_List::IsNodeVisible 0x45b110
	bool IsNodeVisible(Camera *cam, PrimEntry *elem, const Matrix *m);

	// ---- the sort pass ---------------------------------------------------------
	// retail: renderer::Display_List::SortAllLists 0x45ad10
	void SortAllLists(void);
	// retail: renderer::Display_List::ComputeDepthKeys 0x459120
	void ComputeDepthKeys(i32 listID);
	// retail: renderer::Display_List::ComputeDepthKeysList66 0x4591e0
	void ComputeDepthKeysList66(void);

	// ---- the frame (Entity vslot 9) --------------------------------------------
	// retail: renderer::Display_List::Render 0x45e680
	virtual void Render(void);
	// retail: renderer::Display_List::RenderList 0x459910 --- the only generic walker,
	// and the only one that does NOT cull
	void RenderList(i32 listID, bool applyContainerFade);
	// the shape all ~35 specialised list renderers share (notes/displaylist.md §4)
	void RenderCulledList(i32 listID, bool applyContainerFade);

	// ---- the named list renderers, in the order Render() calls them -------------
	void RenderSky(void);					// retail: 0x459a00  46, 47
	void RenderLowLOD59(void);				// retail: 0x45d140  59
	void RenderUnlitBlendAndLayered_36_42(void);		// retail: 0x45ccd0  36, 42
	void RenderInstanced73(void);				// retail: 0x45a680  73
	void RenderOpaqueBuildings_21_27_28_35_43(void);	// retail: 0x45c800  21, 26, 22, 27, 28, 35, 43
	void RenderCbvAlphaTest_26_22(bool fadingFirst);	// retail: 0x45c070  26, 22
	void RenderInstanced74(void);				// retail: 0x45a620  74
	void RenderEnv_9_10(void);				// retail: 0x45dcf0  9, 10
	void RenderSpecular_13_15(void);			// retail: 0x45deb0  13, 15
	void RenderUnderwater_78_79(void);			// retail: 0x45ac10  78, 79
	void RenderDecals_3_17_18_4_75(void);			// retail: 0x45b590  3, 17, 18, 4, 75
	void RenderInteriorFloors_33_34(void);			// retail: 0x45cfb0  33, 34
	void RenderProjectedShadows62(void);			// retail: 0x45bce0  62
	void RenderShadowDecals_7_8_77(void);			// retail: 0x45caf0  7, 8, 77
	void RenderLightSet67(void);				// retail: 0x45aa50  67
	void RenderLightSet69(void);				// retail: 0x45a890  69
	void RenderLightSet82(void);				// retail: 0x45a770  82
	void RenderDepthOnly58(void);				// retail: 0x459be0  58
	void RenderVertexFade2(void);				// retail: 0x45d1f0  2
	void RenderSpecularFading_14_16(void);			// retail: 0x45e030  14, 16
	void RenderDecalsFading_5_19_20_6(void);		// retail: 0x45b870  5, 19, 20, 6
	void RenderNightLights11(void);				// retail: 0x45db80  11
	void RenderInstanced72(void);				// retail: 0x45a4c0  72
	void RenderFadingBlend_44_25_32_31_40(void);		// retail: 0x45e240  44, 25, 26, 22, 32, 31, 40
	void RenderUnderwaterFading_80_81(void);		// retail: 0x45ac90  80, 81
	void RenderCbvDefault23(void);				// retail: 0x45c280  23
	void RenderLit_29_50(void);				// retail: 0x45bf30  29, 50
	void RenderCardsNight12(void);				// retail: 0x45da30  12
	void RenderLightSet68(void);				// retail: 0x45ab20  68
	void RenderLightSet83(void);				// retail: 0x45a7f0  83
	void RenderLightSet71(void);				// retail: 0x45a910  71
	void RenderShadowVolumes_61_63_64(void);		// retail: 0x45a010  61, 63, 64
	void RenderSpecularPass2_15_13(void);			// retail: 0x45b240  15, 13
	void RenderFading_41_45(void);				// retail: 0x45ce00  41, 45
	void RenderUnlit_38_37(void);				// retail: 0x45c3c0  38, 37
	void RenderAdditive_39_30_24(void);			// retail: 0x45c590  39, 30, 24
	void RenderWater65(void);				// retail: 0x45dc80 / 0x459c80  65
	void RenderLightSet70(void);				// retail: 0x45a9b0  70
	void RenderWaterSurface66(void);			// retail: 0x459f70  66
	void RenderCameraLocked76(void);			// retail: 0x459810  76

	// The five pieces of group (B) of Render(): emitted here when the camera is
	// outside and all together, in this order, at the end when it is inside.
	void RenderOutdoor_36_42_73_52_53(void);
	void RenderOutdoor_Decals_Floors_Shadows(void);
	void RenderOutdoor_49_67_69_82(void);
	void RenderOutdoor_54(void);
	void RenderOutdoor_68_83(void);
	void RenderOutdoorGroups(void);
};

// retail: g_displayList g[0x8108a0], written by renderer::SetGlobalDisplayList
// (0x4589d0), which GamePlayScene's ctor and dtor call.
extern Display_List *g_displayList;
void SetGlobalDisplayList(Display_List *list);

}
