#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <algorithm>
#include <map>
#include <set>

#include "imgui.h"

#include "streaming.h"
#include "../renderer/render_manager.h"
#include "../renderer/instance.h"
#include "../renderer/streamgraph.h"

#ifndef nil
#define nil nullptr
#endif

// cement paths: they resolve inside the mounted cement.rcf, or under a content root
// (../assets) when there is none --- see content::OpenContentFile (rcf.cpp)
static const char *pkgdir = "packages/z04";
static const char *graphPaths[] = {
	"art/levels/z04/streamgraph.p3d",
	"packages/z04/streamgraph.p3d",
};

std::vector<Package*> packages;
bool streamingEnabled = true;

static content::LoadInventory *graphInv;
static std::vector<renderer::StreamTrigger*> triggers;
static std::vector<renderer::StreamTrigger*> current;	// triggers containing the camera
static std::map<std::string, std::string> fileIndex;	// lower-case name -> real file name
static content::LoadInventory *resolver;
static float unloadDelay = 3.0f;	// seconds a package may be unwanted before it goes
static bool nearestFallback = true;	// outside every trigger: use the closest one
static int loadsPerFrame = 1;
static bool firstUpdate = true;
static float lastLoadMs;

static void
RegisterShapes(content::LoadInventory *inv)
{
	std::vector<pure3d::Geometry*> geos;
	inv->Collect(geos);
	for(u32 i = 0; i < geos.size(); i++)
		renderer::RegisterInstanceShape(geos[i]);
}

Package*
LoadPackage(const char *file, content::LoadInventory *resolveInv, bool pinned)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", pkgdir, file);
	u32 t0 = SDL_GetTicks();
	content::LoadInventory *inv = content::loadManager->LoadFile(path, resolveInv);
	if(inv == nil)
		return nil;
	Package *pkg = new Package;
	pkg->name = file;
	pkg->inv = inv;
	pkg->unneeded = 0.0f;
	pkg->pinned = pinned;

	RegisterShapes(inv);
	inv->Collect(pkg->rends);
	int resolved = 0, unresolved = 0;
	for(u32 i = 0; i < pkg->rends.size(); i++) {
		renderer::Renderable *r = pkg->rends[i];
		r->AddRef();
		// retail: every creator ends with scenes[scene]->AddRenderable(r)
		renderer::g_renderMgr->scenes[renderer::RenderManager::GAMEPLAY_SCENE]->AddRenderable(r);
		renderables.push_back(r);
		// bind eco prop placements to their InstanceShape meshes
		if(auto *ir = dynamic_cast<renderer::InstanceRenderable*>(r)) {
			if(ir->modelName.empty()) continue;
			if(renderer::ResolveInstanceShapes(ir)) resolved++;
			else { unresolved++; if(getenv("P3D_VERBOSE")) printf("no InstanceShape for %s\n", ir->modelName.c_str()); }
		}
	}
	// keep the inventory for the explorer (the loader gave us a reference)
	loadedFiles.push_back(LoadedFile{pkg->name, inv});
	packages.push_back(pkg);
	lastLoadMs = SDL_GetTicks() - t0;
	if(getenv("P3D_VERBOSE"))
		printf("loaded %s: %zu renderables, %d/%d instance models, %.0f ms\n", file, pkg->rends.size(), resolved, resolved+unresolved, lastLoadMs);
	return pkg;
}

void
UnloadPackage(Package *pkg)
{
	renderer::Scene *scene = renderer::g_renderMgr->scenes[renderer::RenderManager::GAMEPLAY_SCENE];
	for(u32 i = 0; i < pkg->rends.size(); i++) {
		renderer::Renderable *r = pkg->rends[i];
		r->SetVisible(false);		// withdraws its display list nodes
		scene->RemoveRenderable(r);
		renderables.erase(std::remove(renderables.begin(), renderables.end(), r), renderables.end());
		r->Release();
	}
	for(u32 i = 0; i < loadedFiles.size(); i++)
		if(loadedFiles[i].inv == pkg->inv) { loadedFiles.erase(loadedFiles.begin()+i); break; }
	packages.erase(std::remove(packages.begin(), packages.end(), pkg), packages.end());
	pkg->inv->Release();
	if(getenv("P3D_VERBOSE"))
		printf("unloaded %s\n", pkg->name.c_str());
	delete pkg;
}

Package*
FindPackage(const char *file)
{
	for(u32 i = 0; i < packages.size(); i++)
		if(strcasecmp(packages[i]->name.c_str(), file) == 0)
			return packages[i];
	return nil;
}

const std::vector<renderer::StreamTrigger*> &StreamingTriggers(void) { return triggers; }
const std::vector<renderer::StreamTrigger*> &StreamingCurrent(void) { return current; }

const char*
PackageFile(const std::string &graphName)
{
	auto it = fileIndex.find(graphName);
	return it == fileIndex.end() ? "" : it->second.c_str();
}

static void
ZoneFiles(const char *tag, std::set<std::string> &files)
{
	for(u32 i = 0; i < triggers.size(); i++) {
		if(strcasecmp(triggers[i]->tag.c_str(), tag) != 0) continue;
		std::vector<std::string> names;
		triggers[i]->Packages("Shell", names);
		triggers[i]->Packages("Detail", names);
		for(auto &n : names) {
			const char *f = PackageFile(n);
			if(*f) files.insert(f);
		}
	}
}

void
StreamingPinZone(const char *tag, bool pin)
{
	std::set<std::string> files;
	ZoneFiles(tag, files);
	for(auto &f : files) {
		Package *p = FindPackage(f.c_str());
		if(p == nil && pin) p = LoadPackage(f.c_str(), resolver, true);
		if(p) { p->pinned = pin; p->unneeded = 0.0f; }
	}
}

bool
StreamingZonePinned(const char *tag)
{
	std::set<std::string> files;
	ZoneFiles(tag, files);
	if(files.empty()) return false;
	for(auto &f : files) {
		Package *p = FindPackage(f.c_str());
		if(p == nil || !p->pinned) return false;
	}
	return true;
}

bool
StreamingInit(content::LoadInventory *resolveInv)
{
	resolver = resolveInv;
	for(u32 i = 0; i < sizeof(graphPaths)/sizeof(graphPaths[0]); i++) {
		if(!content::ContentFileExists(graphPaths[i])) continue;
		graphInv = content::loadManager->LoadFile(graphPaths[i], nil);
		break;
	}
	if(graphInv == nil) {
		printf("no streamgraph.p3d (run with -rcf <cement.rcf>, or extract it with: "
		       "python3 re/rcf.py <cement.rcf> extract assets streamgraph)\n");
		return false;
	}
	graphInv->Collect(triggers);
	printf("stream graph: %zu triggers\n", triggers.size());

	// The graph names packages in lower case without the extension, so the real file
	// names have to come from a directory listing: the archive's name table when one
	// is mounted, readdir of the extracted tree otherwise.
	std::vector<std::string> files;
	if(content::RCFArchive *rcf = content::MountedRCF()) {
		std::vector<std::string> paths;
		rcf->List(pkgdir, paths);
		for(auto &p : paths) {
			size_t s = p.find_last_of("\\/");
			files.push_back(s == std::string::npos ? p : p.substr(s+1));
		}
	} else {
		for(auto &root : content::ContentRoots()) {
			std::string dir = root + "/" + pkgdir;
			DIR *d = opendir(dir.c_str());
			if(d == nil) continue;
			struct dirent *e;
			while((e = readdir(d)))
				files.push_back(e->d_name);
			closedir(d);
		}
	}
	for(auto &n : files) {
		if(n.size() < 5 || strcasecmp(n.c_str()+n.size()-4, ".p3d") != 0) continue;
		std::string key = n.substr(0, n.size()-4);
		for(auto &c : key) c = tolower(c);
		fileIndex[key] = n;
	}
	if(getenv("P3D_VERBOSE"))
		printf("package index: %zu files in %s\n", fileIndex.size(), pkgdir);
	return true;
}

void
StreamingUpdate(const math::Vector &pos, float dt)
{
	if(!streamingEnabled || triggers.empty())
		return;

	current.clear();
	for(u32 i = 0; i < triggers.size(); i++)
		if(triggers[i]->Contains(pos.x, pos.z))
			current.push_back(triggers[i]);
	if(current.empty() && nearestFallback) {
		float best = 1e30f; renderer::StreamTrigger *bt = nil;
		for(u32 i = 0; i < triggers.size(); i++) {
			float x0, x1, z0, z1;
			triggers[i]->Bounds(x0, x1, z0, z1);
			float dx = pos.x < x0 ? x0-pos.x : pos.x > x1 ? pos.x-x1 : 0.0f;
			float dz = pos.z < z0 ? z0-pos.z : pos.z > z1 ? pos.z-z1 : 0.0f;
			float d = dx*dx + dz*dz;
			if(d < best) { best = d; bt = triggers[i]; }
		}
		if(bt) current.push_back(bt);
	}

	// what the game would have resident here: the Shell and Detail slots
	std::set<std::string> wanted;
	for(u32 i = 0; i < current.size(); i++) {
		std::vector<std::string> names;
		current[i]->Packages("Shell", names);
		current[i]->Packages("Detail", names);
		for(auto &n : names) {
			auto it = fileIndex.find(n);
			if(it != fileIndex.end()) wanted.insert(it->second);
		}
	}

	// load the missing ones, a few per frame so the hitch stays small
	int loads = firstUpdate ? 1000 : loadsPerFrame;
	for(auto &file : wanted) {
		Package *p = FindPackage(file.c_str());
		if(p) { p->unneeded = 0.0f; continue; }
		if(loads-- <= 0) break;
		LoadPackage(file.c_str(), resolver, false);
	}
	firstUpdate = false;

	// age the rest and drop what nobody has asked for in a while
	for(u32 i = 0; i < packages.size(); ) {
		Package *p = packages[i];
		if(p->pinned || wanted.count(p->name)) { i++; continue; }
		p->unneeded += dt;
		if(p->unneeded > unloadDelay) UnloadPackage(p);	// erases from packages
		else i++;
	}
}

void
StreamingGUI(void)
{
	if(!ImGui::CollapsingHeader("Streaming", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	if(triggers.empty()) { ImGui::TextDisabled("no stream graph: static package list"); return; }
	ImGui::Checkbox("stream packages by camera position", &streamingEnabled);
	ImGui::Checkbox("nearest trigger when outside all", &nearestFallback);
	ImGui::SliderFloat("unload delay (s)", &unloadDelay, 0.0f, 30.0f);
	ImGui::SliderInt("loads per frame", &loadsPerFrame, 1, 8);
	ImGui::Text("last load %.0f ms", lastLoadMs);
	ImGui::Separator();
	ImGui::Text("triggers here (%zu):", current.size());
	std::set<std::string> tags;
	for(u32 i = 0; i < current.size(); i++) tags.insert(current[i]->tag + "  [" + current[i]->Region() + "]");
	for(auto &t : tags) ImGui::BulletText("%s", t.c_str());
	ImGui::Separator();
	ImGui::Text("resident packages (%zu):", packages.size());
	for(u32 i = 0; i < packages.size(); i++) {
		Package *p = packages[i];
		if(p->pinned) ImGui::BulletText("%s  (pinned)", p->name.c_str());
		else if(p->unneeded > 0.0f) ImGui::BulletText("%s  unwanted %.1fs", p->name.c_str(), p->unneeded);
		else ImGui::BulletText("%s", p->name.c_str());
	}
}
