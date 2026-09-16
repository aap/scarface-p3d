// renderer::LightingRenderable / SFLightGroupLoader (chunk 0x08800007) and the light
// manager. The chunk formats, the retail addresses and which rules were verified in the
// disassembly are written down in re/notes/lighting.md.

#include "lighting.h"
#include "../pddi.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace renderer
{

// retail: renderer::gLightManager g[0x008111cc]
LightManager *gLightManager;


// ---------------------------------------------------------------- LightingRenderable

// retail: renderer::LightingRenderable::LightingRenderable 0x0046ff10
LightingRenderable::LightingRenderable(LightGroup *group, LightAnimationController **ctrls, i32 numCtrls, u32 kind)
 : Renderable(0), group(nil), kind(kind), numControllers(numCtrls)
{
	typeMask = TYPE_LIGHTING;
	for(i32 i = 0; i < MAX_CONTROLLERS; i++)
		controllers[i] = nil;
	if(group) group->AddRef();
	this->group = group;
	for(i32 i = 0; i < numCtrls && i < MAX_CONTROLLERS; i++) {
		controllers[i] = ctrls[i];
		if(controllers[i]) controllers[i]->AddRef();
	}

	if(group == nil || gLightManager == nil)
		return;
	gLightManager->RegisterLightGroup(group, kind);
	// retail also registers kind 0 as a lighting zone under GetHash("zone_lights")
	// (LightManager::AddLightingZone 0x00460380); we have no lighting zones.
	if(kind == ZONE || kind == ZONE_RAIN)
		gLightManager->SetZoneRenderable(this, kind == ZONE);
}

// retail: renderer::LightingRenderable::~LightingRenderable 0x00470240
LightingRenderable::~LightingRenderable(void)
{
	if(gLightManager && group) {
		gLightManager->UnregisterLightGroup(group, kind);
		if(gLightManager->zoneRenderable == this) gLightManager->SetZoneRenderable(nil, true);
		if(gLightManager->zoneRainRenderable == this) gLightManager->SetZoneRenderable(nil, false);
	}
	for(i32 i = 0; i < MAX_CONTROLLERS; i++)
		if(controllers[i]) controllers[i]->Release();
	if(group) group->Release();
}

// retail: renderer::SFLightGroupLoader::LoadObject 0x00470280
void
SFLightGroupLoader::LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];

	f->GetString(name);
	u32 uid = core::GetHash(name);

	char groupName[256];
	f->GetString(groupName);
	u32 kind = f->GetU32();
	LightGroup *group = inventory->Find<LightGroup>(groupName);

	i32 numCtrls = (i32)f->GetU32();
	LightAnimationController *ctrls[LightingRenderable::MAX_CONTROLLERS];
	i32 n = 0;
	for(i32 i = 0; i < numCtrls; i++) {
		char ctrlName[256];
		f->GetString(ctrlName);
		LightAnimationController *c = inventory->Find<LightAnimationController>(ctrlName);
		if(c && n < LightingRenderable::MAX_CONTROLLERS)
			ctrls[n++] = c;
	}

	LightingRenderable *r = new LightingRenderable(group, ctrls, n, kind);
	r->SetName(name);
	if(getenv("P3D_VERBOSE"))
		printf("lighting: %s kind %d group %s (%d lights) %d/%d controllers\n",
		       name, kind, groupName, group ? (int)group->lights.size() : -1, n, numCtrls);
	*pObject = r;
	*pUID = uid;
}


// ---------------------------------------------------------------- LightManager

// retail: renderer::LightManager::LightManager 0x00460600
LightManager::LightManager(void)
 : zoneGroup(nil), zoneRainGroup(nil), zoneRenderable(nil), zoneRainRenderable(nil),
   timeOfDay(12.0f), raining(false), animate(true), enabled(true), localLights(true),
   flipX(false), ambient(0u), activeGroup(nil)
{
}

// retail: renderer::LightManager::RegisterLightGroup 0x0045fe10 --- a switch on the kind:
// 0 and 1 are single slots, 2 and 3 are vectors, 4 turns every light of the group into a
// TemplateLight keyed on the light's name hash (street lamps, car headlights).
void
LightManager::RegisterLightGroup(LightGroup *group, u32 kind)
{
	if(group == nil)
		return;
	switch(kind) {
	case LightingRenderable::ZONE:
		LoadObject::Assign(zoneGroup, group);
		break;
	case LightingRenderable::ZONE_RAIN:
		LoadObject::Assign(zoneRainGroup, group);
		break;
	case LightingRenderable::EXTERIOR:
		group->AddRef();
		exteriorGroups.push_back(group);
		break;
	case LightingRenderable::INTERIOR:
		group->AddRef();
		interiorGroups.push_back(group);
		break;
	case LightingRenderable::TEMPLATE:
		group->AddRef();
		for(u32 i = 0; i < group->lights.size(); i++)
			templateLights.push_back(group->lights[i]);
		break;
	}
}

void
LightManager::UnregisterLightGroup(LightGroup *group, u32 kind)
{
	if(group == nil)
		return;
	// an unloaded package takes its lights with it
	for(u32 i = 0; i < active.size(); ) {
		bool gone = false;
		for(u32 j = 0; j < group->lights.size(); j++)
			if(active[i].light == group->lights[j]) gone = true;
		if(gone) active.erase(active.begin()+i);
		else i++;
	}
	if(activeGroup == group)
		activeGroup = nil;

	std::vector<LightGroup*> *v = nil;
	switch(kind) {
	case LightingRenderable::ZONE:
		if(zoneGroup == group) LoadObject::Assign(zoneGroup, (LightGroup*)nil);
		return;
	case LightingRenderable::ZONE_RAIN:
		if(zoneRainGroup == group) LoadObject::Assign(zoneRainGroup, (LightGroup*)nil);
		return;
	case LightingRenderable::EXTERIOR: v = &exteriorGroups; break;
	case LightingRenderable::INTERIOR: v = &interiorGroups; break;
	case LightingRenderable::TEMPLATE:
		for(u32 i = 0; i < group->lights.size(); i++)
			for(u32 j = 0; j < templateLights.size(); j++)
				if(templateLights[j] == group->lights[i]) {
					templateLights.erase(templateLights.begin()+j);
					break;
				}
		group->Release();
		return;
	default:
		return;
	}
	for(u32 i = 0; i < v->size(); i++)
		if((*v)[i] == group) { v->erase(v->begin()+i); group->Release(); break; }
}

// retail: EnvManager 0x0046a270, called from the LightingRenderable ctor through
// 0x0046fcd0 (which compares the group's UID against GetHash("zone_lights") /
// GetHash("zone_rainlights") to pick the slot)
void
LightManager::SetZoneRenderable(LightingRenderable *r, bool clear)
{
	if(clear) zoneRenderable = r;
	else zoneRainRenderable = r;
}

// The LITE animations are 241 frames of a 24 h day. Retail runs the clock through six
// key-frame times with hold and transit bands first (EnvManager::MapTimeOfDay 0x0046a400,
// env+0x34 is the resulting animation time in ms); we map linearly.
float
LightManager::GetFrame(void) const
{
	LightingRenderable *r = raining ? zoneRainRenderable : zoneRenderable;
	float numFrames = 241.0f;
	if(r && r->numControllers > 0 && r->controllers[0])
		numFrames = r->controllers[0]->GetNumFrames();
	float t = timeOfDay/24.0f;
	t -= floorf(t);
	return t*(numFrames - 1.0f);
}

// retail: EnvManager 0x0046b4f0 --- the building ambient is never part of the general
// light set; it belongs to the "buildinglights" world geo (the lit windows at night).
static bool
IsBuildingAmbient(Light *l)
{
	static u32 uid = core::GetHash("MiamiBuildingAmbientShape");
	static u32 rainyUid = core::GetHash("MiamiRainyBuildingAmbientShape");
	u32 u = l->GetUID();
	return u == uid || u == rainyUid;
}

static bool
BrighterFirst(const LightManager::ActiveLight &a, const LightManager::ActiveLight &b)
{
	return a.light->Intensity()*a.decay > b.light->Intensity()*b.decay;
}

// SHR: tLightsChooser::AddLight --- ambient lights accumulate into one colour (there is
// only one hardware ambient), everything else takes a slot
void
LightManager::AddLight(Light *l, float decay)
{
	pddiColour c = l->colour;
	pddiColour lit((u8)(c.R()*decay), (u8)(c.G()*decay), (u8)(c.B()*decay));
	if(l->type == Light::AMBIENT) {
		ambient = pddiColour((u8)std::min(255, ambient.R() + lit.R()),
		                     (u8)std::min(255, ambient.G() + lit.G()),
		                     (u8)std::min(255, ambient.B() + lit.B()));
		return;
	}
	ActiveLight a = { l, lit, decay };
	active.push_back(a);
}

// retail: renderer::LightManager::Update 0x00460130 plus EnvManager 0x0046ba90 --- the
// two together fill a pure3d::LightsChooser with
//   (a) every light of the kind 2 groups that has a decay range and whose position is
//       within 50 m of the camera,                                     [V 0x004601b0]
//   (b) every live template light instance within the same 50 m,       [V 0x00460276]
//       (we have no template light instances: nothing calls AddTemplateLight)
//   (c) the lights of the active zone group (kind 0, or kind 1 when it rains), except
//       the building ambient.                                          [V 0x0046baa9]
// Retail then reduces that set to 4 directional lights PER LIT OBJECT; we keep one set
// for the whole frame, chosen at the camera, and hand it straight to the context.
// The kind 3 (interior) groups are not in retail's loop --- rooms and lighting zones
// bring those in, and the viewer has neither, so it treats them like the kind 2 ones.
void
LightManager::Update(TimeInfo *t)
{
	Vector camPos = View_GetCullingCamera()->GetPosition();

	// play the time-of-day animations onto the zone group's lights
	LightingRenderable *zone = raining ? zoneRainRenderable : zoneRenderable;
	if(animate && zone) {
		float frame = GetFrame();
		for(i32 i = 0; i < zone->numControllers; i++)
			if(zone->controllers[i])
				zone->controllers[i]->SetFrame(frame);
	}

	active.clear();
	ambient = pddiColour(0, 0, 0);
	activeGroup = zone ? zone->group : (raining ? zoneRainGroup : zoneGroup);

	// (c) the zone lights: no decay range, so they reach everywhere
	if(activeGroup)
		for(u32 i = 0; i < activeGroup->lights.size(); i++) {
			Light *l = activeGroup->lights[i];
			if(l == nil || !l->enabled) continue;
			if(l->decayType == Light::NO_DECAY && IsBuildingAmbient(l)) continue;
			AddLight(l, 1.0f);
		}

	// the zone lights keep the first slots: retail ranks every light by its contribution
	// at the lit object, so a lamp only outshines the sun for objects next to it; the
	// viewer has one light set for the whole frame and would let a lamp near the camera
	// push the sun out of the last slot.
	u32 numZoneLights = active.size();

	// (a) the local lights of the exterior groups, and the interior ones on top: the
	// viewer has no rooms, so a shell package's lights are simply in range or not
	if(localLights) {
		const std::vector<LightGroup*> *lists[2] = { &exteriorGroups, &interiorGroups };
		for(int li = 0; li < 2; li++)
			for(u32 g = 0; g < lists[li]->size(); g++) {
				LightGroup *group = (*lists[li])[g];
				for(u32 i = 0; i < group->lights.size(); i++) {
					Light *l = group->lights[i];
					if(l == nil || !l->enabled || l->decayType == Light::NO_DECAY)
						continue;
					float d = Norm(l->position - camPos);
					if(d > (float)LIGHT_RADIUS) continue;
					float decay = l->Decay(camPos);
					if(decay <= 0.0f) continue;
					AddLight(l, decay);
				}
			}
	}

	std::sort(active.begin()+numZoneLights, active.end(), BrighterFirst);
	Apply();

	// P3D_VERBOSE: one dump of what the frame is lit with
	static bool printed = false;
	if(!printed && getenv("P3D_VERBOSE") && activeGroup) {
		printed = true;
		printf("lighting: active group \"%s\"%s, %.2fh (frame %.1f), ambient %d,%d,%d\n",
		       activeGroup->GetName(), raining ? " (rain)" : "", timeOfDay, GetFrame(),
		       ambient.R(), ambient.G(), ambient.B());
		for(u32 i = 0; i < active.size(); i++) {
			Light *l = active[i].light;
			pddiColour c = active[i].colour;
			printf("  %-30s %-11s %3d,%3d,%3d  dir %.3f %.3f %.3f  decay %.2f%s\n",
			       l->GetName(),
			       l->type == Light::DIRECTIONAL ? "directional" :
			       l->type == Light::POINT ? "point" : l->type == Light::SPOT ? "spot" : "ambient",
			       c.R(), c.G(), c.B(),
			       l->direction.x, l->direction.y, l->direction.z, active[i].decay,
			       i < (u32)context->GetMaxLights() ? "" : "  (no slot)");
		}
	}
}

void
LightManager::Apply(void)
{
	if(context == nil)
		return;
	int maxLights = context->GetMaxLights();

	if(!enabled) {
		// the View tab's "hardcoded light" fallback: what glShader::SetPass used to do
		context->SetAmbientLight(pddiColour(51, 43, 27));
		pddiLightDesc d(true);
		d.SetDirectionalLight(pddiColour(97, 95, 70), Vector(0.5f, -0.5f, 0.5f));
		context->SetLight(0, &d);
		for(int i = 1; i < maxLights; i++)
			context->EnableLight(i, false);
		return;
	}

	context->SetAmbientLight(ambient);
	int slot = 0;
	for(u32 i = 0; i < active.size() && slot < maxLights; i++) {
		Light *l = active[i].light;
		pddiLightDesc d(true);
		d.colour = active[i].colour;
		// the viewer draws the world with x flipped, the lights are native
		float sx = flipX ? -1.0f : 1.0f;
		if(l->type == Light::DIRECTIONAL) {
			d.type = PDDI_LIGHT_DIRECTIONAL;
			d.direction = Vector(l->direction.x*sx, l->direction.y, l->direction.z);
		} else {
			// point and spot; we have no cone, a spot lights its sphere
			d.type = PDDI_LIGHT_POINT;
			d.position = Vector(l->position.x*sx, l->position.y, l->position.z);
			// the decay range is the falloff. Sphere ranges only use x, the cuboid and
			// ellipsoid ones get their largest extent.
			if(l->decayType == Light::SPHERE_DECAY) {
				d.innerRange = l->decayInner.x;
				d.outerRange = l->decayOuter.x;
			} else if(l->decayType != Light::NO_DECAY) {
				d.innerRange = std::min(l->decayInner.x, std::min(l->decayInner.y, l->decayInner.z));
				d.outerRange = std::max(l->decayOuter.x, std::max(l->decayOuter.y, l->decayOuter.z));
			} else {
				d.innerRange = 0.0f;
				d.outerRange = (float)LIGHT_RADIUS;
			}
			if(d.outerRange <= d.innerRange)
				d.outerRange = d.innerRange + 0.01f;
			// the shader does the falloff, so use the light's own colour
			d.colour = l->colour;
		}
		context->SetLight(slot, &d);
		slot++;
	}
	for(int i = slot; i < maxLights; i++)
		context->EnableLight(i, false);
}

}
