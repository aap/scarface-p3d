#include "display_list.h"
#include "renderable.h"
#include "../shader.h"
#include "../pddi.h"

namespace renderer
{

// debug temporary
bool displistvisible[NUM_DISPLAY_LISTS];
int displistsize[NUM_DISPLAY_LISTS];
int listorder[1000];
int nlists;

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
displistvisible[i] = true;
	}
displistvisible[7] = displistvisible[8] = false;	// shadows
displistvisible[9] = displistvisible[10] = false;	// env
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

/*
	trying to make sense of this mess:

layer
        0                                                       49 L    51 LF   52      53 A    55 F    56 AF
        1               ???                                     50      54      65      66      75      77
        2
        3               nightlight                              11
        4               cardsnight                              12
        5               specular                                13      14 F
        6               specular_mcbv                           15      16 F
        7               unlit_XXX_blend                         35 A    36      40 AF   41 F
        8               unlit_default                           37      38 A
        9       unused
        10              unlit_blend_add                         39
        11              lit_XXX_blend                           27      32 F
        12              lit_default                             29
        13              lit_alpha_test                          28      31 F
        14              lit_blend_add                           30
        15              cbvlit_XXX_blend                        21      25 F
        16              cbvlit_default                          23
        17              cbvlit_alpha_test                       22      26 F
        18              cbvlit_blend_add                        24
        19              layered                                 42      45 F
        20              layered_lit                             43      44 F
        21              decal_add                               4       6 F
        22              decal_blend                             3       5 F
        23              cbvlitdetails_add                       18      20 F
        24              cbvlitdetails_alpha                     17      19 F
        25              interiorfloors_unlit_alpha_blend        33      34 f
        26              env                                     9       10 F
        27                                                      60
        28                                                      76
        29              foam                                    0       1 F
        30                                                      57
        31                                                      67      68 F
        32                                                      69      70 X/XF 71 F
        33              vertexfade/untextured/lowLOD            2 F     59
        34              underwater                              78      79 B    80 F    81 BF
        35                                                      54      50 A
        36                                                      58
        37              shadowdecal                             7       8 F
        38                                                      47
        39                                                      46
        40                                                      72
        41                                                      73
        42                                                      74
        43      unused
        44                                                      82      83 F

lists
        0                       29                      foam                                                                    edges in water
        1                       29 F                    foam
        2                       33 f                    vertexfade/untextured/lowLOD
        3                       22                      decal_blend                             gunk on ground
        4                       21                      decal_add
        5                       22 F                    decal_blend
        6                       21 F                    decal_add
        7                       37                      shadowdecal                             shadows
        8                       37 F                    shadowdecal
        9                       26                      env                                     reflections???
        10                      26 F                    env
        11                      3                       nightlight                              building lights                 should be late probably!
        12                      4                       cardsnight
        13                      5                       specular                                streets
        14                      5 F                     specular
        15                      6                       specular_mcbv                           streets/ground
        16                      6 F                     specular_mcbv
        17                      24                      cbvlitdetails_alpha                     ground with alpha
        18                      23                      cbvlitdetails_add
        19                      24 F                    cbvlitdetails_alpha
        20                      23 F                    cbvlitdetails_add
        21                      15                      cbvlit_XXX_blend                        buildings
        22                      17                      cbvlit_alpha_test                       rails, scaffolding
        23                      16                      cbvlit_default                          windows, fences, signs
        24                      18                      cbvlit_blend_add                        light in tunnel
        25                      15 F                    cbvlit_XXX_blend
        26                      17 F                    cbvlit_alpha_test
        27                      11                      lit_XXX_blend                           buildings
        28                      13                      lit_alpha_test                          building fences, scaffolding
        29                      12                      lit_default                             windows, alpha stuff
        30                      14                      lit_blend_add
        31                      13 F                    lit_alpha_test
        32                      11 F                    lit_XXX_blend
        33                      25                      interiorfloors_unlit_alpha_blend
        34                      25 F                    interiorfloors_unlit_alpha_blend
        35                      7 A                     unlit_XXX_blend
        36                      7                       unlit_XXX_blend                         interiors
        37                      8                       unlit_default                           signs, windows
        38                      8 A                     unlit_default                           gunk on buildings
        39                      10                      unlit_blend_add                         neons
        40                      7 AF                    unlit_XXX_blend
        41                      7 F                     unlit_XXX_blend
        42                      19                      layered                                 interiors
        43                      20                      layered_lit                             ground
        44                      20 F                    layered_lit
        45                      19 F                    layered
        46                      39
        47                      38
        48
        49                      0 L
        50                      1
        51                      0 LF
        52                      0
        53                      0 A
        54                      1
        55                      0 F
        56                      0 AF
        57                      30
        58                      36
        59                      33                      vertexfade/untextured/lowLOD
        60                      27
        61
        62
        63
        64
        65                      1
        66                      1
        67                      31
        68                      31 F
        69                      32
        70                      32 X/XF
        71                      32 F
        72                      40
        73                      41
        74                      42
        75                      1
        76                      28
        77                      1
        78                      34                      underwater
        79                      34 B                    underwater
        80                      34 F                    underwater
        81                      34 BF                   underwater
        82                      44
        83                      44 F
*/

void
Display_List::Display(void)
{
nlists = 0;
#if 0
	// TODO: this function is much too tiny. the real one is huge
	// and these seem to be missing:
	// 75, 26, 22, 19, 1, 57, 78

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


	// the missing ones:
	RenderList(75);
	RenderList(26);
	RenderList(22);
	RenderList(19);
	RenderList(1);
	RenderList(57);
	RenderList(78);
#else
	// trying to fix this mess a bit

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
	RenderList(78);	// this was missing
		RenderList(3); RenderList(17); RenderList(18); RenderList(4);
		RenderList(33); RenderList(34);
		RenderList(62);
		RenderList(7); RenderList(8); RenderList(77);

		RenderList(49); RenderList(67); RenderList(69); RenderList(82);

		RenderList(58); RenderList(60);
	RenderList(2);
	RenderList(14); RenderList(16);
	RenderList(5); RenderList(20); RenderList(6);
	RenderList(72);
	RenderList(44); RenderList(25); RenderList(32); RenderList(31); RenderList(40);
	RenderList(80); RenderList(81);
	RenderList(23);
	RenderList(29); RenderList(50);

		RenderList(54);
	RenderList(51);
		RenderList(68);
		RenderList(83);
	RenderList(71);
	RenderList(0); RenderList(61); RenderList(63); RenderList(64);
	RenderList(41); RenderList(45);
	RenderList(38); RenderList(37);
	RenderList(39); RenderList(30); RenderList(24);
	RenderList(65);
	RenderList(70);
	RenderList(66);
	RenderList(55);
	RenderList(56);
		RenderList(76);


	// the missing ones:
	RenderList(75);
	RenderList(26);
	RenderList(22);
	RenderList(19);
	RenderList(1);
	RenderList(57);

		RenderList(11);	// light lights better come late
		RenderList(12);	// not sure, not many of these around
#endif
}

void
Display_List::RenderList(i32 i)
{
	// TODO: this is all wrong
listorder[nlists++] = i;
displistsize[i] = 0;
	for(ListLink<DisplayListDrawable> *link = renderLists[i].anchor.next;
	    link;
	    link = link->next) {
displistsize[i]++;
if(!displistvisible[i])
continue;
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
