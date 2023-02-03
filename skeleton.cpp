#include "skeleton.h"

#include <assert.h>

namespace pure3d
{

using namespace math;

Pose::Pose(Skeleton *skel)
 : skeleton(skel), updateCount(0), dirty1(true)
{
	skeleton->AddRef();

	numJoints = skel->GetNumJoints();
	allocPtr = new u8[numJoints*(sizeof(Matrix) + sizeof(Vector) + sizeof(Quaternion))];
	matrix = (Matrix*)allocPtr;
	position = (Vector*)&matrix[numJoints];
	rotation = (Quaternion*)&position[numJoints];
	assert((void*)&rotation[numJoints] == (void*)&allocPtr[numJoints*(sizeof(Matrix) + sizeof(Vector) + sizeof(Quaternion))]);
	Pose::UpdateFromSkeleton();
	Pose::Update(nil);
}

Pose::~Pose(void)
{
	Release(skeleton);
	if(allocPtr)
		delete[] allocPtr;
}

Vector*
Pose::GetPosition(i32 i)
{
	return &position[i];
}

void
Pose::SetPosition(i32 i, Vector *pos)
{
	position[i] = *pos;
	dirty2 = true;
}

void
Pose::Update(Matrix *root)
{
	if(dirty1)
		updateCount++;
	if(dirty2) {
		Evaluate(root);
		dirty2 = false;
	}
}

void
Pose::Evaluate(Matrix *root)
{
	Matrix mat;
	if(root) {
		mat.Identity();
		rotation[0].SetMatrix(mat);
		mat.SetPosition(position[0]);
		matrix[0] = Multiply(mat, *root);
	} else {
		if(!dirty1)
			return;
		mat.Identity();
		rotation[0].SetMatrix(mat);
		mat.SetPosition(position[0]);
		matrix[0] = mat;
	}

	mat.Identity();
	for(int i = 1; i < numJoints; i++) {
		rotation[i].SetMatrix(mat);
		mat.SetPosition(position[i]);
		assert(skeleton->GetParent(i) < numJoints);
		matrix[i] = Multiply(mat, matrix[skeleton->GetParent(i)]);
	}

	dirty1 = false;
}

void
Pose::EvaluateJointMatrix(i32 i, Matrix *root, Matrix *dst)
{
	Update(root);
	*dst = matrix[i];
}

void
Pose::SetDirty1(bool set)
{
	if(set) dirty1 = true;
}

void
Pose::SetClean(void)
{
	dirty1 = false;
}

void
Pose::UpdateFromSkeleton(void)
{
	for(int i = 0; i < numJoints; i++) {
		rotation[i] = *skeleton->GetRotation(i);
		position[i] = *skeleton->GetPosition(i);
		matrix[i] = *skeleton->GetWorldMatrix(i);
	}
}




Skeleton::Skeleton(i32 numJoints, i32 numXXX, i32 numLimbs)
 : numJoints(numJoints)
{
	allocPtr = new u8[numJoints*(sizeof(Matrix)*3 + sizeof(Quaternion) + sizeof(u32) + sizeof(u16))];
	localMatrix = (Matrix*)allocPtr;
	worldMatrix = (Matrix*)&localMatrix[numJoints];
	invWorldMatrix = (Matrix*)&worldMatrix[numJoints];
	rotation = (Quaternion*)&invWorldMatrix[numJoints];
	uid = (u32*)&rotation[numJoints];
	parentID = (u16*)&uid[numJoints];
}

Skeleton::~Skeleton(void)
{
	delete[] allocPtr;
}

void
Skeleton::Rebuild(void)
{
	rotation[0].FromMatrix(localMatrix[0]);
	worldMatrix[0] = localMatrix[0];
	invWorldMatrix[0] = localMatrix[0];
	invWorldMatrix[0].InvertOrtho();
	for(int i = 1; i < numJoints; i++) {
		rotation[i].FromMatrix(localMatrix[i]);
		worldMatrix[i] = Multiply(localMatrix[i], worldMatrix[parentID[i]]);
		invWorldMatrix[i] = worldMatrix[i];
		invWorldMatrix[i].InvertOrtho();
	}
}


void
SkeletonLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256];

	f->GetString(name);
//	printf("	Skeleton %s\n", name);

	i32 a = f->GetI32();
	i32 numJoints = f->GetI32();
	i32 numXXX = f->GetI32();
	i32 numLimbs = f->GetI32();

	if(numJoints == 0)
		return;

	// can't handle those yet
	assert(numXXX == 0);
	assert(numLimbs == 0);
	Skeleton *skeleton = new Skeleton(numJoints, numXXX, numLimbs);
	skeleton->SetName(name);

	int count = 0;

	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()){
		case Skeleton::SKELETON_JOINT: {
			char jointName[256];
			f->GetString(jointName);
			u32 parentID = f->GetU32();
			skeleton->SetJointUID(count, MakeKey(jointName));
			skeleton->SetParent(count, parentID);
			f->GetData(skeleton->GetLocalMatrix(count), 16, sizeof(float));
//			printf("		joint %d %s\n", parentID, jointName);
			count++;
			break;
		}
		}
		f->EndChunk();
	}

	skeleton->Rebuild();

	*pObject = skeleton;
	*pUID = skeleton->GetUID();
}

}
