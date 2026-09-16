#pragma once

#include "../list.h"
#include "../displaylist.h"
#include "view.h"

namespace renderer
{

using namespace pure3d;

class Renderable;
struct DisplayListNode;

void SetPrimLayerByShader(DrawablePrimitive *prim, bool &flag1);

// P3D_VERBOSE: the per-renderable "explain your visibility decision" prints are
// meant to be one dump, not a stream; nothing sets this yet
extern bool debugPrinted;

// retail: renderer::DisplayListPrimitive : GameDrawableInfo, vtable 0x00737704,
// 0x20 bytes, ctor 0x458c60. One per (renderable, drawable) pair. It owns the nodes it
// put into the Display_List (they are linked into `children` through Node::linkParent)
// and it is the only thing that ever talks to the display list.
class DisplayListPrimitive : public GameDrawableInfo
{
	Renderable *owner;		// +0x08, read back as node->parent->owner
	DrawableHierarchy *drawable;	// +0x0c, ref-counted
	const Matrix *instanceMatrix;	// not retail: per-placement transform (eco props)
	LinkedList<DisplayListNode> children;	// +0x10, of Node::linkParent
	// +0x1c
	bool isVisible : 1;		// 1
	bool isInList : 1;		// 2  some of my nodes are in the display list
	bool checkDrawable : 1;		// 4  re-check the drawable every frame
public:
	CLASSNAME(DisplayListPrimitive);
	DisplayListPrimitive(void);
	void SetParent(Renderable *renderable) { owner = renderable; }
	Renderable *GetOwner(void) { return owner; }
	// retail: renderer::DisplayListPrimitive::SetDrawable 0x458e50
	void SetDrawable(DrawableHierarchy *drawable, bool checkDrawable);
	DrawableHierarchy *GetDrawable(void) { return drawable; }
	void SetInstanceMatrix(const Matrix *m) { instanceMatrix = m; }
	const Matrix *GetInstanceMatrix(void) { return instanceMatrix; }
	bool IsInList(void) { return isInList; }
	// retail: renderer::DisplayListPrimitive::RemoveFromList 0x458ea0 --- the nodes
	// cache the world matrix, so every matrix or fade change has to force a re-submit
	void RemoveFromList(void);
	// retail: renderer::DisplayListPrimitive::SetVisible 0x458ec0 --- only ever REMOVES
	void SetVisible(bool visible);
	// retail: renderer::DisplayListPrimitive::Display 0x458f00 --- edge triggered: it
	// submits when it becomes visible and withdraws when it stops being visible, so a
	// static, continuously visible object is submitted once, ever
	void Display(bool visible);

	friend class Display_List;
};

// retail: renderer::DisplayListElement, 0x30 bytes
struct DisplayListElement
{
	float drawDistMin;		// +0x00
	float drawDistMax;		// +0x04
	float drawDistFade;		// +0x08  the width of the cross-fade band
	DisplayListPrimitive prim;	// +0x0c
	bool isFading;			// +0x2c
};

// retail: renderer::Renderable : pure3d::Entity, vtable 0x0073838c (16 slots),
// ctor 0x474ba0, base size 0x84. A world matrix plus N (drawable, draw-distance) slots.
// It never draws: once a frame it decides visible/faded and pushes the surviving
// drawables into the Display_List.
class Renderable : public Entity
{
public:
	enum {
		CHARACTER_LOADER	= 0x8800000,
		VEHICLE_LOADER		= 0x8800001,
		SKY_LOADER		= 0x8800002,
		WORLDGEO_LOADER		= 0x8800003,
		ZONEPKG_LOADER		= 0x8800004,
		SFSTATEPROP_LOADER	= 0x8800005,
		SFLIGHTGROUP_LOADER	= 0x8800007,
		SHADOW_LOADER		= 0x8800008,
		OCCLUDER_LOADER		= 0x880000A
	};

	// retail: Renderable::typeMask (+0x54), one bit per class
	// (notes/renderable_classes.md §3.1)
	enum {
		TYPE_SKY		= 0x00000001,
		TYPE_TRACEFIRE		= 0x00000002,
		TYPE_PARTICLEEFFECT	= 0x00000004,
		TYPE_WORLDGEO		= 0x00000008,	// also PropRenderable
		TYPE_STATEPROP		= 0x00000010,
		TYPE_CHARACTER		= 0x00000020,
		TYPE_OCEAN		= 0x00000040,
		TYPE_WAKE		= 0x00000080,
		TYPE_SHADOW		= 0x00000100,
		TYPE_INSTANCE		= 0x00000200,
		TYPE_LIGHTING		= 0x00000400,
		TYPE_RAIN		= 0x00000800,
		TYPE_MASK		= 0x00001000,
		TYPE_DECAL		= 0x00002000,
		TYPE_VEHICLE		= 0x00004000,
		TYPE_ZONEPKG		= 0x00008000,
		TYPE_NIS		= 0x00020000,
		TYPE_PLUGIN		= 0x00040000,
		TYPE_SKIDMARK		= 0x00100000
	};

	Matrix matrix;				// +0x14, row 3 is the position
	i32 typeMask;				// +0x54
	Array<DisplayListElement> elements;	// +0x58/+0x5c, stride 0x30
	u32 uniqueId;				// +0x64, the generation counter of the handles
	float fadeTime;				// +0x68, NOT a distance: UpdateFade steps by 1000/fadeTime
	float fade;				// +0x6c, 0 = opaque, 1 = gone
	float fade2;				// +0x70, a second channel, max()'d with fade
	float fadeTarget;			// +0x74
	float timeSinceDrawn;			// +0x78
	i32 sceneId;				// +0x7c, written by Scene::AddRenderable
	// +0x80
	bool doDistanceTest : 1;		// 0x01
	bool doFade : 1;			// 0x02
	bool fadeDirectionOut : 1;		// 0x04
	bool hasHandle : 1;			// 0x08
	bool isVisible : 1;			// 0x10
	bool statePropFitsInPool : 1;		// 0x20
	bool shareLastElementFarDist : 1;	// 0x40, multi-element LOD chains
	bool useBoxBoundsFromPose : 1;		// 0x80, state props
	// +0x81
	bool isInsideRoom : 1;			// 0x01, maintained by Renderable::UpdateRoom
	bool isMatrixDirty : 1;			// 0x02
	// not retail: measure the draw distance to the reference point, not to the
	// bounding sphere (low_LOD_ world geo, whose sphere covers the whole city)
	bool distanceToRefPoint : 1;

	Renderable(i32 numElements);

	void SetNumElements(i32 n);			// retail: 0x474ac0
	i32 GetNumElements(void) { return elements.Size(); }	// retail: 0x473fe0
	void SetElement(DrawableHierarchy *drawable, i32 i, bool checkDrawable);	// retail: 0x473e70
	void SetElementDrawDist(i32 i, float min, float max, float fade);	// retail: 0x473f00
	DrawableHierarchy *GetElementDrawable(i32 i);	// retail: 0x473fc0

	// retail: 0x473aa0 / 0x473ac0. The argument is a TIME IN MS, not a distance:
	// SetFadeDist(3000) means "fade in over 3 s", SetFadeDist(0) means "appear now"
	// (the division by zero completes the fade on the first tick).
	void SetFadeDist(float timeMs);
	void SetToFadeOut(float timeMs);
	void SetFade2(float f);				// retail: 0x473d40
	float UpdateFade(float dt);			// retail: 0x473d90
	void Reset(void);				// retail: 0x473b70
	// retail: Renderable::Tick 0x4740c0 --- NOT virtual, GamePlayScene::Update calls it
	// on every renderable, even the hidden ones
	void Tick(TimeInfo *t);

	virtual void SetVisible(bool visible);		// vslot 8,  retail: 0x473c20
	virtual void Update(TimeInfo *t);		// vslot 9,  base is a stub
	virtual void Display(void);			// vslot 10, retail: 0x4740f0
	virtual void RenderImmediate(void);		// vslot 11, the non-gameplay scenes
	virtual void SetMatrix(const Matrix &m);	// vslot 12, retail: 0x473b40
	virtual void GetPosition(Vector *p);		// vslot 13, retail: 0x473c00
	virtual void Hide(void);			// vslot 14, retail: 0x473c80
	// vslot 15, retail: 0x559000 (returns false); the hook for "measure my distance
	// from somewhere other than my origin". Only WorldGeoRenderable overrides it.
	virtual bool GetDistanceRefPos(Vector *p);
};

}
