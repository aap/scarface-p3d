#pragma once

#include "gmath.h"
#include "entity.h"
#include "loadmanager.h"
#include "skeleton.h"
#include "pddi.h"

#include <string>
#include <vector>

namespace pure3d
{

using namespace math;
using namespace core;
using namespace content;

class CompositeDrawable;

// retail/SHR: pure3d::Animation (SHR tAnimation, p3d/anim/animate.cpp), chunk 0x00121000.
// One animation is a type four-CC, a frame count and a speed, and a list of named groups;
// each group is a list of typed channels, each tagged with a parameter four-CC. What the
// groups and the parameters MEAN is up to the frame controller that plays the animation
// (re/notes/sky.md ��4): a 'LITE' group is a light, a 'BQG' group is one billboard quad, a
// 'PTRN' group is a skeleton joint, a 'VRTX' group is a prim group of a mesh.
//
// Scarface pads the three-letter four-CCs with a NUL where SHR uses a space, and bumped
// the quaternion channel to version 1 (the QUATERNION_FORMAT sub-chunk is gone, the keys
// are always four plain floats).
class Animation : public Entity
{
public:
	enum {
		ANIMATION	= 0x00121000,
		GROUP		= 0x00121001,
		GROUP_LIST	= 0x00121002,
		SIZE		= 0x00121004,	// SHR's per-platform block sizes
		SIZE_LIST	= 0x00121006,	// Scarface's replacement, see the loader
		SIZE_ENTRY	= 0x00121007,

		CH_FLOAT1	= 0x00121100,
		CH_FLOAT2	= 0x00121101,
		CH_VECTOR1DOF	= 0x00121102,
		CH_VECTOR2DOF	= 0x00121103,
		CH_VECTOR3DOF	= 0x00121104,
		CH_QUATERNION	= 0x00121105,
		CH_STRING	= 0x00121106,
		CH_ENTITY	= 0x00121107,
		CH_BOOL		= 0x00121108,
		CH_COLOUR	= 0x00121109,
		CH_EVENT	= 0x0012110A,
		CH_INT		= 0x0012110E,
		CH_QUAT_FORMAT	= 0x0012110F,
		CH_INTERPOLATION_MODE = 0x00121110,
		CH_COMPRESSED_QUATERNION = 0x00121111,

		// the Scarface frame controller. Retail's generic one is 0x00121200; 0x121201
		// is the version-1 variant with an extra 'ANIM' tag word.
		FRAME_CONTROLLER = 0x00121201,
		// the wrapper a billboard quad group puts its frame controllers in
		CONTROLLER_LIST	= 0x00121204
	};

	// animation types (SHR: Pure3DAnimationTypes)                            [V]
	enum {
		TYPE_LITE	= FOURCC("LITE"),	// a pure3d::Light
		TYPE_BQG	= FOURCC("BQG"),	// 'BQG\0', a BillboardQuadGroup
		TYPE_PTRN	= FOURCC("PTRN"),	// a pose (skeleton) transform
		TYPE_PVIS	= FOURCC("PVIS"),	// pose visibility
		TYPE_VRTX	= FOURCC("VRTX"),	// vertex (morph) animation
		TYPE_PSYS	= FOURCC("PSYS"),	// particle system
		TYPE_TEX	= FOURCC("TEX"),
		TYPE_SHAD	= FOURCC("SHAD")
	};

	// channel parameters (SHR: Pure3DAnimationChannels)                      [V]
	enum {
		// Light
		CHANNEL_COLOUR	= FOURCC("CLR"),	// 'CLR\0'
		CHANNEL_DIR	= FOURCC("DIR"),	// 'DIR\0'
		CHANNEL_PARAM	= FOURCC("PARM"),
		CHANNEL_ENABLE	= FOURCC("EABL"),
		// BillboardObjects
		CHANNEL_VISIBILITY = FOURCC("VIS"),	// 'VIS\0'
		CHANNEL_TRANSLATION = FOURCC("TRAN"),
		CHANNEL_ROTATION = FOURCC("ROT"),	// 'ROT\0'
		CHANNEL_WIDTH	= FOURCC("WDT"),	// 'WDT\0'
		CHANNEL_HEIGHT	= FOURCC("HGT"),	// 'HGT\0'
		CHANNEL_DISTANCE = FOURCC("DIST"),
		CHANNEL_UVOFFSET = FOURCC("OFF"),	// 'OFF\0'
		CHANNEL_UVOFFSET_RANGE = FOURCC("ORNG"),
		CHANNEL_SOURCE_RANGE = FOURCC("SRNG"),
		CHANNEL_EDGE_RANGE = FOURCC("ERNG"),
		// Scarface only: the uv-animation frame step of the 0x00017008 atlas
		CHANNEL_FRAMESTEP = FOURCC("FSF"),	// 'FSF\0'
		// Vertex
		CHANNEL_VERTEX	= FOURCC("VRTX")
	};

	// One channel: an ascending list of key frames plus one value per key. The value
	// arrays overlap on purpose --- only the one that matches `id` is filled.
	class Channel
	{
	public:
		u32 id;			// the channel's chunk id
		u32 param;		// the parameter four-CC
		bool interpolate;	// 0x00121110
		u32 mapping;		// 1DOF: the moving index; 2DOF: the STATIC index
		Vector constants;	// 1DOF/2DOF: the values of the frozen components
		bool startState;	// BOOL: the value before the first key

		std::vector<float> frames;
		std::vector<float> values;	// 1/2/3/4 per key, by id
		std::vector<u32> colours;	// CH_COLOUR
		std::vector<i32> ints;		// CH_INT

		Channel(void) : id(0), param(0), interpolate(true), mapping(0),
			constants(0.0f, 0.0f, 0.0f), startState(false) {}

		u32 NumKeys(void) const { return (u32)frames.size(); }
		// SHR: tChannel::FindBracketKeys --- returns false (and a == b) outside the
		// key range, which is also how a non-interpolating channel is sampled
		bool FindKeys(float frame, i32 *a, i32 *b, float *t) const;

		float GetFloat(float frame) const;
		Vector2 GetVector2(float frame) const;
		Vector GetVector(float frame) const;
		Quaternion GetQuaternion(float frame) const;
		pddiColour GetColour(float frame) const;
		bool GetBool(float frame) const;
		// SHR's tIntChannel truncates; we keep the fraction, because Scarface's
		// 'VRTX' channel is a morph frame INDEX and the sky has to cross-fade
		float GetInt(float frame) const;
	};

	class Group
	{
	public:
		std::string name;
		u32 id;
		std::vector<Channel> channels;

		Group(void) : id(0) {}
		const Channel *Find(u32 param) const;
	};

	u32 type;
	float numFrames;
	float speed;		// frames per second
	bool cyclic;
	std::vector<Group> groups;

	CLASSNAME(Animation);
	Animation(void) : type(0), numFrames(0.0f), speed(30.0f), cyclic(false) {}

	const Group *FindGroup(const char *name) const;
	// SHR: tAnimation::MakeValidFrame with the whole animation as the range
	float MakeValidFrame(float frame) const;
};

class AnimationLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(AnimationLoader)
	AnimationLoader(void) : SimpleChunkHandler(Animation::ANIMATION) {}
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory);
};


// retail: the frame controller family (SHR tFrameController / tSimpleFrameController).
// Retail's controllers advance themselves from the clock; the sky's are all driven from
// outside, by renderer::SkyRenderable::Update, which is why its loader clears the "do
// advance yourself" flag ([fc+0x0d] = 0) on every one of them.
class FrameController : public Entity
{
public:
	Animation *animation;
	float frameOffset;

	CLASSNAME(FrameController);
	FrameController(void) : animation(nil), frameOffset(0.0f) {}
	virtual ~FrameController(void);

	void SetAnimation(Animation *a);
	float GetNumFrames(void) const { return animation ? animation->numFrames : 0.0f; }
	virtual void SetFrame(float frame) = 0;
};

// The header of a 0x00121201 chunk, which is the same for every controller type: the
// parent loader reads it and then builds the controller its own type needs.       [V]
struct FrameControllerInfo
{
	char name[256];
	char target[256];
	char animName[256];
	u32 type;		// the animation type four-CC ('LITE', 'BQG\0', ...)
	float frameOffset;
};
void ReadFrameControllerInfo(ChunkFile *f, FrameControllerInfo *info);

// retail: pure3d::PoseAnimationController (SHR tPoseAnimationController). A 'PTRN'
// animation whose groups are named after the skeleton's joints; it writes the joint
// rotation and translation of a CompositeDrawable's Pose. The sky composite has one of
// these ("PTRN_sky"), and the joint it turns --- "sun_grp" --- is what carries the sun,
// its flares and the moon across the sky.
class PoseAnimationController : public FrameController
{
	Pose *pose;
	Skeleton *skeleton;
	// animation group index per joint, -1 for "not animated" (SHR looks the group up
	// by the joint's UID every frame; ours is a load-time bind)
	std::vector<i32> jointGroup;
public:
	CLASSNAME(PoseAnimationController);
	PoseAnimationController(void) : pose(nil), skeleton(nil) {}

	// binds to the pose of the composite and resolves the group <-> joint mapping
	void SetPose(Pose *p, Skeleton *s);
	Pose *GetPose(void) { return pose; }

	// SHR: tPoseAnimationController::UpdateNoBlending
	virtual void SetFrame(float frame);
};

}
