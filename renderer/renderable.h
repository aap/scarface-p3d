#pragma once

#include "../list.h"
#include "../displaylist.h"

namespace renderer
{

using namespace pure3d;

class Renderable;
struct DisplayListDrawable;

// This used by Renderable, it also knows about Display_List
//  because the children are stored there.
// the drawable comes from a CompositeDrawable
class DisplayListPrimitive : public GameDrawableInfo
{
	Renderable *parent;
	DrawableHierarchy *drawable;
	LinkedList<DisplayListDrawable> children;
	bool isVisible : 1;
	bool isInList : 1;
	bool flag4 : 1;
public:
	CLASSNAME(DisplayListPrimitive);
	DisplayListPrimitive(void);
	void SetParent(Renderable *renderable) { parent = renderable; }
	void SetDrawable(DrawableHierarchy *drawable, bool flag4);
	DrawableHierarchy *GetDrawable(void) { return drawable; }
	void SetVisible(bool visible);
	void Display(bool visible);

	friend class Display_List;	// TODO: get rid of this
};

struct DisplayListElement
{
	float drawDist[3];	// min, max, fade
	DisplayListPrimitive prim;
	bool isFading;
};

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

	// TODO: lots more
	Matrix matrix;
	Array<DisplayListElement> elements;
	bool flag1 : 1;
	bool doDistFade : 1;
	bool flag4 : 1;
	bool flag8 : 1;
	bool isVisible : 1;
	// more flag?

	bool flagB1 : 1;
	bool isMatrixDirty : 1;

	Renderable(i32 numElements);

	void SetNumElements(i32 n);
	void SetElement(DrawableHierarchy *drawable, i32 i, bool flag4);

	//
	//
	virtual void SetVisible(bool visible);
	//
	virtual void Display(void);
	//
	// SetMatrix
	// GetPosition
	// Synch
	//
};

}
