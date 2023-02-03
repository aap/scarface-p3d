#include "compositedrawable.h"
#include "geometry.h"
#include "skeleton.h"
#include "shader.h"

namespace pure3d
{

CompositeDrawable::ActivePrimitiveList::ActivePrimitiveList(int numPrims)
 : usageMap(numPrims), primMap(numPrims), primArray(numPrims)
{
}

void
CompositeDrawable::ActivePrimitiveList::UpdateMaps(void)
{
	numPrims = 0;
	int n = primArray.Size();

//	assert(n < 128);	// problem with i8
	for(int i = 0; i < n; i++) {
		if(primArray[i].GetDrawable()) {
			usageMap[i] = numPrims;
			primMap[numPrims++] = i;
		} else {
			usageMap[i] = -1;
			primMap[i] = -1;
		}
	}
}



CompositeDrawable::CompositeDrawable(void)
 : pose(nil), primList(nil)
{
}

CompositeDrawable::~CompositeDrawable(void)
{
	CompositeDrawable::ReleaseShaderCallback();
	Release(pose);
	Release(primList);
}

void
CompositeDrawable::Display(DisplayList *list, GameDrawableInfo *info)
{
	// TOOD: complex shit with animation

	assert(pose);

	u32 n = primList->GetNumUsedPrimitives();
	for(u32 i = 0; i < n; i++) {
		ActivePrimitive *prim = primList->GetUsedPrimitive(i);
		if(!prim->isVisible) continue;

		// TODO: more weird stuff

		Matrix *mat = pose->GetMatrix(prim->id);

		DrawableContainer *drawable = prim->GetDrawable();
		context->PushDebugName(drawable->GetName());

		context->PushWorldMatrix();
		context->MultWorldMatrix(*mat);
		prim->GetDrawable()->Display(list, info);
		context->PopWorldMatrix();

		context->PopDebugName();
	}
}

void
CompositeDrawable::CalcBounds(void)
{
	if(pose == nil)
		return;

	pose->Update(nil);
	box.Init();
	sphere = Sphere(Vector(0.0f, 0.0f, 0.0f), 0.0f);

	u32 n = primList->GetNumUsedPrimitives();
	for(u32 i = 0; i < n; i++) {
		ActivePrimitive *prim = primList->GetUsedPrimitive(i);
		if(!prim->isVisible) continue;

		prim->GetDrawable()->CalcBounds();

		Box3D b = prim->GetDrawable()->box;
		// TODO: don't transform based on type??
		b = b.Transform(*pose->GetMatrix(prim->id));
		box.ContainPoint(b.low);
		box.ContainPoint(b.high);
	}

	if(box.low.x > box.high.x ||
	   box.low.y > box.high.y ||
	   box.low.z > box.high.z)
		box = Box3D(Vector(0.0f, 0.0f, 0.0f), Vector(0.0f, 0.0f, 0.0f));

	sphere = box.GetSphere();
}

void
CompositeDrawable::SetShaderCallback(ShaderCallback *cb)
{
	DrawableHierarchy::SetShaderCallback(cb);
	u32 n = primList->GetNumPrimitives();
	for(u32 i = 0; i < n; i++) {
		ActivePrimitive *prim = primList->GetPrimitive(i);
if(prim->GetDrawable())	// TODO remove
		prim->GetDrawable()->SetShaderCallback(cb);
	}
}

void
CompositeDrawable::ReleaseShaderCallback(void)
{
	DrawableHierarchy::ReleaseShaderCallback();
	u32 n = primList->GetNumPrimitives();
	for(u32 i = 0; i < n; i++) {
		ActivePrimitive *prim = primList->GetPrimitive(i);
if(prim->GetDrawable())	// TODO remove
		prim->GetDrawable()->ReleaseShaderCallback();
	}
}

void
CompositeDrawable::SetFading(bool enable)
{
	DrawableHierarchy::ReleaseShaderCallback();
	u32 n = primList->GetNumPrimitives();
	for(u32 i = 0; i < n; i++) {
		ActivePrimitive *prim = primList->GetPrimitive(i);
if(prim->GetDrawable())	// TODO remove
		prim->GetDrawable()->SetFading(enable);
	}
}

void
CompositeDrawable::SetFadeAmount(float fade)
{
	DrawableHierarchy::ReleaseShaderCallback();
	u32 n = primList->GetNumPrimitives();
	for(u32 i = 0; i < n; i++) {
		ActivePrimitive *prim = primList->GetPrimitive(i);
if(prim->GetDrawable())	// TODO remove
		prim->GetDrawable()->SetFadeAmount(fade);
	}
}




CompositeDrawableLoader::CompositeDrawableLoader(void)
 : SimpleChunkHandler(CompositeDrawable::COMPOSITE_DRAWABLE)
{
}

void
CompositeDrawableLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256];
	char skel[256];

	i32 version = f->GetI32();
	f->GetString(name);
	f->GetString(skel);

	i32 numPrims = f->GetI32();
//	printf("	Composite: %d %s %s\n", numPrims, name, skel);

	CompositeDrawable *composite = new CompositeDrawable;
	composite->SetName(name);

	Skeleton *skeleton = inventory->Find<Skeleton>(skel);
	if(skeleton)
		composite->SetPose(new Pose(skeleton));

	CompositeDrawable::ActivePrimitiveList *prims = new CompositeDrawable::ActivePrimitiveList(numPrims);
	composite->SetPrimitiveList(prims);

	int count = 0;

	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()){
		case CompositeDrawable::COMPOSITE_DRAWABLE_PRIMITIVE: {
			char childName[256];
			u32 unk = f->GetU32();	// unused
			bool makeCopy = f->GetU32() != 0;
			f->GetString(childName);
			u32 type = f->GetU32();
			DrawableContainer *drawable = nil;
			if(type & 1)
				drawable = inventory->Find<Geometry>(childName);
			// TODO: other types
// need assert later
//			assert(drawable);
if(drawable) {
			// TODO: makeCopy
			drawable->SetFlags();
			if(drawable->isLit)
				composite->isLit = true;

			prims->GetPrimitive(count)->SetDrawable(drawable);
			// TODO: pose
			drawable->SetParent(composite);
}
			i32 id = f->GetI32();
			prims->GetPrimitive(count)->id = id;
//			printf("		%X %s %d\n", type, childName, id);
			count++;
			break;
		}

		// TODO: 0x121201	frame controller
		}
		f->EndChunk();
	}

	prims->UpdateMaps();
	composite->CalcBounds();
	// TODO: unknown function

	*pObject = composite;
	*pUID = composite->GetUID();
}

}
