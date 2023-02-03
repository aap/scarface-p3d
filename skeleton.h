#pragma once

#include "gmath.h"
#include "entity.h"
#include "loadmanager.h"

namespace pure3d
{

using namespace math;
using namespace core;
using namespace content;

class Skeleton;

class Pose : public NonCopyable
{
	Skeleton *skeleton;
	u32 updateCount;
	Matrix *matrix;
	Quaternion *rotation;
	Vector *position;
	i32 numJoints;
	bool dirty1 : 1;
	bool dirty2 : 2;
	u8 *allocPtr;
public:
	CLASSNAME(Pose);
	Pose(Skeleton *skeleton);
	~Pose(void);

	Matrix *GetMatrix(i32 i) { return &matrix[i]; }

	virtual Vector *GetPosition(i32 i);
	virtual void SetPosition(i32 i, Vector *pos);
	virtual void Update(Matrix *root);
	virtual void Evaluate(Matrix *root);
	virtual void EvaluateJointMatrix(i32 i, Matrix *root, Matrix *dst);
	virtual void SetDirty1(bool set);
	virtual void SetClean(void);
	virtual void UpdateFromSkeleton(void);
};

class Skeleton : public Entity
{
	i32 numJoints;
	Quaternion *rotation;
	Matrix *localMatrix;
	Matrix *worldMatrix;
	Matrix *invWorldMatrix;
	u32 *uid;	// TODO? would be nice to have the names available too
	u16 *parentID;
	// unknown
	u8 *allocPtr;
	// TODO: some arrays
public:
	enum {
		SKELETON = 0x23000,
		SKELETON_JOINT = 0x23001
	};

	CLASSNAME(Skeleton);
	Skeleton(i32 numJoints, i32 numXXX, i32 numLimbs);
	~Skeleton(void);

	i32 GetNumJoints(void) { return numJoints; }
	Quaternion *GetRotation(i32 i) { return &rotation[i]; }
	Vector *GetPosition(i32 i) { return localMatrix[i].GetPosition(); }
	Matrix *GetLocalMatrix(i32 i) { return &localMatrix[i]; }
	Matrix *GetWorldMatrix(i32 i) { return &worldMatrix[i]; }
	void SetJointUID(i32 i, u32 UID) { uid[i] = UID; }
	void SetParent(i32 i, i32 id) { parentID[i] = id; }
	i32 GetParent(i32 i) { return parentID[i]; }

	void Rebuild(void);
};

class SkeletonLoader : public SimpleChunkHandler
{
public:
	CLASSNAME(SkeletonLoader)

	SkeletonLoader(void) : SimpleChunkHandler(Skeleton::SKELETON) {}

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory);
};

}
