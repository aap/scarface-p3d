#pragma once

#include "entity.h"
#include "gmath.h"
#include "array.h"

#include <stdio.h>

namespace pure3d
{

class Shader;

class DrawablePrimitive : public Entity
{
	u32 layer;
public:
	math::Box3D box;
	math::Sphere sphere;
	// retail: DrawablePrimitive+0x68, written by ShadowLoader on every primitive of a
	// shadow composite (re/notes/shadows.md §3.1). Display_List's layer-2 case reads it
	// to tell a building shadow from a car/NPC one.
	bool isBuildingShadow;

	DrawablePrimitive(void) : layer(0), isBuildingShadow(false) {}

	virtual u32 GetSomeMask(void) = 0;
	virtual void Display(void) = 0;
	virtual Shader *GetShader(void) const = 0;
	virtual void SetShader(Shader *shader) = 0;
	virtual bool IsLit(void) = 0;
	virtual bool IsALUM(void) = 0;
	virtual void UpdateBounds(void) {}
	// retail: DrawablePrimitive vslot 14 [+0x38] and vslot 15 [+0x3c]. Display_List
	// pushes the owning container's fade/tint into the primitive around the draw
	// (notes/displaylist.md §4). Nothing in the GL backend consumes them yet.
	virtual void SetFade(float fade) {}
	virtual void SetTint(float tint) {}
	// unknown

	// not retail: the triangles of this primitive, in the primitive's own space, so
	// that p3dview can pick a triangle instead of a bounding sphere
	// (p3dview/explorer.cpp). 0 for anything with no readable triangle list --- a
	// billboard quad group, the ocean's per-frame projected grid.
	virtual u32 GetNumTriangles(void) { return 0; }
	virtual bool GetTriangle(u32 i, math::Vector v[3]) { return false; }

	void SetLayer(u32 l) { layer = l; }
	u32 GetLayer(void) { return layer; }
};

// TODO: need better name for this
class PrimEntry
{
public:
	DrawablePrimitive *prim;
	Array<void*> frameControllers;	// TODO: use this
public:
	PrimEntry(void) : prim(nil) {}
	void Display(void);
	void SetPrimitive(DrawablePrimitive *primitive) {
		if(primitive) primitive->AddRef();
		if(prim) prim->Release();
		prim = primitive;
	}
};

class ShaderCallback : public NonCopyable
{
};

class DisplayList;
class GameDrawableInfo;

class DrawableHierarchy : public Entity
{
public:
	ShaderCallback *callback;
	math::Box3D box;
	math::Sphere sphere;
	bool isLit : 1;
	bool isALUM : 1;
	bool unknown : 1;
	bool alphaBlend : 1;

	DrawableHierarchy(void) : callback(nil), isLit(false), isALUM(false), unknown(false), alphaBlend(false) {}

	// 3 virtuals
	virtual void Display(DisplayList *list, GameDrawableInfo *info) = 0;
	// 1 virtual - some recursive update thing
	// SetPose
	// GetPose
	virtual void CalcBounds(void) = 0;
	virtual void SetShaderCallback(ShaderCallback *cb) { Assign(callback, cb); }
	virtual void ReleaseShaderCallback(void) { Release(callback); }
	virtual void SetFading(bool enable) {}
	virtual void SetFadeAmount(float fade) {}
	virtual bool IsFading(void) { return false; }
	// retail: DrawableContainer vslot 19/20/21 [+0x4c]/[+0x50]/[+0x54]
	virtual float GetFadeAmount(void) { return 0.0f; }
	virtual float GetTint(void) { return 1.0f; }
	// 4 virtuals
};

class DrawableContainer : public DrawableHierarchy
{
	DrawableHierarchy *parent;
	Array<PrimEntry> prims;
public:
	// retail: DrawableContainer +0x3c, the class key Display_List::AddContainerElement
	// copies into Node::sortKey (notes/renderable_classes.md §2). Observed in retail:
	// Decal 0.0, Skidmark 0.0, Ocean 0.5, Wake 0.89; the base ctor value is 0.5.
	float sortKey;

	DrawableContainer(i32 nPrim);
	void SetParent(DrawableHierarchy *p) { parent = p; }
	PrimEntry *GetElement(i32 n) { return &prims[n]; }
	i32 GetNumElements(void) { return prims.Size(); }

	virtual void CalcBounds(void);

	// 5 virtuals

	void DrawPrimitives(DisplayList *list, GameDrawableInfo *info);
	void SetFlags(void);
};


}
