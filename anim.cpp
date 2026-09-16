// pure3d::Animation (chunk 0x00121000) and the frame controller family (0x00121201).
// The chunk formats and where they were read out of are in re/notes/sky.md ��4 and
// re/notes/lighting.md; the SHR ancestors are p3d/anim/{animate,channel,poseanimation,
// billboardobjectanimation,lightanimation}.cpp.

#include "anim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace pure3d
{

// ---------------------------------------------------------------- Channel

// SHR: tChannel::FindBracketKeys. Returns false when the frame is outside the key range;
// then *a == *b and the caller takes that key's value unchanged.
bool
Animation::Channel::FindKeys(float frame, i32 *a, i32 *b, float *t) const
{
	i32 n = (i32)frames.size();
	*t = 0.0f;
	if(n <= 0) { *a = *b = 0; return false; }
	if(frame <= frames[0]) { *a = *b = 0; return false; }
	if(frame >= frames[n-1]) { *a = *b = n-1; return false; }
	i32 i = 0;
	while(i+1 < n && frame >= frames[i+1])
		i++;
	*a = i;
	*b = i+1;
	float d = frames[i+1] - frames[i];
	*t = d > 0.0f ? (frame - frames[i])/d : 0.0f;
	return true;
}

float
Animation::Channel::GetFloat(float frame) const
{
	i32 a, b; float t;
	if(values.empty()) return 0.0f;
	if(!FindKeys(frame, &a, &b, &t) || !interpolate)
		return values[a];
	return values[a] + (values[b] - values[a])*t;
}

Vector2
Animation::Channel::GetVector2(float frame) const
{
	i32 a, b; float t;
	Vector2 v; v.x = v.y = 0.0f;
	if(values.size() < 2) return v;
	if(!FindKeys(frame, &a, &b, &t) || !interpolate) {
		v.x = values[a*2]; v.y = values[a*2+1];
		return v;
	}
	v.x = values[a*2+0] + (values[b*2+0] - values[a*2+0])*t;
	v.y = values[a*2+1] + (values[b*2+1] - values[a*2+1])*t;
	return v;
}

// SHR: tVector1DOFChannel / tVector2DOFChannel / tVector3DOFChannel::GetValue. The 1DOF
// and 2DOF forms store the components that never move in `constants` and only key the
// rest; `mapping` is the moving index for 1DOF and the FROZEN index for 2DOF.
Vector
Animation::Channel::GetVector(float frame) const
{
	static const i32 dynamic2[3][2] = { {1,2}, {0,2}, {0,1} };
	i32 a, b; float t;
	Vector v = constants;
	if(frames.empty()) return v;
	bool lerp = FindKeys(frame, &a, &b, &t) && interpolate;
	float *out = &v.x;
	switch(id) {
	case CH_VECTOR1DOF: {
		if(values.empty()) break;
		i32 i = mapping < 3 ? (i32)mapping : 0;
		out[i] = lerp ? values[a] + (values[b] - values[a])*t : values[a];
		break;
	}
	case CH_VECTOR2DOF: {
		if(values.size() < 2) break;
		i32 m = mapping < 3 ? (i32)mapping : 0;
		i32 i0 = dynamic2[m][0], i1 = dynamic2[m][1];
		out[i0] = lerp ? values[a*2+0] + (values[b*2+0] - values[a*2+0])*t : values[a*2+0];
		out[i1] = lerp ? values[a*2+1] + (values[b*2+1] - values[a*2+1])*t : values[a*2+1];
		break;
	}
	default: {
		if(values.size() < 3) break;
		for(i32 i = 0; i < 3; i++)
			out[i] = lerp ? values[a*3+i] + (values[b*3+i] - values[a*3+i])*t : values[a*3+i];
		break;
	}
	}
	return v;
}

// SHR: tQuaternionChannel::GetQuaternion --- a slerp between the bracketing keys. The
// shortest-arc sign fix matters here: the sun's 21 keys flip sign between frames 153 and
// 154 (re/notes/sky.md), and without it the sun would jump across the sky at 15:20.
Quaternion
Animation::Channel::GetQuaternion(float frame) const
{
	i32 a, b; float t;
	if(values.size() < 4) return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
	bool lerp = FindKeys(frame, &a, &b, &t) && interpolate;
	Quaternion qa(values[a*4+0], values[a*4+1], values[a*4+2], values[a*4+3]);
	if(!lerp)
		return qa;
	Quaternion qb(values[b*4+0], values[b*4+1], values[b*4+2], values[b*4+3]);
	float dot = qa.x*qb.x + qa.y*qb.y + qa.z*qb.z + qa.w*qb.w;
	if(dot < 0.0f) { qb = -qb; dot = -dot; }
	float wa = 1.0f - t, wb = t;
	if(dot < 0.9995f) {
		float angle = acosf(dot > 1.0f ? 1.0f : dot);
		float s = sinf(angle);
		if(s > 1.0e-6f) {
			wa = sinf((1.0f - t)*angle)/s;
			wb = sinf(t*angle)/s;
		}
	}
	Quaternion q = qa*wa + qb*wb;
	float len = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
	return len > 0.0f ? q/len : qa;
}

// The key is the file's D3DCOLOR word, kept exactly as it is on disk --- which is the byte
// order a vertex colour has in this code base (the GL shader reads `col.bgr`), so a quad
// colour goes straight in. A LIGHT colour is a uniform and wants red in the low byte;
// pure3d::LightLoader swaps it there and so does LightAnimationController.
pddiColour
Animation::Channel::GetColour(float frame) const
{
	i32 a, b; float t;
	if(colours.empty()) return pddiColour((u32)0xFFFFFFFF);
	if(!FindKeys(frame, &a, &b, &t) || !interpolate)
		return pddiColour(colours[a]);
	u32 x = colours[a], y = colours[b], out = 0;
	for(int i = 0; i < 4; i++) {
		float p = (float)((x>>(i*8))&0xFF), q = (float)((y>>(i*8))&0xFF);
		out |= (u32)(p + (q - p)*t) << (i*8);
	}
	return pddiColour(out);
}

// SHR: tBoolChannel::GetValue --- the keys are TOGGLES, not values
bool
Animation::Channel::GetBool(float frame) const
{
	bool v = startState;
	for(u32 i = 0; i < frames.size(); i++) {
		if(frame < frames[i])
			break;
		v = !v;
	}
	return v;
}

float
Animation::Channel::GetInt(float frame) const
{
	i32 a, b; float t;
	if(ints.empty()) return 0.0f;
	if(!FindKeys(frame, &a, &b, &t) || !interpolate)
		return (float)ints[a];
	return (float)ints[a] + (float)(ints[b] - ints[a])*t;
}


const Animation::Channel *
Animation::Group::Find(u32 param) const
{
	for(u32 i = 0; i < channels.size(); i++)
		if(channels[i].param == param)
			return &channels[i];
	return nil;
}

const Animation::Group *
Animation::FindGroup(const char *name) const
{
	u32 uid = GetHash(name);
	for(u32 i = 0; i < groups.size(); i++)
		if(GetHash(groups[i].name.c_str()) == uid)
			return &groups[i];
	return nil;
}

float
Animation::MakeValidFrame(float frame) const
{
	if(numFrames <= 0.0f)
		return 0.0f;
	if(cyclic) {
		frame = fmodf(frame, numFrames);
		if(frame < 0.0f) frame += numFrames;
		return frame;
	}
	if(frame < 0.0f) return 0.0f;
	if(frame > numFrames) return numFrames;
	return frame;
}


// ---------------------------------------------------------------- the loader

// SHR: tChannelLoader::LoadChannel. Every channel is `u32 version; u32 param;` then a
// per-type body; all of them end with `u32 nKeys; u16 frames[nKeys];` and the values.
//                                                                              [V]
static void
LoadChannel(ChunkFile *f, u32 id, Animation::Channel *ch)
{
	ch->id = id;
	f->GetU32();				// version (1 for CH_QUATERNION)
	ch->param = f->GetU32();

	if(id == Animation::CH_BOOL)
		ch->startState = f->GetU16() == 1;
	if(id == Animation::CH_VECTOR1DOF || id == Animation::CH_VECTOR2DOF) {
		ch->mapping = f->GetU16();
		ch->constants.x = f->GetFloat();
		ch->constants.y = f->GetFloat();
		ch->constants.z = f->GetFloat();
	}

	u32 n = f->GetU32();
	ch->frames.resize(n);
	for(u32 i = 0; i < n; i++)
		ch->frames[i] = (float)f->GetU16();

	u32 perKey = 0;
	switch(id) {
	case Animation::CH_FLOAT1:	perKey = 1; break;
	case Animation::CH_FLOAT2:	perKey = 2; break;
	case Animation::CH_VECTOR1DOF:	perKey = 1; break;
	case Animation::CH_VECTOR2DOF:	perKey = 2; break;
	case Animation::CH_VECTOR3DOF:	perKey = 3; break;
	case Animation::CH_QUATERNION:	perKey = 4; break;
	default: break;
	}
	if(perKey) {
		ch->values.resize(n*perKey);
		for(u32 i = 0; i < n*perKey; i++)
			ch->values[i] = f->GetFloat();
	} else if(id == Animation::CH_COLOUR) {
		ch->colours.resize(n);
		for(u32 i = 0; i < n; i++)
			ch->colours[i] = f->GetU32();
	} else if(id == Animation::CH_INT) {
		ch->ints.resize(n);
		for(u32 i = 0; i < n; i++)
			ch->ints[i] = f->GetI32();
	}
	// CH_BOOL has no values at all, CH_STRING/CH_ENTITY/CH_EVENT are not decoded

	while(f->ChunksRemaining()) {
		if(f->BeginChunk() == Animation::CH_INTERPOLATION_MODE) {
			f->GetU32();		// version
			ch->interpolate = f->GetU32() != 0;
		}
		f->EndChunk();
	}
}

// retail: the 0x00121000 loader. z04 has 1499 of these, of a dozen types; every type is
// decoded the same way, the meaning is the frame controller's business.
void
AnimationLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256];

	f->GetU32();				// version
	f->GetString(name);

	Animation *anim = new Animation;
	anim->SetName(name);
	anim->type = f->GetU32();
	anim->numFrames = f->GetFloat();
	anim->speed = f->GetFloat();
	anim->cyclic = f->GetU32() == 1;

	while(f->ChunksRemaining()) {
		u32 id = f->BeginChunk();
		if(id == Animation::GROUP_LIST) {
			f->GetU32();		// version
			u32 numGroups = f->GetU32();
			for(u32 g = 0; g < numGroups && f->ChunksRemaining(); g++) {
				if(f->BeginChunk() != Animation::GROUP) {
					f->EndChunk();
					continue;
				}
				f->GetU32();	// version
				f->GetString(name);
				anim->groups.push_back(Animation::Group());
				Animation::Group &group = anim->groups.back();
				group.name = name;
				group.id = f->GetU32();
				u32 numChannels = f->GetU32();
				for(u32 c = 0; c < numChannels && f->ChunksRemaining(); c++) {
					u32 cid = f->BeginChunk();
					group.channels.push_back(Animation::Channel());
					LoadChannel(f, cid, &group.channels.back());
					f->EndChunk();
				}
				f->EndChunk();
			}
		}
		// 0x00121006/0x00121007 is Scarface's replacement for SHR's 0x00121004 SIZE:
		// { u32 version; u32 numEntries; } + per entry { u32 version; u32 channelChunkId;
		// u32 numChannels; u16 numKeys[numChannels]; } --- a preallocation hint only.
		f->EndChunk();
	}

	if(getenv("P3D_VERBOSE"))
		printf("anim %s: type %c%c%c%c, %g frames %g fps%s, %d groups\n",
			anim->GetName(), anim->type&0xFF, (anim->type>>8)&0xFF,
			(anim->type>>16)&0xFF, (anim->type>>24)&0xFF ? (anim->type>>24)&0xFF : ' ',
			anim->numFrames, anim->speed, anim->cyclic ? " cyclic" : "",
			(int)anim->groups.size());

	*pObject = anim;
	*pUID = anim->GetUID();
}


// ---------------------------------------------------------------- frame controllers

FrameController::~FrameController(void)
{
	Release(animation);
}

void
FrameController::SetAnimation(Animation *a)
{
	Assign(animation, a);
}

// The 0x00121201 chunk (version 1): pstring name, u32 animation type, u32 'ANIM',
// float frameOffset, u32 (always 1), pstring target, pstring animation.        [V]
// SHR's 0x121200 is the same without the 'ANIM' word and the trailing u32.
void
ReadFrameControllerInfo(ChunkFile *f, FrameControllerInfo *info)
{
	f->GetU32();			// version
	f->GetString(info->name);
	info->type = f->GetU32();
	f->GetU32();			// 'ANIM'
	info->frameOffset = f->GetFloat();
	f->GetU32();			// always 1
	f->GetString(info->target);
	f->GetString(info->animName);
}


// ---------------------------------------------------------------- pose animation

void
PoseAnimationController::SetPose(Pose *p, Skeleton *s)
{
	pose = p;
	skeleton = s;
	jointGroup.clear();
	if(pose == nil || skeleton == nil || animation == nil)
		return;
	// SHR looks the group up by the joint's UID on every frame; the skeleton's UIDs are
	// MakeKey(jointName) in this code base (re/README.md), so hash the group names the
	// same way and bind once
	i32 n = skeleton->GetNumJoints();
	jointGroup.resize(n, -1);
	for(i32 i = 0; i < n; i++) {
		u32 uid = skeleton->GetJointUID(i);
		for(u32 g = 0; g < animation->groups.size(); g++)
			if(MakeKey(animation->groups[g].name.c_str()) == uid) {
				jointGroup[i] = (i32)g;
				break;
			}
	}
}

// SHR: tPoseAnimationController::UpdateNoBlending --- write the animated translation and
// rotation into the pose's joints and leave the skeleton's rest pose where there is no
// channel. Retail then re-evaluates the pose lazily; we do it here.
void
PoseAnimationController::SetFrame(float frame)
{
	if(pose == nil || skeleton == nil || animation == nil || jointGroup.empty())
		return;
	frame = animation->MakeValidFrame(frame + frameOffset);
	i32 n = skeleton->GetNumJoints();
	bool any = false;
	for(i32 i = 0; i < n; i++) {
		const Animation::Group *group = jointGroup[i] < 0 ? nil : &animation->groups[jointGroup[i]];
		Vector pos = *skeleton->GetPosition(i);
		Quaternion rot = *skeleton->GetRotation(i);
		if(group) {
			const Animation::Channel *tran = group->Find(Animation::CHANNEL_TRANSLATION);
			const Animation::Channel *r = group->Find(Animation::CHANNEL_ROTATION);
			if(tran) pos = tran->GetVector(frame);
			if(r) rot = r->GetQuaternion(frame);
			any = true;
		}
		pose->SetJointPosition(i, pos);
		pose->SetJointRotation(i, rot);
	}
	if(any)
		pose->Evaluate(nil);
}

}
