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
static const char *azonedir = "packages/azones";
static const char *graphPaths[] = {
	"art/levels/z04/streamgraph.p3d",
	"packages/z04/streamgraph.p3d",
};
// the compiled mission script that owns the azone triggers (see "azone pockets" below)
static const char *azoneScript = "scriptc/missions/z04/azone_triggers.dso";
// how a pocket package is named in `packages` (the file lives in another directory)
static const char *azonePrefix = "azones/";

std::vector<Package*> packages;
bool streamingEnabled = true;

static content::LoadInventory *graphInv;
static std::vector<renderer::StreamTrigger*> triggers;
static std::vector<renderer::StreamTrigger*> current;	// triggers containing the camera
static std::vector<renderer::StreamTrigger*> currentAzones;
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
LoadPackagePath(const char *dir, const char *file, const char *name, content::LoadInventory *resolveInv, bool pinned)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, file);
	u32 t0 = SDL_GetTicks();
	content::LoadInventory *inv = content::loadManager->LoadFile(path, resolveInv);
	if(inv == nil)
		return nil;
	Package *pkg = new Package;
	pkg->name = name;
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
		printf("loaded %s: %zu renderables, %d/%d instance models, %.0f ms\n", pkg->name.c_str(), pkg->rends.size(), resolved, resolved+unresolved, lastLoadMs);
	return pkg;
}

Package*
LoadPackage(const char *file, content::LoadInventory *resolveInv, bool pinned)
{
	return LoadPackagePath(pkgdir, file, file, resolveInv, pinned);
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

// ------------------------------------------------------------- azone "pockets"
//
// The shells and details of the map come out of streamgraph.p3d, but the 32
// `packages/azones/*_pocket.p3d` do not: they are loaded by a mission script. The
// compiled `scriptc/missions/z04/azone_triggers.dso` declares one trigger volume per
// pocket and calls `LoadAzone('<pocket>')` / `UnloadAzone('<pocket>')` as the player
// crosses it (re/notes/streaming.md "azones / pockets"). Those packages are not just
// shop interiors: 46 world geos live in them, some of which are pieces of the street
// OUTSIDE --- the tiled sidewalk in front of the North Beach bank is `details_sbn02p`
// in `sbeachn_02_pocket.p3d`. Without them the map has holes you can see the sky and
// the ocean through.
//
// The viewer runs no scripts, so it reads the polygons and the package names out of the
// script's GLOBAL STRING TABLE, which holds them in source order: the trigger's name,
// then one "x y z" string per polygon point, then "LoadAzone('<pocket>');" and
// "UnloadAzone('<pocket>');". `re/notes/fog.md` ("Reading the compiled script") has the
// .cso/.dso layout: `u32 version | u32 globalStringTableSize + bytes | ...`, and the
// hashed `stx########` identifiers that sit between the point strings are simply not
// float triples, so they are skipped. 25 triggers come out of the z04 script.
static std::vector<renderer::StreamTrigger*> azones;

static bool
LoadAzoneTriggers(void)
{
	content::LoadStream *s = content::OpenContentFile(azoneScript);
	if(s == nil) {
		printf("no %s: the azone pockets stay out (extract it with: "
		       "python3 re/rcf.py <cement.rcf> extract assets azone_triggers)\n", azoneScript);
		return false;
	}
	u32 version = s->GetU32();
	u32 size = s->GetU32();
	if(version != 1 || size == 0 || size > s->GetSize()) {
		printf("%s: not a compiled script (version %u, string table %u)\n", azoneScript, version, size);
		s->Release();
		return false;
	}
	char *blob = new char[size+1];
	s->GetData(blob, size);
	blob[size] = '\0';
	s->Release();

	std::vector<math::Vector> points;
	for(u32 o = 0; o < size; o += strlen(blob+o) + 1) {
		const char *str = blob + o;
		char name[128];
		float x, y, z;
		int n = 0;
		if(sscanf(str, "LoadAzone('%127[^']');%n", name, &n) == 1 && str[n] == '\0') {
			if(points.empty()) continue;
			renderer::StreamTrigger *t = new renderer::StreamTrigger;
			t->AddRef();
			t->name = name;
			t->tag = name;
			t->height = 0.0f;
			t->points = points;
			t->loads.push_back(renderer::StreamTrigger::Load{name, "Azone"});
			azones.push_back(t);
			points.clear();
		} else if(sscanf(str, "%f %f %f%n", &x, &y, &z, &n) == 3 && str[n] == '\0')
			points.push_back(math::Vector(x, y, z));
	}
	delete[] blob;
	printf("azone triggers: %zu pockets\n", azones.size());
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
	LoadAzoneTriggers();
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
	// ...plus the azone pockets whose script trigger we are inside (LoadAzone)
	currentAzones.clear();
	for(u32 i = 0; i < azones.size(); i++)
		if(azones[i]->Contains(pos.x, pos.z)) {
			currentAzones.push_back(azones[i]);
			wanted.insert(azonePrefix + azones[i]->tag + ".p3d");
		}

	// load the missing ones, a few per frame so the hitch stays small
	int loads = firstUpdate ? 1000 : loadsPerFrame;
	for(auto &file : wanted) {
		Package *p = FindPackage(file.c_str());
		if(p) { p->unneeded = 0.0f; continue; }
		if(loads-- <= 0) break;
		if(file.compare(0, strlen(azonePrefix), azonePrefix) == 0)
			LoadPackagePath(azonedir, file.c_str()+strlen(azonePrefix), file.c_str(), resolver, false);
		else
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
	if(!currentAzones.empty()) {
		ImGui::Text("azone pockets here (%zu):", currentAzones.size());
		for(u32 i = 0; i < currentAzones.size(); i++) ImGui::BulletText("%s", currentAzones[i]->tag.c_str());
	}
	ImGui::Separator();
	ImGui::Text("resident packages (%zu):", packages.size());
	for(u32 i = 0; i < packages.size(); i++) {
		Package *p = packages[i];
		if(p->pinned) ImGui::BulletText("%s  (pinned)", p->name.c_str());
		else if(p->unneeded > 0.0f) ImGui::BulletText("%s  unwanted %.1fs", p->name.c_str(), p->unneeded);
		else ImGui::BulletText("%s", p->name.c_str());
	}
}
