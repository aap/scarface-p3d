#include "display_list.h"
#include "renderable.h"
#include "../shader.h"
#include "../pddi.h"

namespace renderer
{

using namespace pure3d;

Display_List *Display_List::Inst;

Display_List::Display_List(i32 size)
 : DisplayList(1, 1)
{
	drawableStore.Allocate(size);
	renderLists.Allocate(NUM_DISPLAY_LISTS);
	for(u32 i = 0; i < nelem(renderListBits); i++) {
		renderLists[i].Init();
		renderListBits[i] = 0;
	}
	for(i32 i = 0; i < size; i++) {
		DisplayListDrawable *drw = &drawableStore[i];
		drw->parent = nil;
		drw->primEntry = nil;
		drw->shader = nil;
		drw->mylist = nil;
		freeList.Insert(&drw->link);
		drw->self = drw;
	}
	Inst = this;
}

void
Display_List::AddContainer(DrawableContainer *container, Matrix *matrix, GameDrawableInfo *info)
{
	for(i32 i = 0; i < container->GetNumElements(); i++)
		AddContainerElement(container, i, matrix, info);
}

void
Display_List::AddContainerElement(DrawableContainer *container, i32 idx, Matrix *matrix, GameDrawableInfo *info)
{
	ListLink<DisplayListDrawable> *link = freeList.anchor.next;
	bool isFading = container->IsFading();
	if(link == nil) {
		printf("no space in display list\n");
		return;
	}
	DisplayListDrawable *draw = (DisplayListDrawable*)link;

	if(matrix)
		draw->matrix = Multiply(*matrix, context->GetWorldMatrix());
	else
		draw->matrix = context->GetWorldMatrix();

	draw->container = container;
	draw->primEntry = container->GetElement(idx);
	draw->shader = draw->primEntry->prim->GetShader();

	DrawablePrimitive *prim = container->GetElement(idx)->prim;
	Shader *shader = prim->GetShader();
	u32 shaderType = shader->GetType();
	int listID = prim->GetLayer();
	bool isLit = prim->IsLit();
	bool isALUM = prim->IsALUM();
	u32 type = prim->GetSomeMask();
	switch(prim->GetLayer()) {
	case 0:
		if(isFading) {
			if(isLit)
				listID = 51;
			else if(isALUM)
				listID = 56;
			else
				listID = 55;
		} else {
			if(isLit)
				listID = 49;
			else if(isALUM)
				listID = 53;
			else
				listID = 52;
		}
		break;
	case 1:
		if(type == 2)
			listID = 66;
		else if(type == 4 || type == 8)
			listID = 65;
		else if(shaderType == Shader::SHADER_SHADOWDECAL)
			listID = 77;
		else if(shaderType == Shader::SHADER_DECAL)
			listID = 75;
		else if(isLit)
			listID = 50;
		else
			listID = 54;
// TODO? some shit here
		break;
	case 2:
		assert(0 && "no idea what's going on here");
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
		if(isFading) {
			if(shader->IsXXXBlendMode()) {
				listID = 70;
				// TODO: set shit
			} else
				listID = 71;
		} else {
			if(shader->IsXXXBlendMode())
				listID = 70;
			else
				listID = 69;
		}
		break;
	case 33: listID = shaderType == Shader::SHADER_VERTEXFADE ? 2 : 59; break;
	case 34:
		if(shaderType == Shader::SHADER_CBVLIT && shader->GetBlendMode() == PDDI_BLEND_ALPHA)
			listID = isFading ? 81 : 79;
		else
			listID = isFading ? 80 : 78;
		break;
	case 35: listID = isLit || isALUM ? 50 : 54;
// TODO? some shit here
		break;
	case 36: listID = 58; break;
	case 37: listID = isFading ? 8 : 7; break;
	case 38: listID = 47; break;
	case 39: listID = 46; break;
	case 40: listID = 72; break;
	case 41: listID = 73; break;
	case 42: listID = 74; break;
	case 44: listID = isFading ? 83 : 82; break;
	default:
		printf("unknown layer %d\n", prim->GetLayer());
		return;
	}

	if(info) {
		freeList.Remove(&draw->link);
		draw->parent = (DisplayListPrimitive*)info;
		draw->parent->children.Insert(&draw->linkParent);
	} else {
		freeList.Remove(&draw->link);
		draw->parent = nil;
	}
	renderLists[listID].Insert(&draw->link);
	draw->mylist = &renderLists[listID];
	renderListBits[listID] = true;
}

void
Display_List::Display(void)
{
	// TODO: this function is much too tiny. the real one is huge

	RenderList(46); RenderList(47);
	RenderList(59);
		RenderList(36); RenderList(42);
		RenderList(73);
		RenderList(52);
		RenderList(53);
	RenderList(21); RenderList(27); RenderList(28); RenderList(35); RenderList(43);
	RenderList(74);
	RenderList(9); RenderList(10);
	RenderList(13); RenderList(15);
	RenderList(79);
		RenderList(3); RenderList(17); RenderList(18); RenderList(4);
		RenderList(33); RenderList(34);
		RenderList(62);
	// 46, 47
	// 59
	//	36, 42
	//	73
	//	52
	//	53
	// 21, 27, 28, 35, 43
	// 74
	// 9, 10
	// 13, 15
	// 79
	// 	3, 17, 18, 4
	// 	33, 34
	//	62
	// -
		RenderList(7); RenderList(8); RenderList(77);

		RenderList(49); RenderList(67); RenderList(69); RenderList(82);

		RenderList(58); RenderList(60);
	RenderList(2);
	RenderList(14); RenderList(16);
	RenderList(5); RenderList(20); RenderList(6);
		RenderList(11);
	//	7, 8, 77
	// -
	//	49, 67, 69, 82
	// -
	//	58, 60
	// 2
	// 14, 16
	// 5, 20, 6
	//	11
	RenderList(72);
	RenderList(44); RenderList(25); RenderList(32); RenderList(31); RenderList(40);
	RenderList(80); RenderList(81);
	RenderList(23);
	RenderList(29); RenderList(50);
		RenderList(12);

		RenderList(54);
	RenderList(51);
		RenderList(68);
		RenderList(83);
	RenderList(71);
	RenderList(0); RenderList(61); RenderList(63); RenderList(64);
	// 72
	// 44, 25, 32, 31, 40
	// 80, 81
	// 23
	// 29, 50
	//	12
	// -
	//	54
	// 51
	//	68
	//	83
	// 71
	// 0, 61, 63, 64
/*
	RenderList(15); RenderList(13);						// DUP somehow
		RenderList(36); RenderList(42);					// DUP
		RenderList(73);							// DUP 
		RenderList(3); RenderList(17); RenderList(18);			// DUP + 4
		RenderList(52);							// DUP
		RenderList(53);							// DUP
		RenderList(62);							// DUP
		RenderList(67);							// DUP
		RenderList(69);							// DUP
		RenderList(82);							// DUP
		RenderList(33); RenderList(34);					// DUP
		RenderList(49);							// DUP
		RenderList(54);							// DUP
		RenderList(68);							// DUP
		RenderList(83);							// DUP
*/
	// 15, 13
	//	36, 42
	//	73
	// 	3, 17, 18
	//	52
	//	53
	//	62
	//	67
	//	69
	//	82
	//	33, 34
	//	49
	//	54
	//	68
	//	83
	RenderList(41); RenderList(45);
	RenderList(38); RenderList(37);
	RenderList(39); RenderList(30); RenderList(24);
	RenderList(65);
	RenderList(70);
	RenderList(66);
	RenderList(55);
	RenderList(56);
		RenderList(76);
	// 41, 45
	// 38, 37
	// 39, 30, 24
	// 65
	// 70
	// 66
	// 55
	// 56
	//	76
}

void
Display_List::RenderList(i32 i)
{
	// TODO: this is all wrong
	for(ListLink<DisplayListDrawable> *link = renderLists[i].anchor.next;
	    link;
	    link = link->next) {
		DisplayListDrawable *draw = (DisplayListDrawable*)link;

		context->SetWorldMatrix(draw->matrix);
		draw->primEntry->prim->Display();
	}
}

void
Display_List::RemovePrimitive(DisplayListPrimitive *prim)
{
	ListLink<DisplayListDrawable> *next;
	for(ListLink<DisplayListDrawable> *link = prim->children.anchor.next;
	    link;
	    link = next) {
		next = link->next;
		// TODO: this is probably expressed differently
		DisplayListDrawable *drw = *(DisplayListDrawable**)(link+1);
		drw->mylist->Remove(&drw->link);
		freeList.Insert(&drw->link);
drw->mylist = &freeList;

		prim->children.Remove(link);
// just to be safe
drw->linkParent.Init();
		drw->parent = nil;
	}
}

}
