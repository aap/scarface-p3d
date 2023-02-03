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

	DrawablePrimitive(void) : layer(0) {}

	virtual u32 GetSomeMask(void) = 0;
	virtual void Display(void) = 0;
	virtual Shader *GetShader(void) const = 0;
	virtual void SetShader(Shader *shader) = 0;
	virtual bool IsLit(void) = 0;
	virtual bool IsALUM(void) = 0;
	virtual void UpdateBounds(void) {}
	// SetFade
	// SetCTNT
	// unknown

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
	virtual float GetFadeAmount(void) { return 0.0f; }
	// 4 virtuals
};

class DrawableContainer : public DrawableHierarchy
{
	// float unknown
	DrawableHierarchy *parent;
	Array<PrimEntry> prims;
public:
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
