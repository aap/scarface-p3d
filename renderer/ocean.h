#pragma once

#include "renderable.h"
#include "../drawable.h"
#include "../ocean.h"
#include "../inventory.h"

namespace renderer
{

using namespace pure3d;
using namespace content;

// retail: renderer::OceanPrimitive : pure3d::DrawablePrimitive, vtable 0x0073807c,
// ctor 0x00470820, 0x48 bytes (re/notes/renderable_classes.md §5.14, re/notes/ocean.md).
//	layer = 27          -> Display_List list 60, drawn in group 12 with z-write on
//	                       and fog ON, after the lit world and before the decals
//	+0x38               the pure3d ocean object (ctor 0x689af0)
//	+0x3c/+0x40/+0x44   the three AddRef'd arguments of _CreateInstance
//	vslot 8  0x4707a0   Display() -> if(+0x38) jmp 0x689aa0
//	vslot 13 0x4707c0   CalcBounds(): sphere = the camera, radius 100000 --- the ocean
//	                    follows the camera and is never frustum culled
class OceanPrimitive : public DrawablePrimitive
{
	Ocean *ocean;
public:
	CLASSNAME(OceanPrimitive)

	OceanPrimitive(Ocean *ocean);
	~OceanPrimitive(void);

	Ocean *GetOcean(void) { return ocean; }

	// retail: OceanPrimitive::GetTypeMask 0x4707b0 --- 0x00020000
	virtual u32 GetSomeMask(void) { return 0x00020000; }
	virtual void Display(void);			// retail: 0x4707a0
	virtual Shader *GetShader(void) const;
	virtual void SetShader(Shader *sh);
	virtual bool IsLit(void) { return false; }
	virtual bool IsALUM(void);
	virtual void UpdateBounds(void);		// retail: CalcBounds 0x4707c0
};

// retail: renderer::OceanContainer : pure3d::DrawableContainer, vtable 0x00737ffc,
// ctor 0x00470a40, 0x50 bytes. One element, the OceanPrimitive; sortKey 0.5 (list 60 is
// never sorted, so it only ever reaches Node::sortKey).
class OceanContainer : public DrawableContainer
{
public:
	CLASSNAME(OceanContainer)

	OceanContainer(Ocean *ocean);

	// DrawableContainer vslot 10 is j_DrawableContainer::Display (0x004707f0 ->
	// 0x6834c0): the normal path, push the element into the Display_List
	virtual void Display(DisplayList *list, GameDrawableInfo *info) { DrawPrimitives(list, info); }
	virtual void CalcBounds(void);

	OceanPrimitive *GetPrimitive(void) { return (OceanPrimitive*)GetElement(0)->prim; }
};

// retail: renderer::OceanRenderable : Renderable, vtable 0x007380c4, ctor 0x00470ad0,
// 0x88 bytes, typeMask 0x40 (TYPE_OCEAN). The ctor clears doDistanceTest AND doFade:
// the ocean is always drawn, at full opacity.
class OceanRenderable : public Renderable
{
public:
	CLASSNAME(OceanRenderable)

	OceanContainer *container;	// +0x84

	OceanRenderable(Ocean *ocean);
	~OceanRenderable(void);

	Ocean *GetOcean(void) { return container->GetPrimitive()->GetOcean(); }

	virtual void Update(TimeInfo *t);		// retail: 0x470be0
	// retail: vslot 10 is a thunk to the base (0x470810); the g_oceanEnabled test is
	// ours, in the spirit of SkyRenderable::Display
	virtual void Display(void);
	virtual void SetMatrix(const Matrix &m) {}	// retail: vslot 12 = 0x438400, a stub
};

// retail: renderer::OceanRenderable_CreateInstance 0x00466940 --- looks the three
// textures up in the inventory, builds the renderable and names it "oceanrenderable"
// (the string at 0x737904). The game calls it out of OceanObject::OnInit with the
// three texture names of the OceanTemplate.
OceanRenderable *OceanRenderable_CreateInstance(const char *reflectionTextureName,
                                                const char *detailTextureName,
                                                const char *foamTextureName,
                                                LoadInventory *inventory);

// retail: the ocean is a singleton; the viewer keeps it here so the Explorer can find it
extern OceanRenderable *gOcean;
// not retail: the View tab's on/off switch, in the spirit of renderer::g_skyEnabled
extern bool g_oceanEnabled;

}
