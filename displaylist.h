#pragma once

#include "drawable.h"
#include "gmath.h"
#include "array.h"

namespace pure3d
{

using namespace math;

class GameDrawableInfo : public NonCopyable
{
	// unclear what is contained here
};

class DisplayList : public Entity
{
	struct Drawable {
		Matrix matrix;
		DrawableContainer *container;
		GameDrawableInfo *info;
	};

	i32 numSorted;
	i32 numObjects;
	IRefCount *unknown;
	Array2<void*> sortedList;	// unclear what the elements are, possibly renderlists
	Array2<Drawable> drawables;
public:
	CLASSNAME(DisplayList);
	// TODO: implement them, unfortunately we don't know how the sortedList works
	DisplayList(i32 numXX, i32 numDrawables) {}
	// retail vslots 7, 8 and 9 of renderer::Display_List (0x45d360, 0x45d3b0, 0x45e680)
	virtual void AddContainer(DrawableContainer *container, Matrix *matrix, GameDrawableInfo *info) {}
	virtual void AddContainerElement(DrawableContainer *container, i32 idx, Matrix *matrix, GameDrawableInfo *info) {}
	virtual void Render(void) {}
};

}
