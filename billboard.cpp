#include <stdio.h>
#include <string.h>

#include "billboard.h"
#include "shader.h"
#include "displaylist.h"

namespace pure3d
{

// the rotation part only, for directions
static Vector
Rotate3(const Vector &v, const Matrix &m)
{
	return Vector(v.x*m.e[0] + v.y*m.e[4] + v.z*m.e[8],
	              v.x*m.e[1] + v.y*m.e[5] + v.z*m.e[9],
	              v.x*m.e[2] + v.y*m.e[6] + v.z*m.e[10]);
}


BillboardQuad::BillboardQuad(void)
 : colour(0xFFFFFFFF),
   width(0.5f),
   height(0.5f),
   distance(0.0f),
   visible(true),
   billboardMode(MODE_ALL_AXIS),
   flipMode(0),
   cutOffMode(0),
   intensity(1.0f)
{
	transform.Identity();
	uv[0].x = 0.0f; uv[0].y = 0.0f;
	uv[1].x = 1.0f; uv[1].y = 0.0f;
	uv[2].x = 1.0f; uv[2].y = 1.0f;
	uv[3].x = 0.0f; uv[3].y = 1.0f;
	uvOffset.x = 0.0f; uvOffset.y = 0.0f;
}


// retail: pure3d::BillboardQuadGroup::BillboardQuadGroup 0x00698830
BillboardQuadGroup::BillboardQuadGroup(void)
 : shader(nil),
   primBuffer(nil),
   intensityBias(1.0f),
   zTest(true),
   zWrite(false),
   occlusion(false),
   occlusionIndex(0)
{
	transform.Identity();
}

BillboardQuadGroup::~BillboardQuadGroup(void)
{
	for(u32 i = 0; i < quads.Size(); i++)
		Release(quads[i]);
	Release(shader);
	Release(primBuffer);
}

void
BillboardQuadGroup::SetShader(Shader *sh)
{
	Assign(shader, sh);
}

bool
BillboardQuadGroup::IsALUM(void)
{
	return shader && shader->GetALUM();
}

// retail: BillboardQuadGroup vslot 13 (0x006971d0). The bounds are only used by the
// containing composite; a sky billboard sits 500..1500 m out and its group is never
// distance tested, so a box around the quad centres is enough.
void
BillboardQuadGroup::UpdateBounds(void)
{
	box.Init();
	for(u32 i = 0; i < quads.Size(); i++) {
		BillboardQuad *q = quads[i];
		Vector p = *q->transform.GetPosition();
		float r = q->width > q->height ? q->width : q->height;
		box.ContainPoint(p - Vector(r, r, r));
		box.ContainPoint(p + Vector(r, r, r));
	}
	if(quads.Size() == 0)
		box = Box3D(Vector(0.0f, 0.0f, 0.0f), Vector(0.0f, 0.0f, 0.0f));
	sphere = box.GetSphere();
}

// retail: pure3d::BillboardQuadGroup::Display 0x00698940 (DrawablePrimitive vslot 8).
// It takes the world matrix off the matrix stack and the camera-to-world and
// world-to-camera matrices off the view, builds every visible quad into one
// PDDI_PRIM_TRIANGLES stream of numVisible*6 vertices and draws it with the group's
// shader, with the group's zTest/zWrite around it. The per-quad vertex construction is
// tBillboardQuad::Display in the SHR Pure3D source (billboardobject.cpp); the only
// difference here is that the quads are left in world space instead of being
// transformed into camera space, because our pddi keeps world and view apart.
void
BillboardQuadGroup::Display(void)
{
	u32 n = quads.Size();
	if(n == 0 || shader == nil)
		return;

	// local -> world (includes the pose, the sky renderable's 2x scale, the display
	// list node matrix and the viewer's x flip)
	Matrix world = context->GetWorldMatrix();
	// world -> camera is what pddi has; a billboard needs the other direction
	Matrix camera = context->GetViewMatrix();
	camera.InvertOrtho();
	Vector camPos = *camera.GetPosition();
	Vector camRight = *camera.GetX();
	Vector camUp = *camera.GetY();

	if(primBuffer == nil) {
		primBuffer = device->NewPrimBuffer(PDDI_PRIM_TRIANGLES,
			PDDI_V_POSITION|PDDI_V_COLOUR|PDDI_V_UVCOUNT1, n*4, n*6);
		u16 *idx = new u16[n*6];
		for(u32 i = 0; i < n; i++) {
			idx[i*6+0] = i*4 + 0; idx[i*6+1] = i*4 + 1; idx[i*6+2] = i*4 + 2;
			idx[i*6+3] = i*4 + 0; idx[i*6+4] = i*4 + 2; idx[i*6+5] = i*4 + 3;
		}
		primBuffer->SetIndices(idx);
		delete[] idx;
	}

	pddiPrimBufferStream *stream = primBuffer->Lock();
	for(u32 i = 0; i < n; i++) {
		BillboardQuad *q = quads[i];
		Vector v[4];
		// retail drops a quad that is invisible or fully transparent from the stream;
		// the buffer has a fixed index list here, so collapse it to a point instead
		bool on = q->visible && q->colour.A() != 0 &&
			(q->colour.R() || q->colour.G() || q->colour.B());

		if(!on) {
			v[0] = v[1] = v[2] = v[3] = Vector(0.0f, 0.0f, 0.0f);
		} else if(q->billboardMode == BillboardQuad::MODE_ALL_AXIS) {
			// fully camera facing: the quad centre goes to world space and the
			// corners are laid out along the camera's own right/up axes
			Matrix objectToWorld = Multiply(q->transform, world);
			Vector pos = *objectToWorld.GetPosition();
			if(q->distance != 0.0f) {
				Vector d = camPos - pos;
				float len = Norm(d);
				if(len > 0.0f)
					pos = pos + d*(q->distance/len);
			}
			// retail scales a non-perspective billboard by the camera-space z so
			// that it keeps its size on screen; every sky quad is perspective
			float x = q->width, y = q->height;
			Vector right = camRight*x;
			Vector up = camUp*y;
			v[0] = pos - right - up;
			v[1] = pos + right - up;
			v[2] = pos + right + up;
			v[3] = pos - right + up;
		} else {
			// everything else is a flat quad in its own transform; the axis
			// aligned modes additionally spin it towards the camera, which
			// nothing in the sky uses (re/notes/sky.md)
			Matrix objectToWorld = Multiply(q->transform, world);
			v[0] = Multiply(Vector(-q->width, -q->height, 0.0f), objectToWorld);
			v[1] = Multiply(Vector( q->width, -q->height, 0.0f), objectToWorld);
			v[2] = Multiply(Vector( q->width,  q->height, 0.0f), objectToWorld);
			v[3] = Multiply(Vector(-q->width,  q->height, 0.0f), objectToWorld);
			if(q->distance != 0.0f) {
				Vector e = Rotate3(Vector(0.0f, 0.0f, -q->distance), objectToWorld);
				for(int j = 0; j < 4; j++)
					v[j] = v[j] + e;
			}
		}

		float in = q->intensity * intensityBias;
		pddiColour col = q->colour;
		if(in != 1.0f)
			col = pddiColour((u8)(col.R()*in), (u8)(col.G()*in),
			                 (u8)(col.B()*in), (u8)(col.A()*in));
		for(int j = 0; j < 4; j++) {
			stream->Colour(col);
			stream->TexCoord2(q->uv[j].x + q->uvOffset.x, q->uv[j].y + q->uvOffset.y);
			stream->Position(v[j].x, v[j].y, v[j].z);
		}
	}
	primBuffer->Unlock(stream);

	// retail saves the previous z state and puts it back afterwards; the quads are
	// already in world space, so the world matrix goes to the identity
	// (retail: p3d::stack->PushIdentity())
	bool oldZWrite = context->GetZWrite();
	bool oldZTest = context->GetZTest();
	if(oldZWrite != zWrite)
		context->SetZWrite(zWrite);
	if((!zTest || occlusion) && oldZTest)
		context->SetZTest(false);
	Matrix ident;
	ident.Identity();
	context->PushWorldMatrix();
	context->SetWorldMatrix(ident);
	context->DrawPrimBuffer(shader->GetShader(), primBuffer);
	context->PopWorldMatrix();
	if((!zTest || occlusion) && oldZTest)
		context->SetZTest(true);
	if(oldZWrite != zWrite)
		context->SetZWrite(oldZWrite);
}


// retail: the ctor inlined into BillboardObjectLoader::LoadObject at 0x006993f7
BillboardObject::BillboardObject(void)
 : DrawableContainer(1), intensityBias(1.0f)
{
}

// retail: pure3d::BillboardObject::Display 0x00697660 --- push the group's transform,
// hand the container's intensity bias to the group, then the normal container path.
// The transform ends up baked into the display list node's world matrix.
void
BillboardObject::Display(DisplayList *list, GameDrawableInfo *info)
{
	BillboardQuadGroup *group = (BillboardQuadGroup*)GetElement(0)->prim;
	if(group == nil) {
		DrawPrimitives(list, info);
		return;
	}
	context->PushWorldMatrix();
	context->MultWorldMatrix(group->transform);
	group->intensityBias = intensityBias;
	DrawPrimitives(list, info);
	group->intensityBias = 1.0f;
	context->PopWorldMatrix();
}


// ---------------------------------------------------------------- the loader

BillboardObjectLoader::BillboardObjectLoader(void)
 : SimpleChunkHandler(BILLBOARD_QUAD_GROUP)
{
}

// the fourcc -> enum tables of retail 0x006977ff (quad) and 0x006990d6 (cut off)
static u32
GetFourCC(ChunkFile *f)
{
	u32 cc = f->GetU32();
	return cc;
}
#define FCC(a,b,c,d) ((u32)(a) | (u32)(b)<<8 | (u32)(c)<<16 | (u32)(d)<<24)

// 0x00017007: a quaternion and a position, both on the group and on every quad
static void
ReadTransform(ChunkFile *f, Matrix *m)
{
	Quaternion q;
	f->GetI32();			// version
	q.x = f->GetFloat();
	q.y = f->GetFloat();
	q.z = f->GetFloat();
	q.w = f->GetFloat();
	Vector pos;
	f->GetData(&pos, 3, sizeof(float));
	q.SetMatrix(*m);
	m->SetPosition(pos);
}

// retail: the 0x17005 arm of billboard_loader, inlined here from 0x006976f0
BillboardQuad *
BillboardObjectLoader::LoadQuad(ChunkFile *f, LoadInventory *inventory)
{
	char name[256];

	f->GetI32();			// version
	f->GetString(name);
	bool isCutOff = f->GetU32() != 0;	// -> BillboardCutOffQuad in retail

	BillboardQuad *quad = new BillboardQuad;
	quad->SetName(name);

	quad->visible = f->GetU32() != 0;
	u32 mode = GetFourCC(f);
	switch(mode) {
	case FCC('N','O','A','X'): quad->billboardMode = BillboardQuad::MODE_NO_AXIS; break;
	case FCC('X','A','X',0):   quad->billboardMode = BillboardQuad::MODE_X_AXIS; break;
	case FCC('Y','A','X',0):   quad->billboardMode = BillboardQuad::MODE_Y_AXIS; break;
	case FCC('L','X','A','X'): quad->billboardMode = BillboardQuad::MODE_LOCAL_X_AXIS; break;
	case FCC('L','Y','A','X'): quad->billboardMode = BillboardQuad::MODE_LOCAL_Y_AXIS; break;
	default:                   quad->billboardMode = BillboardQuad::MODE_ALL_AXIS; break;
	}
	quad->colour = pddiColour(f->GetU32());
	quad->width = f->GetFloat() * 0.5f;	// retail: * g[0x7644ec] == 0.5
	quad->height = f->GetFloat() * 0.5f;
	quad->distance = f->GetFloat();

	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()) {
		case BILLBOARD_TRANSFORM:
			ReadTransform(f, &quad->transform);
			break;

		case BILLBOARD_UV_INFO: {
			f->GetI32();			// version
			bool flipU = f->GetU32() != 0;
			bool flipV = f->GetU32() != 0;
			// retail 0x00697a84: 3/5 when u is flipped, 4 when only v is
			if(flipU) quad->flipMode = flipV ? 5 : 3;
			else if(flipV) quad->flipMode = 4;
			for(int i = 0; i < 4; i++) {
				quad->uv[i].x = f->GetFloat();
				quad->uv[i].y = f->GetFloat();
			}
			quad->uvOffset.x = f->GetFloat();
			quad->uvOffset.y = f->GetFloat();
			break;
		}

		// 0x17008 (uv animation frames), 0x1700a/b/d (the cut-off cones of a
		// BillboardCutOffQuad): re/notes/sky.md, nothing reads them yet
		default:
			break;
		}
		f->EndChunk();
	}
	(void)isCutOff;
	return quad;
}

// retail: pure3d::BillboardObjectLoader::LoadObject 0x006993d0 + billboard_loader
// 0x00698ce0. The object registered in the inventory is the BillboardObject container,
// under the name of the chunk; the composite drawable looks it up by that name.
void
BillboardObjectLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	char name[256];
	char shaderName[256];

	f->GetI32();			// version
	f->GetString(name);
	f->GetString(shaderName);
	bool isCutOff = f->GetU32() != 0;	// -> BillboardCutOffQuadGroup in retail

	BillboardObject *object = new BillboardObject;
	BillboardQuadGroup *group = new BillboardQuadGroup;
	object->SetName(name);
	group->SetName(name);
	object->GetElement(0)->SetPrimitive(group);

	Shader *shader = inventory->Find<Shader>(shaderName);
	if(shader == nil) {
		fprintf(stderr, "warning : shader \"%s\" not found while loading billboard quad group from file %s\n",
			shaderName, f->GetName());
		shader = new Shader("error");
	}
	group->SetShader(shader);

	group->zTest = f->GetU32() != 0;
	group->zWrite = f->GetU32() != 0;
	group->occlusion = f->GetU32() != 0;
	u32 numQuads = f->GetU32();
	group->quads.Create(numQuads);
	for(u32 i = 0; i < numQuads; i++)
		group->quads[i] = nil;

	u32 count = 0;
	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()) {
		case BILLBOARD_QUAD:
			if(count < numQuads) {
				BillboardQuad *quad = LoadQuad(f, inventory);
				quad->AddRef();
				group->quads[count++] = quad;
			}
			break;

		case BILLBOARD_TRANSFORM:
			ReadTransform(f, &group->transform);
			break;

		// 0x1700a/b (cut-off cones), 0x122000 (the composite sort key), 0x121204
		// (the BillboardQuadGroupAnimationController that the time of day drives),
		// 0x10003/4 (bounds): re/notes/sky.md
		default:
			break;
		}
		f->EndChunk();
	}
	// a hole in the array would crash Display; retail keeps nil slots too but never
	// hits them because the writer always emits every quad
	for(u32 i = count; i < numQuads; i++)
		group->quads[i] = new BillboardQuad, group->quads[i]->AddRef();

	object->SetFlags();
	object->CalcBounds();
	(void)isCutOff;

	*pObject = object;
	*pUID = object->GetUID();
}

}
