// pure3d::Light / LightGroup and the LITE (time-of-day) animation.
// Chunk formats and the retail addresses are written down in re/notes/lighting.md.

#include "light.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace pure3d
{

static float
SmoothClamp(float t)
{
	if(t <= 0.0f) return 0.0f;
	if(t >= 1.0f) return 1.0f;
	return t*t*(3.0f - 2.0f*t);
}


Light::Light(void)
 : type(AMBIENT), colour(0u), position(0.0f, 0.0f, 0.0f), direction(0.0f, 0.0f, 1.0f),
   phi(0.0f), theta(0.0f), falloff(0.0f), range(0.0f),
   decayType(NO_DECAY), decayInner(0.0f, 0.0f, 0.0f), decayOuter(0.0f, 0.0f, 0.0f),
   decayRotationY(0.0f), illuminationType(POSITIVE_ILLUMINANT),
   enabled(true), isShadowCaster(false)
{
	attenuation[0] = 1.0f;
	attenuation[1] = 0.0f;
	attenuation[2] = 0.0f;
}

// SHR: tLight::Decay
float
Light::Decay(const Vector &samplePosition) const
{
	switch(decayType) {
	case NO_DECAY:
	default:
		return 1.0f;

	case SPHERE_DECAY: {
		// only the x component of the ranges is used
		float in = decayInner.x, out = decayOuter.x;
		float d = Norm(samplePosition - position);
		if(d <= in) return 1.0f;
		if(d >= out) return 0.0f;
		return SmoothClamp((out - d) / (out - in));
	}

	// SHR treats the ellipsoid range as a cuboid range too ("HBW TODO")
	case CUBOID_DECAY:
	case ELLIPSOID_DECAY: {
		Vector o = samplePosition - position;
		if(fabsf(o.y) >= decayOuter.y) return 0.0f;
		float s = sinf(decayRotationY), c = cosf(decayRotationY);
		if(s != 0.0f) {
			float x = c*o.x + s*o.z;
			o.z = c*o.z - s*o.x;
			o.x = x;
		}
		o.x = fabsf(o.x); o.y = fabsf(o.y); o.z = fabsf(o.z);
		if(o.x >= decayOuter.x || o.z >= decayOuter.z) return 0.0f;
		float decay = 1.0f;
		if(o.x > decayInner.x && decayOuter.x > decayInner.x)
			decay = SmoothClamp((decayOuter.x - o.x) / (decayOuter.x - decayInner.x));
		if(o.y > decayInner.y && decayOuter.y > decayInner.y) {
			float d = SmoothClamp((decayOuter.y - o.y) / (decayOuter.y - decayInner.y));
			if(d < decay) decay = d;
		}
		if(o.z > decayInner.z && decayOuter.z > decayInner.z) {
			float d = SmoothClamp((decayOuter.z - o.z) / (decayOuter.z - decayInner.z));
			if(d < decay) decay = d;
		}
		return decay;
	}
	}
}

float
Light::Intensity(void) const
{
	pddiColour c = colour;
	return (c.R() + c.G() + c.B()) / (3.0f*255.0f);
}


// retail: pure3d::LightLoader::LoadObject, chunk 0x00013000
void
LightLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256];
	f->GetString(name);

	Light *light = new Light;
	light->SetName(name);

	u32 version = f->GetU32();	// 0x101 everywhere in z04
	light->type = f->GetU32();
	u32 col = f->GetU32();		// 0xAARRGGBB
	light->colour = pddiColour((col>>16)&0xFF, (col>>8)&0xFF, col&0xFF, (col>>24)&0xFF);
	light->attenuation[0] = f->GetFloat();
	light->attenuation[1] = f->GetFloat();
	light->attenuation[2] = f->GetFloat();
	light->enabled = f->GetU32() != 0;

	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()) {
		case Light::DIRECTION:
			light->direction.x = f->GetFloat();
			light->direction.y = f->GetFloat();
			light->direction.z = f->GetFloat();
			break;
		case Light::POSITION:
			light->position.x = f->GetFloat();
			light->position.y = f->GetFloat();
			light->position.z = f->GetFloat();
			break;
		case Light::CONE_PARAM:
			light->phi = f->GetFloat();
			light->theta = f->GetFloat();
			light->falloff = f->GetFloat();
			light->range = f->GetFloat();
			break;
		case Light::SHADOW:
			light->isShadowCaster = f->GetU32() != 0;
			break;
		case Light::DECAY_RANGE:
			light->decayType = f->GetU32();
			light->decayInner.x = f->GetFloat();
			light->decayInner.y = f->GetFloat();
			light->decayInner.z = f->GetFloat();
			light->decayOuter.x = f->GetFloat();
			light->decayOuter.y = f->GetFloat();
			light->decayOuter.z = f->GetFloat();
			while(f->ChunksRemaining()) {
				if(f->BeginChunk() == Light::DECAY_RANGE_ROTATION_Y)
					light->decayRotationY = f->GetFloat();
				f->EndChunk();
			}
			break;
		case Light::ILLUMINATION_TYPE:
			light->illuminationType = f->GetU32();
			break;
		}
		f->EndChunk();
	}

	*pObject = light;
	*pUID = light->GetUID();
}


LightGroup::~LightGroup(void)
{
	for(u32 i = 0; i < lights.size(); i++)
		if(lights[i]) lights[i]->Release();
}

// retail: pure3d::LightGroupLoader::LoadObject, chunk 0x00002380
void
LightGroupLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256];
	f->GetString(name);

	LightGroup *group = new LightGroup;
	group->SetName(name);

	u32 n = f->GetU32();
	for(u32 i = 0; i < n; i++) {
		f->GetString(name);
		Light *l = inventory->Find<Light>(name);
		if(l) {
			l->AddRef();
			group->lights.push_back(l);
		}
	}

	*pObject = group;
	*pUID = group->GetUID();
}


// ---------------------------------------------------------------- LITE animation

LightAnimationController::~LightAnimationController(void)
{
	Release(light);
}

// retail/SHR: tLightAnimationController::UpdateNoBlending. A LITE animation has one group
// per light; ours is bound to a single light, so group 0 is the one.
void
LightAnimationController::SetFrame(float frame)
{
	if(light == nil || animation == nil || animation->groups.empty())
		return;
	frame = animation->MakeValidFrame(frame + frameOffset);
	const Animation::Group *group = &animation->groups[0];
	if(const Animation::Channel *c = group->Find(Animation::CHANNEL_COLOUR)) {
		// the channel hands out the file's D3DCOLOR; a light colour goes to the shader
		// as a uniform, which wants red in the low byte --- the same swap LightLoader
		// does on the 0x13000 colour word
		u32 col = c->GetColour(frame).c;
		light->colour = pddiColour((col>>16)&0xFF, (col>>8)&0xFF, col&0xFF, (col>>24)&0xFF);
	}
	if(const Animation::Channel *c = group->Find(Animation::CHANNEL_DIR)) {
		Vector dir = c->GetVector(frame);
		float len = Norm(dir);
		if(len > 0.0f)
			light->direction = dir/len;
	}
	if(const Animation::Channel *c = group->Find(Animation::CHANNEL_ENABLE))
		light->enabled = c->GetBool(frame);
}

// The standalone 0x00121201 chunks; only the 'LITE' ones have a target we can resolve
// out of the inventory (a nested controller's target is still being built by its parent).
void
FrameControllerLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	FrameControllerInfo info;
	ReadFrameControllerInfo(f, &info);
	if(info.type != Animation::TYPE_LITE)
		return;

	Light *light = inventory->Find<Light>(info.target);
	Animation *anim = inventory->Find<Animation>(info.animName);
	if(light == nil || anim == nil)
		return;

	LightAnimationController *ctrl = new LightAnimationController;
	ctrl->SetName(info.name);
	ctrl->frameOffset = info.frameOffset;
	light->AddRef();
	ctrl->light = light;
	ctrl->SetAnimation(anim);

	*pObject = ctrl;
	*pUID = ctrl->GetUID();
}

}
