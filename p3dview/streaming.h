#pragma once

#include "p3dview.h"

// A resident package: its inventory and the renderables it contributed to the scene.
struct Package {
	std::string name;			// file name in the package dir ("sbeachn_01_shell.p3d")
	content::LoadInventory *inv;
	std::vector<renderer::Renderable*> rends;
	float unneeded;				// seconds since a trigger last asked for it
	bool pinned;				// loaded by hand, never streamed out
};

// load/unload one package into the gameplay scene (shared by streaming and the
// static "load everything" path)
Package *LoadPackage(const char *file, content::LoadInventory *resolveInv, bool pinned);
// the same out of another content directory (the azone pockets live in
// packages/azones); `name` is what the package is called in `packages`
Package *LoadPackagePath(const char *dir, const char *file, const char *name,
                         content::LoadInventory *resolveInv, bool pinned);
void UnloadPackage(Package *pkg);
extern std::vector<Package*> packages;

// stream graph driven streaming
bool StreamingInit(content::LoadInventory *resolveInv);	// false: no streamgraph.p3d
void StreamingUpdate(const math::Vector &nativePos, float dt);
void StreamingGUI(void);
extern bool streamingEnabled;

// for the explorer's World tab
namespace renderer { class StreamTrigger; }
const std::vector<renderer::StreamTrigger*> &StreamingTriggers(void);
const std::vector<renderer::StreamTrigger*> &StreamingCurrent(void);	// containing the camera
Package *FindPackage(const char *file);
const char *PackageFile(const std::string &graphName);	// graph name -> file name in pkgdir, "" if absent
void StreamingPinZone(const char *tag, bool pin);		// keep a subzone's Shell+Detail packages resident
bool StreamingZonePinned(const char *tag);
