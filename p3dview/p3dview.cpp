#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <algorithm>
#include <vector>
#include <string.h>

#include "imgui.h"

#include "glad/glad.h"

#include "p3dview.h"
#include "streaming.h"
#include "../renderer/streamgraph.h"
#include "camera.h"
#include "../renderer/display_list.h"
#include "../renderer/render_manager.h"

namespace renderer { extern bool displistvisible[NUM_DISPLAY_LISTS]; }

#ifndef nil
#define nil nullptr
#endif

#define nelem(array) (sizeof(array)/sizeof(array[0]))

using namespace core;

Input input;
CCamera camera;

float timeStep, avgTimeStep;
float windowWidth, windowHeight;

struct Scene {
	pure3d::CompositeDrawable *drawable;
	bool visible;

	friend bool operator<(const Scene &sc1, const Scene &sc2) { return strcmp(sc1.drawable->GetName(), sc2.drawable->GetName()) < 0; }
};

pure3d::CompositeDrawable *composite;
std::vector<renderer::Renderable*> renderables;
std::vector<LoadedFile> loadedFiles;

static void
RegisterShapes(content::LoadInventory *inv)
{
	std::vector<pure3d::Geometry*> geos;
	inv->Collect(geos);
	for(u32 i = 0; i < geos.size(); i++)
		renderer::RegisterInstanceShape(geos[i]);
}

// just some pure3d shit for now
void
InitApp(void)
{
	pure3d::InitDevice();
	// retail: renderer::Init (0x465120) makes the RenderManager and calls Init, which
	// builds the four scenes; GamePlayScene's ctor makes the Display_List.
	(new renderer::RenderManager)->Init(65536, 200000);
	// P3D_HIDELIST=a,b,c hides display lists (debugging)
	if(const char *h = getenv("P3D_HIDELIST"))
		for(const char *p = h; *p; ) { renderer::displistvisible[atoi(p)] = false; while(*p && *p != ',') p++; if(*p) p++; }


	content::loadManager = new content::LoadManager;
	content::loadManager->AddHandler(new content::P3DFileHandler, "p3d");
	content::loadManager->AddHandler(new pure3d::TextureLoader, pure3d::Texture::TEXTURE);
	content::loadManager->AddHandler(new pure3d::ShaderLoader, pure3d::Shader::SHADER);
	content::loadManager->AddHandler(new pure3d::GeometryLoader, pure3d::Geometry::MESH);
	content::loadManager->AddHandler(new pure3d::CompositeDrawableLoader, pure3d::CompositeDrawable::COMPOSITE_DRAWABLE);
	content::loadManager->AddHandler(new pure3d::SkeletonLoader, pure3d::Skeleton::SKELETON);

	content::loadManager->AddHandler(new renderer::WorldGeoLoader, renderer::Renderable::WORLDGEO_LOADER);
	content::loadManager->AddHandler(new renderer::ZonePkgLoader, renderer::Renderable::ZONEPKG_LOADER);
	content::loadManager->AddHandler(new renderer::InstanceLoader(renderer::InstanceLoader::SCRIPTOBJECT), renderer::InstanceLoader::SCRIPTOBJECT);
	content::loadManager->AddHandler(new renderer::InstanceLoader(renderer::InstanceLoader::GAMEGROUP), renderer::InstanceLoader::GAMEGROUP);
	content::loadManager->AddHandler(new renderer::StreamTriggerLoader, renderer::StreamTriggerLoader::STREAMTRIGGER);


	static const char *commonfiles[] = {
		"Common.p3d",
		"miami_lod.p3d",
		"miami_lod_D.p3d",
		"islands_LOD.p3d",
		"islands_LOD_D.p3d",
		"babylonclub_region.p3d",
		"bolivia_region.p3d",
		"bolivia_region_D.p3d",
		"bsandTanker_region.p3d",
		"bsandTanker_region_D.p3d",
		"downtown_region.p3d",
		"downtown_region_D.p3d",
		"fountainRock_region.p3d",
		"fountainRock_region_D.p3d",
		"havana_region.p3d",
		"havana_region_D.p3d",
		"indchopshop_region.p3d",
		"industrial_region.p3d",
		"industrial_region_D.p3d",
		"lobst_region.p3d",
		"lobst_region_D.p3d",
		"luxuryhotel_region.p3d",
		"nbeach_region.p3d",
		"nbeach_region_D.p3d",
		"sbeach_region.p3d",
		"sbeach_region_D.p3d",
		"sleepingMary_region.p3d",
		"sleepingMary_region_D.p3d",
		"tonyIsland_region.p3d",
		"tonyIsland_region_D.p3d",
		"tonymansion_region.p3d",
		"trailerpark_region_D.p3d",
		"tranq_region.p3d",
		"tranq_region_D.p3d",
	};

	content::LoadInventory *commonInv = nil;
	for(u32 i = 0; i < nelem(commonfiles); i++) {
		char path[256];
		sprintf(path, "../assets/packages/z04/%s", commonfiles[i]);
		content::LoadInventory *tmp = content::loadManager->LoadFile(path, commonInv);
		tmp->SetParent(commonInv);
		commonInv = tmp;
		RegisterShapes(tmp);
		tmp->AddRef();
		loadedFiles.push_back(LoadedFile{commonfiles[i], tmp});
		// the libraries also carry renderables: the low-LOD city/islands backdrop in
		// miami_lod / islands_LOD (drawn from 581 m out), the sky in Common
		std::vector<renderer::Renderable*> rs;
		tmp->Collect(rs);
		for(u32 j = 0; j < rs.size(); j++) {
			rs[j]->AddRef();
			renderer::g_renderMgr->scenes[renderer::RenderManager::GAMEPLAY_SCENE]->AddRenderable(rs[j]);
			renderables.push_back(rs[j]);
		}
	}

	static const char *mapfiles[] = {
		"DevilsCay_01_shell.p3d",
		"DevilsCay_02_shell.p3d",
		"DevilsCay_03_shell.p3d",
		"FountainRock_01_shell.p3d",
		"FountainRock_02_shell.p3d",
		"babylonclub_01_shell.p3d",
		"babylonclub_02_shell.p3d",
		"bridge_01_shell.p3d",
		"bridge_02_shell.p3d",
		"bsand_01_shell.p3d",
		"bsand_02_shell.p3d",
		"bsand_03_shell.p3d",
		"bsand_04_shell.p3d",
		"bsand_05_shell.p3d",
		"bsand_06_shell.p3d",
		"cargoShip_01_shell_CS0.p3d",
		"cargoShip_01_shell_CS1.p3d",
		"cgrove_01_shell.p3d",
		"cgrove_02_shell.p3d",
		"cgrove_03_shell.p3d",
		"clounge_01_shell.p3d",
		"construction_01_shell.p3d",
		"downtown_01_shell.p3d",
		"drivein_01_shell.p3d",
		"fidelrecords_01_shell.p3d",
		"fountainCave_01_shell.p3d",
		"fountainRock_03_shell.p3d",
		"gentsclub_01_shell.p3d",
		"havana_01_shell.p3d",
		"havana_02_shell.p3d",
		"havana_03_shell.p3d",
		"ind_01_shell.p3d",
		"ind_02_shell.p3d",
		"ind_03_shell.p3d",
		"indchopshop_01_shell.p3d",
		"lobst_01_shell.p3d",
		"lobst_02_shell.p3d",
		"lobst_03_shell.p3d",
		"lobst_04_shell.p3d",
		"lobst_05_shell.p3d",
		"lobst_06_shell.p3d",
		"luxurybank_01_shell.p3d",
		"luxurybank_02_shell.p3d",
		"luxuryhotel_01_shell.p3d",
		"luxuryhotel_02_shell.p3d",
		"penthouse_01_shell.p3d",
		"penthouse_02_shell.p3d",
		"sbeachn_01_shell.p3d",
		"sbeachn_02_shell.p3d",
		"sbeachn_03_shell.p3d",
		"sbeachn_04_shell.p3d",
		"sbeachn_05_shell.p3d",
		"sbeachn_06_shell.p3d",
		"sbeachs_01_shell.p3d",
		"sbeachs_02_shell.p3d",
		"sbeachs_03_shell.p3d",
		"sbeachs_04_shell.p3d",
		"shadygrove_01_shell.p3d",
		"shippingLane_01_shell.p3d",
		"sleepingMary_01_shell.p3d",
		"sleepingMary_02_shell.p3d",
		"sleepingMary_03_shell.p3d",
		"sleepingMary_04_shell.p3d",
		"sosaIsland_01_shell.p3d",
		"sosaMansion_01_shell.p3d",
		"sosaMansion_02_shell.p3d",
		"sosa_01_shell.p3d",
		"sosa_02_shell.p3d",
		"stripmall_01_shell.p3d",
		"swamp_01_shell.p3d",
		"swampshack_01_shell.p3d",
		"tonyIsland_01_shell.p3d",
		"tonyOffice_01_shell.p3d",
		"tonymansion_01_shell_TS0.p3d",
		"tonymansion_01_shell_TS1.p3d",
		"tonymansion_01_shell_TS2.p3d",
		"tonymansion_01_shell_TS3.p3d",
		"tonymansion_03_shell.p3d",
		"tonymansion_04_shell_TS0.p3d",
		"tonymansion_04_shell_TS1.p3d",
		"tonymansion_04_shell_TS2.p3d",
		"tonymansion_04_shell_TS3.p3d",
		"tonymansion_05_shell.p3d",
		"trailerpark_01_shell.p3d",
		"tranq_01_shell.p3d",
		"tranq_02_shell.p3d",
		"tranq_03_shell.p3d",
		"tutorial_01_shell.p3d",
		"DevilsCay_01_detail.p3d",
		"FountainRock_01_detail.p3d",
		"FountainRock_01_detailB.p3d",
		"FountainRock_02_detail.p3d",
		"babylonclub_01_detail.p3d",
		"babylonclub_01_detailB.p3d",
		"babylonclub_02_detail.p3d",
		"babylonclub_02_detailB.p3d",
		"babylonclub_03_detail.p3d",
		"barge_01_detail.p3d",
		"boathouse_01_detail.p3d",
		"bridge_01_detail.p3d",
		"bridge_02_detail.p3d",
		"bridge_03_detail.p3d",
		"bsand_01_detail.p3d",
		"bsand_02_detail.p3d",
		"bsand_03_detail.p3d",
		"bsand_04_detail.p3d",
		"bsand_06_detail.p3d",
		"cargoShip_01_detail_CS0.p3d",
		"cargoShip_01_detail_CS1.p3d",
		"cgrove_01_detail.p3d",
		"cgrove_01_detailB.p3d",
		"cgrove_01_detailC.p3d",
		"cgrove_02_detail.p3d",
		"cgrove_03_detail.p3d",
		"diazmotors_01_detail.p3d",
		"downtown_01_detail.p3d",
		"fountainCave_01_detail.p3d",
		"havana_01_detail.p3d",
		"havana_01_detailB.p3d",
		"havana_02_detail.p3d",
		"havana_02_detailB.p3d",
		"havana_03_detail.p3d",
		"ind_01_detail.p3d",
		"ind_01_detailB.p3d",
		"ind_02_detail.p3d",
		"ind_02_detailB.p3d",
		"ind_03_detail.p3d",
		"leopard_01_detail.p3d",
		"lobst_02_detail.p3d",
		"lobst_03_detail.p3d",
		"lobst_05_detail.p3d",
		"luxurybank_01_detail.p3d",
		"luxurybank_01_detailB.p3d",
		"luxurybank_02_detail.p3d",
		"luxurybank_02_detailB.p3d",
		"luxuryhotel_01_detail.p3d",
		"marina_01_detail.p3d",
		"ocean_01_detail.p3d",
		"ocean_02_detail.p3d",
		"penthouse_01_detail.p3d",
		"penthouse_01_detailB.p3d",
		"sandbar_01_detail.p3d",
		"sandbar_02_detail.p3d",
		"sandbar_03_detail.p3d",
		"sbeachn_01_detail.p3d",
		"sbeachn_02_detail.p3d",
		"sbeachn_03_detail.p3d",
		"sbeachn_04_detail.p3d",
		"sbeachn_04_detailB.p3d",
		"sbeachn_05_detail.p3d",
		"sbeachn_06_detail.p3d",
		"sbeachs_01_detail.p3d",
		"sbeachs_02_detail.p3d",
		"sbeachs_03_detail.p3d",
		"sbeachs_04_detail.p3d",
		"shadygrove_01_detail.p3d",
		"sleepingMary_01_detail.p3d",
		"sleepingMary_02_detail.p3d",
		"sleepingMary_03_detail.p3d",
		"sleepingMary_04_detail.p3d",
		"sosaMansion_01_detail.p3d",
		"sosaMansion_02_detail.p3d",
		"sosa_01_detail.p3d",
		"stripmall_01_detail.p3d",
		"stripmall_01_detailB.p3d",
		"swamp_01_detail.p3d",
		"tonyIsland_01_detail.p3d",
		"tonyIsland_01_detailB.p3d",
		"tonyIsland_01_detail_TS0.p3d",
		"tonyIsland_01_detail_TS1.p3d",
		"tonymansion_01_detailB.p3d",
		"tonymansion_01_detailB_TS0.p3d",
		"tonymansion_01_detailB_TS1.p3d",
		"tonymansion_01_detail_TS0.p3d",
		"tonymansion_01_detail_TS1.p3d",
		"tonymansion_01_detail_TS2.p3d",
		"tonymansion_01_detail_TS3.p3d",
		"tonymansion_02_detail.p3d",
		"tonymansion_02_detailB.p3d",
		"trailerpark_01_detail.p3d",
		"tranq_01_detail.p3d",
		"tranq_02_detail.p3d",
		"tranq_03_detail.p3d",
		"tutorial_01_detail.p3d",
		"uginbar_01_detail.p3d",
	};

	// The game streams the map through art/levels/z04/streamgraph.p3d: the shells and
	// details of the triggers around the camera (p3dview/streaming.cpp). P3D_STREAM=0,
	// or no graph, loads the whole static list above instead.
	const char *e = getenv("P3D_STREAM");
	bool stream = !(e && atoi(e) == 0);
	if(!(stream && StreamingInit(commonInv))) {
		streamingEnabled = false;
		for(u32 i = 0; i < nelem(mapfiles); i++)
			LoadPackage(mapfiles[i], commonInv, true);
		int n = 0; for(u32 i = 0; i < packages.size(); i++) n += packages[i]->rends.size();
		printf("loaded %zu packages, %d renderables\n", packages.size(), n);
	}


/*
//	inv = content::loadManager->LoadFile("../bacinari.p3d", commonInv);
//	inv = content::loadManager->LoadFile("../tony_only_bacinari.p3d", commonInv);
//	if(inv == nil)
//		return;

//	inv->Dump();


	composite = inv->Find<pure3d::CompositeDrawable>("bacinari");
	if(composite) {
		printf("Found composite %s\n", composite->GetName());
		composite->AddRef();
	}
*/

}

using namespace pure3d;

void
InitScene(void)
{
	camera.m_position = Vector(0.0f, 2.0f, 4.0f);
	camera.m_position = Vector(1454.550049, 82.420006, -128.411133);
	camera.m_position = Vector(-513.81, 11.05, -1697.34);	// south beach
	camera.m_position = Vector(1680.08, 16.86, -957.27);	// strip
	camera.m_position = Vector(164.94, 29.68, -1011.85);	// shore
	camera.m_position = Vector(-1306.78, 27.46, 197.28);	// northbeach
	camera.m_far = 4000.0f;

	// P3D_CAMPOS="x y z" and P3D_CAMTARGET="x y z" override the start camera (for scripted screenshots)
	const char *e;
	if((e = getenv("P3D_CAMPOS")) && sscanf(e, "%f %f %f", &camera.m_position.x, &camera.m_position.y, &camera.m_position.z) == 3)
		camera.m_target = camera.m_position + Vector(0.0f, 0.0f, -1.0f);
	if((e = getenv("P3D_CAMTARGET")))
		sscanf(e, "%f %f %f", &camera.m_target.x, &camera.m_target.y, &camera.m_target.z);
}

void
JumpCamera(const char *spec)
{
	Vector p;
	if(sscanf(spec, "%f %f %f", &p.x, &p.y, &p.z) != 3) return;
	Vector dir = camera.m_target - camera.m_position;
	camera.m_position = p;
	camera.m_target = p + dir;
}

#include "../lodepng/lodepng.h"

// Write the current back buffer as PNG (RGBA, flipped to top-down)
void
Screenshot(const char *path)
{
	int w = (int)windowWidth, h = (int)windowHeight;
	std::vector<unsigned char> px(w*h*4), flipped(w*h*4);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	for(int y = 0; y < h; y++)
		memcpy(&flipped[y*w*4], &px[(h-1-y)*w*4], w*4);
	for(int i = 3; i < w*h*4; i += 4) flipped[i] = 255;
	unsigned err = lodepng_encode32_file(path, flipped.data(), w, h);
	printf("screenshot %s: %s\n", path, err ? lodepng_error_text(err) : "ok");
}

ShaderVis shaderRenderable[] = {
	{ true, "error" },
	{ true, "reflection" },
	{ true, "simple" },
	{ true, "specular" },
	{ true, "pointsprite" },
	{ true, "layered" },
	{ true, "fbeffectsshader" },
	{ true, "shadow" },
	{ true, "decal" },
	{ true, "foam" },
	{ true, "nightlight" },
	{ true, "untextured" },
	{ true, "character" },
	{ true, "cbvlit" },
	{ true, "vehicle" },
	{ true, "shadowdecal" },
	{ true, "vertexfade" },
};

bool
IsShadervisible(const char *name)
{
	for(u32 i = 0; i < nelem(shaderRenderable); i++)
		if(strcmp(name, shaderRenderable[i].shader) == 0)
			return shaderRenderable[i].visible;
	return true;
}

namespace renderer {
extern bool displistvisible[renderer::NUM_DISPLAY_LISTS];
extern int displistsize[renderer::NUM_DISPLAY_LISTS];
extern int listorder[1000];
extern int nlists;
};

void
GUI(void)
{
	ExplorerGUI();
	if(0) {
		char lbl[64];
		ImGui::Begin("Lists");
		if(ImGui::Button("All")) {
			for(int i = 0; i < renderer::NUM_DISPLAY_LISTS; i++)
				renderer::displistvisible[i] = true;
		}
		if(ImGui::Button("None")) {
			for(int i = 0; i < renderer::NUM_DISPLAY_LISTS; i++)
				renderer::displistvisible[i] = false;
		}
		for(int j = 0; j < renderer::nlists; j++) {
			int i = renderer::listorder[j];
			sprintf(lbl, "%d - %d", i, renderer::displistsize[i]);
			ImGui::Checkbox(lbl, &renderer::displistvisible[i]);
		}
		ImGui::End();
	}

	if(0) {
		ImGui::Begin("Shaders");
		for(u32 i = 0; i < nelem(shaderRenderable); i++)
			ImGui::Checkbox(shaderRenderable[i].shader, &shaderRenderable[i].visible);
		ImGui::End();
	}
}

void
RenderScene(void)
{
//	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	input.Update();

	camera.m_aspectRatio = windowWidth/windowHeight;
	camera.Process();
	camera.update();

	context->Begin();

	context->SetProjectionMatrix(camera.m_projMat);
	context->SetViewMatrix(camera.m_viewMat);

	Matrix flip; flip.Identity();
	flip.e[0] = -1.0f;	// the viewer draws the world with x flipped
	context->SetWorldMatrix(flip);

	// The culling camera works in native (file) coordinates, which is also what the
	// display list nodes hold; the x flip is part of the world->clip transform. Retail
	// keeps the culling and the rendering camera apart; the viewer uses one for both.
	Vector camPosition = camera.m_position;
	camPosition.x = -camPosition.x;
	// stream the shells/details of the triggers around the camera in and out
	StreamingUpdate(camPosition, timeStep);

	renderer::Camera *cam = renderer::View_GetCullingCamera();
	cam->SetPosition(camPosition);
	cam->SetViewProjection(Multiply(flip, camera.m_viewMat), camera.m_projMat);
	renderer::View_SetRenderingCamera(cam);

	// retail: RenderFlowClient::OnFrame 0x465590
	renderer::TimeInfo t;
	t.dt = timeStep;
	t.fadeDt = timeStep;
	renderer::g_renderMgr->Update(&t);
	renderer::DestroyPendingRenderables();
	renderer::g_renderMgr->Render(&t);

	context->End();
	ExplorerDrawOverlay();
}

void
HandleSDLEvent(SDL_Event *event, bool ignoreMouse, bool ignoreKeybaord)
{
	int down;
	switch(event->type) {
	case SDL_MOUSEMOTION:
		if(ignoreMouse) break;
		input.tempState.m.x = event->motion.x;
		input.tempState.m.y = event->motion.y;
		break;
	case SDL_MOUSEBUTTONDOWN:
		if(ignoreMouse) break;
		if(event->button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_CTRL))
			ExplorerPick(event->button.x, event->button.y);
		input.tempState.m.buttons |= 1<<(event->button.button-1);
		break;
	case SDL_MOUSEBUTTONUP:
		if(ignoreMouse) break;
		input.tempState.m.buttons &= ~(1<<(event->button.button-1));
		break;

	case SDL_KEYDOWN:
	case SDL_KEYUP:
		if(ignoreKeybaord) break;
		down = event->type == SDL_KEYDOWN;
		switch(event->key.keysym.sym) {
		case SDLK_LALT:
		case SDLK_RALT:
			input.tempState.k.alt = down;
			break;
		case SDLK_LCTRL:
		case SDLK_RCTRL:
			input.tempState.k.ctrl = down;
			break;
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:
			input.tempState.k.shift = down;
			break;
		default:
			if(event->key.keysym.sym < 256) {
				int k = event->key.keysym.sym;
				input.tempState.k.keys[k] = down;
				if(islower(k))
					input.tempState.k.keys[toupper(k)] = down;
			}
			break;
		}
		break;
	}
}
