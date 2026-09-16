#pragma once

#include "gmath.h"
#include "entity.h"
#include "loadmanager.h"
#include "pddi.h"
#include "anim.h"

#include <vector>

namespace pure3d
{

using namespace math;
using namespace core;
using namespace content;

// retail: pure3d::Light, vtable 0x0076a15c (RTTI ??_R4Light@pure3d@@), with the three
// subclasses AmbientLight (0x76a21c), DirectionalLight/PointLight (0x76a1cc) and
// SpotLight (0x76a26c). Retail keeps them apart only for Update(): AmbientLight::Update
// (0x6894a0) is `renderContext->SetAmbientLight(colour)` and PointLight::Update
// (0x6893a0) builds a pddiLightDesc on the stack and calls `renderContext->SetLight(slot,
// &desc)` (d3dContext vtable +0xac / +0xb4, see re/notes/lighting.md §4). One class with
// a type field is enough for us.
class Light : public Entity
{
public:
	enum {
		LIGHT			= 0x00013000,
		DIRECTION		= 0x00013001,
		POSITION		= 0x00013002,
		CONE_PARAM		= 0x00013003,
		SHADOW			= 0x00013004,
		PHOTON_MAP		= 0x00013005,
		DECAY_RANGE		= 0x00013006,
		DECAY_RANGE_ROTATION_Y	= 0x00013007,
		ILLUMINATION_TYPE	= 0x00013008
	};
	// the `type` word of the chunk
	enum Type { AMBIENT = 0, POINT = 1, DIRECTIONAL = 2, SPOT = 3 };
	// the `type` word of the 0x13006 sub-chunk. The zone (sun/ambient/fill) lights have
	// no decay chunk at all, every local light has one; renderer::LightManager uses
	// exactly that to tell the two apart, as retail does.
	enum DecayType { NO_DECAY = 0, SPHERE_DECAY = 1, CUBOID_DECAY = 2, ELLIPSOID_DECAY = 3 };
	enum IlluminationType { POSITIVE_ILLUMINANT = 0, ZERO_ILLUMINANT = 1, NEGATIVE_ILLUMINANT = 2 };

	u32 type;
	pddiColour colour;
	Vector position;
	Vector direction;
	float attenuation[3];		// constant, linear, quadratic
	float phi, theta, falloff, range;	// spot cone
	u32 decayType;
	Vector decayInner, decayOuter;
	float decayRotationY;
	u32 illuminationType;
	bool enabled;
	bool isShadowCaster;

	CLASSNAME(Light);
	Light(void);

	// retail/SHR: tLight::Decay --- 1 inside the inner range, a smoothstep down to 0 at
	// the outer range, 0 outside. Ellipsoid is treated as cuboid, as in the SHR source.
	float Decay(const Vector &samplePosition) const;
	// the light's contribution as a scalar, used to rank lights
	float Intensity(void) const;
};

class LightLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(LightLoader)
	LightLoader(void) : SimpleChunkHandler(Light::LIGHT) {}
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory);
};

// retail: pure3d::LightGroup, vtable 0x0076a190 (7 slots, nothing beyond Entity),
// loader vtable 0x0076b23c. A named array of Lights; 152 of them in z04.
class LightGroup : public Entity
{
public:
	enum { LIGHT_GROUP = 0x00002380 };

	std::vector<Light*> lights;

	CLASSNAME(LightGroup);
	~LightGroup(void);
};

class LightGroupLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(LightGroupLoader)
	LightGroupLoader(void) : SimpleChunkHandler(LightGroup::LIGHT_GROUP) {}
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory);
};


// ---------------------------------------------------------------- time of day

// The four LITE_Miami*Shape animations in miami_lod.p3d / islands_LOD.p3d are the
// time-of-day curve: 241 frames at 30 fps = one 24 h day, one frame every six minutes.
// Retail loads them with the generic pure3d::Animation loader (chunk 0x00121000, anim.h)
// and plays them through pure3d::LightAnimationController (chunk 0x00121201, frame
// controller type 'LITE').
//
// retail: pure3d::LightAnimationController (content::LoadInventory::DynamicCaster vtable
// 0x00737ea4). Binds one LITE animation to one Light; SFLightGroupLoader looks these up
// by name for the kind 0 and kind 1 groups.
class LightAnimationController : public FrameController
{
public:
	Light *light;

	CLASSNAME(LightAnimationController);
	LightAnimationController(void) : light(nil) {}
	~LightAnimationController(void);

	// retail/SHR: tLightAnimationController::UpdateNoBlending --- sample the colour and
	// direction channels and write them into the light
	virtual void SetFrame(float frame);
};

// The generic 0x00121201 handler: only the top-level, standalone frame controllers reach
// it, and of those only the 'LITE' ones become an object. The nested ones (a 'BQG' inside
// a billboard quad group, a 'PTRN' inside a composite drawable, a 'VRTX' inside a mesh)
// are read by their parent loader, which is the only place that knows the target.
class FrameControllerLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(FrameControllerLoader)
	FrameControllerLoader(void) : SimpleChunkHandler(Animation::FRAME_CONTROLLER) {}
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory);
};

}
