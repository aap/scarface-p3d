#pragma once

#include "loadmanager.h"
#include "drawable.h"
#include "skeleton.h"

#include <assert.h>

namespace pure3d
{

using namespace core;
using namespace content;

class CompositeDrawable : public DrawableHierarchy
{
public:
	CLASSNAME(CompositeDrawable)

	class ActivePrimitive
	{
		DrawableContainer *drawable;
	public:
		i16 id;		// or unsigned?
		u8 isVisible : 1;

		ActivePrimitive(void) : drawable(nil), id(0), isVisible(1) {}
		void SetDrawable(DrawableContainer *draw) {
			if(draw)		// TODO: remove once we have all drawables
			draw->AddRef();		// no nil check?
			if(drawable) drawable->Release();
			drawable = draw;
		}
		DrawableContainer *GetDrawable(void) { return drawable; }
//		void SetID(int i) { id = i; }
//		void SetVisible(bool visible) { isVisible = isVisible; }
	};

	class ActivePrimitiveList : public NonCopyable
	{
	// these should be u8 and/or i8, but that's a bit too small?
/*
		u8 numPrims;
		Array2<i8> usageMap;	// same indices as primArray, maps to primMap
		Array2<i8> primMap;	// [numPrims], maps to primArray/usageMap
*/
		i32 numPrims;
		Array2<i32> usageMap;	// same indices as primArray, maps to primMap
		Array2<i32> primMap;	// [numPrims], maps to primArray/usageMap
		Array2<ActivePrimitive> primArray;
	public:
		CLASSNAME(ActivePrimitiveList)
		ActivePrimitiveList(i32 numPrims);
		u32 GetNumPrimitives(void) { return primArray.Size(); }
		ActivePrimitive *GetPrimitive(int i) { return &primArray[i]; }
		u32 GetNumUsedPrimitives(void) { return numPrims; }
		ActivePrimitive *GetUsedPrimitive(i32 i) { return &primArray[primMap[i]]; }
		void UpdateMaps(void);
	};

private:
	// 1 element
	Pose *pose;
	ActivePrimitiveList *primList;
	// 1 elements
	// poseAnimationController
	// compDrawVisibilityAnimationController
public:
	enum {
		COMPOSITE_DRAWABLE = 0x123000,
		COMPOSITE_DRAWABLE_PRIMITIVE = 0x123001,
	};

	CompositeDrawable(void);
	~CompositeDrawable(void);
	void SetPose(Pose *p) {
		Assign(pose, p);
	}
	void SetPrimitiveList(ActivePrimitiveList *list) {
		assert(primList == nil);
		primList = list;
	}
	ActivePrimitiveList *GetPrimitiveList(void) { return primList; }
	virtual void Display(DisplayList *list, GameDrawableInfo *info);
	virtual void CalcBounds(void);
	virtual void SetShaderCallback(ShaderCallback *cb);
	virtual void ReleaseShaderCallback(void);
	virtual void SetFading(bool enable);
	virtual void SetFadeAmount(float fade);
};


class CompositeDrawableLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(CompositeDrawableLoader)

	CompositeDrawableLoader(void);

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory);
};

}
