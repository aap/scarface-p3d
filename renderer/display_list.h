#pragma once

#include "../list.h"
#include "../displaylist.h"

namespace renderer
{

using namespace pure3d;

class DisplayListPrimitive;

// these are children of a DisplayListPrimitive, stored in Display_List
struct DisplayListDrawable
{
	ListLink<DisplayListDrawable> link;
	Matrix matrix;
	// float containerUnk;	// copied form container
	// unknown
	PrimEntry *primEntry;
	DrawableContainer *container;	// primEntry's container
	Shader *shader;
	LinkedList<DisplayListDrawable> *mylist;	// list in which link is in
	DisplayListPrimitive *parent;
	ListLink<DisplayListDrawable> linkParent;	// child in parent
	DisplayListDrawable *self;
};

enum { NUM_DISPLAY_LISTS = 84 };

class Display_List : public DisplayList
{
	Array2<LinkedList<DisplayListDrawable>> renderLists;	// [NUM_DISPLAY_LISTS]
	bool renderListBits[NUM_DISPLAY_LISTS];

	LinkedList<DisplayListDrawable> freeList;
	Array<DisplayListDrawable> drawableStore;
	// TODO: some weirder stuff
public:
	CLASSNAME(Display_List);
	Display_List(i32 size);
	virtual void AddContainer(DrawableContainer *container, Matrix *matrix, GameDrawableInfo *info);
	virtual void AddContainerElement(DrawableContainer *container, i32 idx, Matrix *matrix, GameDrawableInfo *info);
	virtual void Display(void);

	void RenderList(i32 i);
	void RemovePrimitive(DisplayListPrimitive *prim);

	static Display_List *Inst;
};

};
