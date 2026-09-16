#pragma once

#include "../pure3d.h"

#include <SDL.h>
#include <string>
#include <vector>
void HandleSDLEvent(SDL_Event *event, bool ignoreMouse, bool ignoreKeybaord);

using namespace core;

struct MouseState
{
	u32 buttons;	// left 1, middle 2, right 4
	i32 x, y;
};

struct KeyboardState
{
	bool ctrl, alt, shift;
	bool keys[256];
};

struct Input
{
	struct {
		MouseState m;
		KeyboardState k;
	} oldState, newState, tempState;

	void Update(void) {
		oldState = newState;
		newState = tempState;
	}
	int MouseDown(int btn) { return newState.m.buttons & btn; }
	int MouseUp(int btn) { return ~newState.m.buttons & btn; }
	int MouseJustDown(int btn) { return (~oldState.m.buttons & newState.m.buttons) & btn; }
	int MouseJustUp(int btn) { return (oldState.m.buttons & ~newState.m.buttons) & btn; }
	int CtrlDown(void) { return newState.k.ctrl; }
	int AltDown(void) { return newState.k.alt; }
	int ShiftDown(void) { return newState.k.shift; }
	int MouseDX(void) { return newState.m.x - oldState.m.x; }
	int MouseDY(void) { return newState.m.y - oldState.m.y; }
	int KeyDown(int key) { return key < 256 ? newState.k.keys[key] : 0; }
	int KeyJustDown(int key) { return key < 256 ? newState.k.keys[key] && !oldState.k.keys[key] : 0; }
};

extern Input input;

extern float timeStep, avgTimeStep;
extern float windowWidth, windowHeight;


void InitApp(void);
void InitGL(void* loadproc);
void InitScene(void);
void RenderScene(void);
void Screenshot(const char *path);
void JumpCamera(const char *spec);	// "x y z", keeps the view direction

struct LoadedFile {
	std::string name;
	content::LoadInventory *inv;
};
extern std::vector<LoadedFile> loadedFiles;
extern std::vector<renderer::Renderable*> renderables;

struct ShaderVis { bool visible; const char *shader; };
extern ShaderVis shaderRenderable[17];

// explorer.cpp
void ExplorerGUI(void);
void ExplorerPick(int mx, int my);
void ExplorerDrawOverlay(void);
void GUI(void);
