#pragma once

#include "renderable.h"
#include "../light.h"

#include <vector>

namespace renderer
{

using namespace pure3d;

class LightManager;

// retail: renderer::LightingRenderable : Renderable, vtable 0x00737eac, ctor 0x0046ff10,
// 0xa4 bytes, typeMask 0x400. Display (vslot 10) is nullsub: a light group draws nothing.
// The whole class is a lifetime object --- its ctor registers the pure3d::LightGroup with
// gLightManager (0x0045fe10) and, for kind 0, also as a lighting zone (0x00460380); for
// kind 0 and kind 1 it hands itself to the environment manager (0x0046fcd0), which is
// what plays the time-of-day animations.
class LightingRenderable : public Renderable
{
public:
	// the `kind` word of the chunk. Counts in z04: 0 x2, 1 x2, 2 x30, 3 x116, 4 x1.
	enum Kind {
		ZONE = 0,		// "zone_lights": sun + fill + ambient + building ambient
		ZONE_RAIN = 1,		// "zone_rainlights": the same four for rain
		EXTERIOR = 2,		// one per detail package: street/shop night lights
		INTERIOR = 3,		// one per shell package: the lights of one interior
		TEMPLATE = 4		// "lights_template" in Common.p3d: lamp/headlight prototypes
	};
	enum { MAX_CONTROLLERS = 5 };

	LightGroup *group;					// +0x84
	u32 kind;						// +0x88
	i32 numControllers;					// +0x8c
	LightAnimationController *controllers[MAX_CONTROLLERS];	// +0x90

	CLASSNAME(LightingRenderable);
	LightingRenderable(LightGroup *group, LightAnimationController **ctrls, i32 numCtrls, u32 kind);
	~LightingRenderable(void);				// retail: 0x00470240

	// retail: nullsub_2 --- a light group never draws
	virtual void Display(void) {}
};

class SFLightGroupLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(SFLightGroupLoader)
	SFLightGroupLoader(void) : SimpleChunkHandler(Renderable::SFLIGHTGROUP_LOADER) {}
	// retail: renderer::SFLightGroupLoader::LoadObject 0x00470280
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};


// retail: renderer::LightManager, g[0x008111cc], ctor 0x00460600, 0x70 bytes. It keeps
// the registered light groups sorted by kind and once a frame collects the lights that
// can reach the camera into a pure3d::LightsChooser. We do not have the LightsChooser
// (which reduces N world lights to 4 directional lights per lit object); the viewer
// applies the set it collects straight to the pddi context, which is the same set minus
// the per-object reduction. re/notes/lighting.md has the rules and what was verified.
class LightManager
{
public:
	// retail: LightManager::Update tests every decaying light against a 50 m sphere
	// around the camera (the constant 0x42480000 at 0x0046017e)
	enum { LIGHT_RADIUS = 50 };

	// retail: LightManager::RegisterLightGroup 0x0045fe10, one slot per kind
	LightGroup *zoneGroup;				// kind 0, +0x20
	LightGroup *zoneRainGroup;			// kind 1, +0x24
	std::vector<LightGroup*> exteriorGroups;	// kind 2, +0x00..+0x08
	std::vector<LightGroup*> interiorGroups;	// kind 3, +0x10..+0x18
	std::vector<Light*> templateLights;		// kind 4, +0x4c..+0x50

	// retail keeps these in RenderManager::env (+0x00 clear, +0x04 rainy) instead
	LightingRenderable *zoneRenderable;
	LightingRenderable *zoneRainRenderable;

	// --- knobs, not retail ---
	// hours in [0,24). Retail maps the clock through six key-frame times with hold and
	// transit bands (EnvManager 0x0046a400) before sampling the 241-frame animations;
	// we map linearly.
	float timeOfDay;
	bool raining;
	bool animate;		// play the LITE animations at all
	bool enabled;		// false: glShader falls back to its hardcoded light
	bool localLights;	// also collect the decaying lights around the camera
	// p3dview draws the world with x flipped (renderer/README.md "Coordinates in the
	// viewer"); the lights come out of the file in native coordinates.
	bool flipX;

	// --- the active set, rebuilt by Update() ---
	struct ActiveLight {
		Light *light;
		pddiColour colour;	// the light's colour scaled by its decay at the camera
		float decay;
	};
	pddiColour ambient;
	std::vector<ActiveLight> active;	// non-ambient, brightest first
	LightGroup *activeGroup;		// the zone group the set came from

	LightManager(void);

	// retail: renderer::LightManager::RegisterLightGroup 0x0045fe10
	void RegisterLightGroup(LightGroup *group, u32 kind);
	void UnregisterLightGroup(LightGroup *group, u32 kind);
	// retail: EnvManager 0x0046a270, called from the LightingRenderable ctor
	void SetZoneRenderable(LightingRenderable *r, bool clear);

	float GetFrame(void) const;
	// SHR: tLightsChooser::AddLight
	void AddLight(Light *l, float decay);
	// retail: renderer::LightManager::Update 0x00460130 --- fill the chooser, then
	// EnvManager 0x0046ba90 adds the zone lights on top
	void Update(TimeInfo *t);
	// hand the active set to the pddi context (retail: pure3d::Light::Activate ->
	// AmbientLight::Update 0x006894a0 / PointLight::Update 0x006893a0)
	void Apply(void);
};

// retail: renderer::gLightManager g[0x008111cc]
extern LightManager *gLightManager;

}
