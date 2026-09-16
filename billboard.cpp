#include <stdio.h>
#include <string.h>
#include <math.h>

#include "billboard.h"
#include "shader.h"
#include "displaylist.h"
#include "geometry.h"

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
   isCutOff(false),
   sourceMode(CUTOFF_NONE),
   edgeMode(CUTOFF_NONE),
   falloffType(0),
   intensity(1.0f)
{
	for(int i = 0; i < 4; i++) {
		sourceRange[i] = 1.0f;
		edgeRange[i] = 1.0f;
		cutOffScale[i] = 1.0f;
	}
	falloff[0] = falloff[1] = 0.0f;
	transform.Identity();
	uv[0].x = 0.0f; uv[0].y = 0.0f;
	uv[1].x = 1.0f; uv[1].y = 0.0f;
	uv[2].x = 1.0f; uv[2].y = 1.0f;
	uv[3].x = 0.0f; uv[3].y = 1.0f;
	uvOffset.x = 0.0f; uvOffset.y = 0.0f;
}


// One cone: `range` is (cos inner, cos outer). Retail's loader already stored the cosines
// of the two angles (the `fcos` right after every File::GetData in the 0x1700a/b arms of
// the quad loader, 0x00697b9e ff), so the test is a plain compare. 0x00695a90 does:
//	if (cos >  cosInner) 1
//	if (cos <= cosOuter) 0
//	else 1 - (cos - cosInner)/(cosOuter - cosInner)                          [V]
static float
CutOffCone(float cosAngle, const float *range)
{
	if(cosAngle > range[0])
		return 1.0f;
	if(cosAngle <= range[1])
		return 0.0f;
	float d = range[1] - range[0];
	return d != 0.0f ? 1.0f - (cosAngle - range[0])/d : 1.0f;
}

// retail: 0x00695a90 --- the pair of cones of one 0x1700a/0x1700b chunk, evaluated in the
// frame of `m`. It projects `dir` onto the matrix's z axis and onto its y (mode bit 0,
// "VERT") or x (bit 1, "HRZT") axis, adds the two projections and takes the cosine
// between that and `dir`. The sum of the two projections IS `dir` projected into the
// plane the two axes span, so the cosine is the cosine of the angle between `dir` and
// that plane --- which makes the whole thing independent of the SIGN of the axes (this
// matters here: the viewer's camera-to-world z points backwards where D3D's points
// forwards, and the result is the same either way).                          [V]
static void
CutOffCones(float *outVert, float *outHorz, u32 mode, const float *range,
            const Matrix &m, const Vector &dir)
{
	*outVert = *outHorz = 1.0f;
	if(mode == BillboardQuad::CUTOFF_NONE)
		return;
	Vector zax = Rotate3(Vector(0.0f, 0.0f, 1.0f), m);
	float zlen = NormSq(zax);
	if(zlen < 1.0e-10f)
		return;
	Vector pz = zax*(Dot(zax, dir)/zlen);
	static const Vector axes[2] = { Vector(0.0f, 1.0f, 0.0f), Vector(1.0f, 0.0f, 0.0f) };
	float *out[2] = { outVert, outHorz };
	for(int i = 0; i < 2; i++) {
		if((mode & (1<<i)) == 0)
			continue;
		Vector ax = Rotate3(axes[i], m);
		float alen = NormSq(ax);
		if(alen < 1.0e-10f)
			continue;
		Vector r = pz + ax*(Dot(ax, dir)/alen);
		float len = Norm(r);
		if(len > 0.0f)
			*out[i] = CutOffCone(Dot(r/len, dir), &range[i*2]);
	}
}

// retail: pure3d::BillboardCutOffQuad vslot 8, 0x00696f80. The direction is the object
// position minus the camera position, normalised; the "source" cone is evaluated in the
// quad's own frame (is the quad turned towards the camera?) and the "edge" cone in the
// camera's (is the quad near the middle of the screen?), exactly SHR's
// objectIntensity * cameraIntensity. The final intensity is the product of all four
// factors. The 0x1700d pair of (hi, lo) ranges is a SIZE scale, not an intensity
// (the sun flares carry 1.3 and 2.88 there); we do not apply it --- see re/notes/sky.md.
void
BillboardQuad::Calculate(const Matrix &objectToWorld, const Matrix &cameraToWorld)
{
	intensity = 1.0f;
	if(sourceMode == CUTOFF_NONE && edgeMode == CUTOFF_NONE)
		return;
	Matrix obj = objectToWorld, cam = cameraToWorld;
	Vector dir = *obj.GetPosition() - *cam.GetPosition();
	float len = Norm(dir);
	if(len <= 0.0f)
		return;
	dir = dir/len;
	float sv, sh, ev, eh;
	CutOffCones(&sv, &sh, sourceMode, sourceRange, obj, dir);
	CutOffCones(&ev, &eh, edgeMode, edgeRange, cam, dir);
	intensity = sv*sh*ev*eh;
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

// SHR: tBillboardQuadGroup::FindQuadByUID
BillboardQuad *
BillboardQuadGroup::FindQuad(const char *name)
{
	u32 uid = GetHash(name);
	for(u32 i = 0; i < quads.Size(); i++)
		if(quads[i] && quads[i]->GetUID() == uid)
			return quads[i];
	return nil;
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

		// retail: the cut-off quads get their intensity recomputed every frame
		// (BillboardCutOffQuadGroup vslot 17, 0x00697430); a plain quad's Calculate
		// is a nullsub and its intensity stays 1
		if(on && (q->sourceMode || q->edgeMode))
			q->Calculate(Multiply(q->transform, world), camera);

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

BillboardObject::~BillboardObject(void)
{
	for(u32 i = 0; i < frameControllers.size(); i++)
		Release(frameControllers[i]);
}


BillboardQuadGroupAnimationController::~BillboardQuadGroupAnimationController(void)
{
	Release(group);
}

void
BillboardQuadGroupAnimationController::SetQuadGroup(BillboardQuadGroup *g)
{
	Assign(group, g);
}

// SHR: tBillboardQuadGroupAnimationController::Update --- every animation group is one
// quad, found by name; the channels that are there are written into it and the ones that
// are not leave the quad's loaded value alone.
void
BillboardQuadGroupAnimationController::SetFrame(float frame)
{
	if(group == nil || animation == nil)
		return;
	frame = animation->MakeValidFrame(frame + frameOffset);
	for(u32 i = 0; i < animation->groups.size(); i++) {
		const Animation::Group *g = &animation->groups[i];
		BillboardQuad *quad = group->FindQuad(g->name.c_str());
		if(quad == nil)
			continue;
		const Animation::Channel *c;
		if((c = g->Find(Animation::CHANNEL_VISIBILITY)) != nil)
			quad->visible = c->GetBool(frame);
		if((c = g->Find(Animation::CHANNEL_TRANSLATION)) != nil)
			quad->transform.SetPosition(c->GetVector(frame));
		if((c = g->Find(Animation::CHANNEL_ROTATION)) != nil) {
			// the rotation part only; the position was just written
			Vector pos = *quad->transform.GetPosition();
			Quaternion q = c->GetQuaternion(frame);
			q.SetMatrix(quad->transform);
			quad->transform.SetPosition(pos);
		}
		// the file stores the full width; retail halves it in the loader and so must
		// the animation (g[0x7644ec] == 0.5)
		if((c = g->Find(Animation::CHANNEL_WIDTH)) != nil)
			quad->width = c->GetFloat(frame)*0.5f;
		if((c = g->Find(Animation::CHANNEL_HEIGHT)) != nil)
			quad->height = c->GetFloat(frame)*0.5f;
		if((c = g->Find(Animation::CHANNEL_DISTANCE)) != nil)
			quad->distance = c->GetFloat(frame);
		if((c = g->Find(Animation::CHANNEL_COLOUR)) != nil)
			quad->colour = c->GetColour(frame);
		if((c = g->Find(Animation::CHANNEL_UVOFFSET)) != nil)
			quad->uvOffset = c->GetVector2(frame);
		// SRNG/ERNG animate the cut-off cones; nothing in z04 has them, and retail
		// hands them to SetSourceRange/SetEdgeRange as plain angles
	}
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

// retail: 0x00695a50, and inline at 0x006990d6 for the group's own cut-off chunks
static u32
GetCutOffMode(ChunkFile *f)
{
	switch(f->GetU32()) {
	case FCC('B','O','T','H'): return BillboardQuad::CUTOFF_BOTH;
	case FCC('V','E','R','T'): return BillboardQuad::CUTOFF_VERT;
	case FCC('H','R','Z','T'): return BillboardQuad::CUTOFF_HRZT;
	default: return BillboardQuad::CUTOFF_NONE;
	}
}

// 0x0001700a / 0x0001700b: { u32 version; u32 mode4cc; float angle[4]; }. The loader
// stores the COSINE of every angle (the `fcos` after each read) and the four are two
// (inner, outer) pairs, the first for the "VERT" cone and the second for "HRZT". [V]
static void
ReadCutOffCone(ChunkFile *f, u32 *mode, float *range)
{
	f->GetU32();			// version
	*mode = GetCutOffMode(f);
	for(int i = 0; i < 4; i++)
		range[i] = cosf(f->GetFloat());
}

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
	quad->isCutOff = isCutOff;

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

		// the two cut-off cones of a BillboardCutOffQuad. Retail ignores both unless
		// the quad's cutOff word was set, which is also when it allocates the bigger
		// object; we always have the fields.
		case BILLBOARD_CUTOFF_SOURCE:
			if(isCutOff)
				ReadCutOffCone(f, &quad->sourceMode, quad->sourceRange);
			break;
		case BILLBOARD_CUTOFF_EDGE:
			if(isCutOff)
				ReadCutOffCone(f, &quad->edgeMode, quad->edgeRange);
			break;

		// 0x0001700c: { u32 version; u32 "LINE"/other; float a, b; } (retail +0xcc,
		// +0x100, +0x104); nothing in the sky has one
		case BILLBOARD_CUTOFF_FALLOFF:
			if(isCutOff) {
				f->GetI32();		// version
				quad->falloffType = GetFourCC(f) == FCC('L','I','N','E') ? 1 : 0;
				quad->falloff[0] = f->GetFloat();
				quad->falloff[1] = f->GetFloat();
			}
			break;

		// 0x0001700d: { u32 version; float scale[4]; } (retail +0xf0..+0xfc), two
		// (hi, lo) pairs the cone factors are lerped into. Every sky quad has the two
		// ends equal, so it is a plain size multiplier; see re/notes/sky.md.
		case BILLBOARD_CUTOFF_RANGE:
			f->GetI32();			// version
			for(int i = 0; i < 4; i++)
				quad->cutOffScale[i] = f->GetFloat();
			break;

		// 0x17008 (the uv atlas animation): re/notes/sky.md, nothing reads it yet
		default:
			break;
		}
		f->EndChunk();
	}
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

		// the group's own cut-off cones (retail +0x94/+0xa0.. and +0x9c/+0xe0..);
		// nothing in z04 actually has them, only the quads do
		case BILLBOARD_CUTOFF_SOURCE:
		case BILLBOARD_CUTOFF_EDGE:
			break;

		// 0x00121204: a wrapper { u32 version; u32 count; } around `count`
		// 0x00121201 frame controllers. The 'BQG' animation is what moves, colours
		// and hides the quads over the 24 h day (re/notes/sky.md).
		case Animation::CONTROLLER_LIST: {
			f->GetU32();		// version
			u32 n = f->GetU32();
			for(u32 i = 0; i < n && f->ChunksRemaining(); i++) {
				if(f->BeginChunk() != Animation::FRAME_CONTROLLER) {
					f->EndChunk();
					continue;
				}
				FrameControllerInfo info;
				ReadFrameControllerInfo(f, &info);
				Animation *anim = info.type == Animation::TYPE_BQG ?
					inventory->Find<Animation>(info.animName) : nil;
				if(anim) {
					BillboardQuadGroupAnimationController *ctrl =
						new BillboardQuadGroupAnimationController;
					ctrl->SetName(info.name);
					ctrl->frameOffset = info.frameOffset;
					ctrl->SetAnimation(anim);
					ctrl->SetQuadGroup(group);
					ctrl->AddRef();
					object->frameControllers.push_back(ctrl);
				}
				f->EndChunk();
			}
			break;
		}

		// the container sort key, same as on a mesh (geometry.cpp). The sun is 0.5,
		// its two flare stars 0.3 and 0.7, the lens quads 0.1 / 0.4 / 0.2, which is
		// the order they are painted on top of each other in (list 76 is not sorted,
		// but the key is what retail reads here).
		case Geometry::SORTKEY: {
			f->GetI32();			// version
			float key = f->GetFloat();
			object->sortKey = key < 0.0f ? 0.0f : key > 1.0f ? 1.0f : key;
			break;
		}

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
