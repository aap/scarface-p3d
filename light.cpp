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

// the channels are not sorted keys with a search structure, just an ascending frame list
template <class KEY> static i32
FindKey(const std::vector<KEY> &keys, float frame, float *t)
{
	i32 n = (i32)keys.size();
	if(n == 0) return -1;
	if(frame <= keys[0].frame) { *t = 0.0f; return 0; }
	for(i32 i = 1; i < n; i++)
		if(frame < keys[i].frame) {
			float d = keys[i].frame - keys[i-1].frame;
			*t = d > 0.0f ? (frame - keys[i-1].frame) / d : 0.0f;
			return i-1;
		}
	*t = 0.0f;
	return n-1;
}

bool
LightAnimation::GetColour(float frame, pddiColour *out) const
{
	float t;
	i32 i = FindKey(colourKeys, frame, &t);
	if(i < 0) return false;
	pddiColour a = colourKeys[i].colour;
	pddiColour b = (i+1 < (i32)colourKeys.size()) ? colourKeys[i+1].colour : a;
	*out = pddiColour((u8)(a.R() + (b.R() - a.R())*t),
	                  (u8)(a.G() + (b.G() - a.G())*t),
	                  (u8)(a.B() + (b.B() - a.B())*t),
	                  (u8)(a.A() + (b.A() - a.A())*t));
	return true;
}

bool
LightAnimation::GetDirection(float frame, Vector *out) const
{
	float t;
	i32 i = FindKey(dirKeys, frame, &t);
	if(i < 0) return false;
	Vector a = dirKeys[i].v;
	Vector b = (i+1 < (i32)dirKeys.size()) ? dirKeys[i+1].v : a;
	Vector v = a + (b - a)*t;
	float len = Norm(v);
	*out = len > 0.0f ? v/len : a;
	return true;
}

// SHR: tChannelLoader::LoadColourChannel / LoadVectorChannel --- u32 version, u32 param,
// u32 nKeys, u16 frames[nKeys], then the values
void
LightAnimationLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256];

	u32 version = f->GetU32();
	f->GetString(name);
	u32 animType = f->GetU32();
	float numFrames = f->GetFloat();
	float speed = f->GetFloat();
	bool cyclic = f->GetU32() == 1;
	// z04 has 1499 animations, of a dozen types; only the time-of-day ones are ours
	if(animType != LightAnimation::TYPE_LITE)
		return;

	LightAnimation *anim = new LightAnimation;
	anim->SetName(name);
	anim->numFrames = numFrames;
	anim->speed = speed;
	anim->cyclic = cyclic;

	while(f->ChunksRemaining()) {
		if(f->BeginChunk() == LightAnimation::GROUP_LIST) {
			u32 groupVersion = f->GetU32();
			u32 numGroups = f->GetU32();
			for(u32 g = 0; g < numGroups && f->ChunksRemaining(); g++) {
				f->BeginChunk();	// GROUP
				f->GetU32();		// version
				f->GetString(name);
				f->GetU32();		// group id
				u32 numChannels = f->GetU32();
				for(u32 c = 0; c < numChannels && f->ChunksRemaining(); c++) {
					u32 id = f->BeginChunk();
					if(id == LightAnimation::COLOUR) {
						f->GetU32();	// version
						u32 param = f->GetU32();
						u32 n = f->GetU32();
						std::vector<float> frames(n);
						for(u32 k = 0; k < n; k++) frames[k] = f->GetU16();
						for(u32 k = 0; k < n; k++) {
							u32 col = f->GetU32();
							LightAnimation::ColourKey key;
							key.frame = frames[k];
							key.colour = pddiColour((col>>16)&0xFF, (col>>8)&0xFF, col&0xFF, (col>>24)&0xFF);
							if(param == LightAnimation::CHANNEL_COLOUR)
								anim->colourKeys.push_back(key);
						}
					} else if(id == LightAnimation::VECTOR_3DOF) {
						f->GetU32();	// version
						u32 param = f->GetU32();
						u32 n = f->GetU32();
						std::vector<float> frames(n);
						for(u32 k = 0; k < n; k++) frames[k] = f->GetU16();
						for(u32 k = 0; k < n; k++) {
							LightAnimation::VectorKey key;
							key.frame = frames[k];
							key.v.x = f->GetFloat();
							key.v.y = f->GetFloat();
							key.v.z = f->GetFloat();
							if(param == LightAnimation::CHANNEL_DIR)
								anim->dirKeys.push_back(key);
						}
					}
					f->EndChunk();
				}
				f->EndChunk();
			}
		}
		f->EndChunk();
	}

	if(getenv("P3D_VERBOSE"))
		printf("light anim: %s %g frames %g fps, %d colour keys, %d dir keys\n",
		       anim->GetName(), anim->numFrames, anim->speed,
		       (int)anim->colourKeys.size(), (int)anim->dirKeys.size());

	*pObject = anim;
	*pUID = anim->GetUID();
}


LightAnimationController::~LightAnimationController(void)
{
	if(light) light->Release();
	if(animation) animation->Release();
}

// retail/SHR: tLightAnimationController::UpdateNoBlending
void
LightAnimationController::SetFrame(float frame)
{
	if(light == nil || animation == nil)
		return;
	frame += frameOffset;
	pddiColour col;
	if(animation->GetColour(frame, &col))
		light->colour = col;
	Vector dir;
	if(animation->GetDirection(frame, &dir))
		light->direction = dir;
}

// The Scarface frame-controller chunk 0x00121201 (version 1): pstring name, u32 type
// ('LITE'), u32 'ANIM', float frameOffset, u32 (always 1), pstring target, pstring
// animation. SHR's 0x121200 is the same without the 'ANIM' word and the trailing u32.
void
FrameControllerLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256], target[256], animName[256];

	u32 version = f->GetU32();
	f->GetString(name);
	u32 type = f->GetU32();
	if(type != LightAnimation::TYPE_LITE)
		return;
	f->GetU32();		// 'ANIM'
	float frameOffset = f->GetFloat();
	f->GetU32();		// always 1
	f->GetString(target);
	f->GetString(animName);

	Light *light = inventory->Find<Light>(target);
	LightAnimation *anim = inventory->Find<LightAnimation>(animName);
	if(light == nil || anim == nil)
		return;

	LightAnimationController *ctrl = new LightAnimationController;
	ctrl->SetName(name);
	ctrl->frameOffset = frameOffset;
	light->AddRef();
	ctrl->light = light;
	anim->AddRef();
	ctrl->animation = anim;

	*pObject = ctrl;
	*pUID = ctrl->GetUID();
}

}
