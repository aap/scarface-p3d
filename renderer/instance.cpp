#include "instance.h"
#include "display_list.h"
#include "../shader.h"
#include "../pddi.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <map>

namespace renderer
{

using namespace pure3d;

float InstanceRenderable::defaultCullMax = 300.0f;
float InstanceRenderable::lodDist = 120.0f;

static std::map<u32, Geometry*> instanceShapes;

void
RegisterInstanceShape(Geometry *geo)
{
	const char *name = geo->GetName();
	size_t len = strlen(name);
	// only keep the meshes the instancer can ask for
	if((len > 13 && strcmp(name+len-13, "InstanceShape") == 0) ||
	   (len > 8 && strcmp(name+len-8, "LODShape") == 0) ||
	   (len > 9 && strcmp(name+len-9, "LODShape2") == 0)) {
		u32 uid = geo->GetUID();
		if(instanceShapes.find(uid) == instanceShapes.end()) {
			geo->AddRef();
			instanceShapes[uid] = geo;
		}
	}
}

static Geometry*
FindShape(const std::string &model, const char *suffix)
{
	std::string name = model + suffix;
	// retail: MakeKey32(suffix, MakeKey32(model)) == GetHash(model+suffix)
	auto it = instanceShapes.find(GetHash(name.c_str()));
	return it == instanceShapes.end() ? nil : it->second;
}

bool
ResolveInstanceShapes(InstanceRenderable *inst)
{
	if(inst->shape || inst->modelName.empty())
		return inst->shape != nil;
	Geometry *shape = FindShape(inst->modelName, "InstanceShape");
	if(shape == nil)
		return false;
	inst->SetShapes(shape, FindShape(inst->modelName, "LODShape"));
	return true;
}


InstanceRenderable::InstanceRenderable(void)
 : Renderable(0),
   cullMin(0.0f), cullMax(0.0f), fadeDist(0.0f),
   shape(nil), lodShape(nil)
{
	typeMask = TYPE_INSTANCE;
	// retail: the ctor clears flags80 & ~3 --- no distance test and no fade; the cull
	// radius is installed afterwards by InstanceRenderable_SetCullDistance (0x463f80)
}

InstanceRenderable::~InstanceRenderable(void)
{
	SetShapes(nil, nil);
}

static void
SetInstanceLayers(Geometry *geo)
{
	// retail InstancePrimitive layers: 40 (list 72) for LOD/flagged, 41 (73) normal, 42 (74) special shaders
	// retail InstancePrimitive uses layers 40..42 (lists 72..74) and draws through the
	// pddi instancing extension; we have no instancing, so sort them like world geo.
	bool flag1 = false;
	for(i32 i = 0; i < geo->GetNumElements(); i++) {
		DrawablePrimitive *prim = geo->GetElement(i)->prim;
		if(prim == nil) {
			if(getenv("P3D_VERBOSE")) printf("instance shape %s: element %d has no primitive\n", geo->GetName(), i);
			continue;
		}
		if(prim->GetLayer() == 0 && prim->GetShader())
			SetPrimLayerByShader(prim, flag1);
		if(getenv("P3D_VERBOSE")) printf("instance shape %s: element %d layer %d shader %s\n", geo->GetName(), i, prim->GetLayer(), prim->GetShader() ? prim->GetShader()->GetName() : "-");
	}
}

void
InstanceRenderable::SetShapes(Geometry *newShape, Geometry *newLod)
{
	if(newShape) { newShape->AddRef(); newShape->CalcBounds(); SetInstanceLayers(newShape); }
	if(newLod) { newLod->AddRef(); newLod->CalcBounds(); SetInstanceLayers(newLod); }
	if(shape) shape->Release();
	if(lodShape) lodShape->Release();
	shape = newShape;
	lodShape = newLod;
	// Retail (re/notes/renderspine.md §5.4) hands both meshes to the instancing
	// extension: the InstanceShape (wind-swayed crown) and the LODShape (whole tree
	// incl. trunk) are both drawn from distance 0; past the near band only the
	// LODShape remains. So each placement gets two display-list primitives.
	for(u32 i = 0; i < locations.size(); i++) {
		InstanceLocation &loc = locations[i];
		loc.prim.SetParent(this);
		loc.prim.SetDrawable(shape, false);
		loc.prim.SetInstanceMatrix(&loc.matrix);
		loc.lodPrim.SetParent(this);
		loc.lodPrim.SetDrawable(lodShape, false);
		loc.lodPrim.SetInstanceMatrix(&loc.matrix);
		if(shape) {
			loc.sphere.centre = Multiply(shape->sphere.centre, loc.matrix);
			float s = loc.scale.x;
			if(loc.scale.y > s) s = loc.scale.y;
			if(loc.scale.z > s) s = loc.scale.z;
			loc.sphere.radius = shape->sphere.radius * s;
		}
	}
}

// retail: Matrix::SetRotation (0x660a90) followed by the row scaling and
// translation of InstancePrimitive::AddInstance (0x46f4b0). Note that X and Y
// are negated.
static void
BuildInstanceMatrix(InstanceLocation *loc)
{
	const float DEG2RAD = 0.017453292f;
	float a = -loc->rotation.x*DEG2RAD, b = -loc->rotation.y*DEG2RAD, c = loc->rotation.z*DEG2RAD;
	float c1 = cosf(a), s1 = sinf(a), c2 = cosf(b), s2 = sinf(b), c3 = cosf(c), s3 = sinf(c);
	Matrix &m = loc->matrix;
	m.Identity();
	m.e[0] = c3*c2;             m.e[1] = s3*c2;             m.e[2]  = -s2;
	m.e[4] = c3*s1*s2 - s3*c1;  m.e[5] = s2*s3*s1 + c3*c1;  m.e[6]  = c2*s1;
	m.e[8] = c3*c1*s2 + s3*s1;  m.e[9] = s3*c1*s2 - c3*s1;  m.e[10] = c2*c1;
	*m.GetX() = *m.GetX() * loc->scale.x;
	*m.GetY() = *m.GetY() * loc->scale.y;
	*m.GetZ() = *m.GetZ() * loc->scale.z;
	m.SetPosition(loc->position);
}

// 0x09900194: float x, y/100, z; u32 rotX, rotY, rotZ (deg); u32 s0, s1, s2; u32 uniform; u32 tint; u32 state
void
InstanceRenderable::DecodeLocation(ChunkFile *f, InstanceLocation *loc)
{
	loc->position.x = f->GetFloat();
	loc->position.y = f->GetFloat() * 0.01f;
	loc->position.z = f->GetFloat();
	loc->rotation.x = (float)f->GetU32();
	loc->rotation.y = (float)f->GetU32();
	loc->rotation.z = (float)f->GetU32();
	u32 s0 = f->GetU32() & 0x3f, s1 = f->GetU32() & 0x3f, s2 = f->GetU32() & 0x3f;
	u32 uniform = f->GetU32();
	loc->tint = f->GetU32() & 0xf;
	loc->state = f->GetU32();
	if(uniform) {
		float s = (s0 | s1<<6 | s2<<12) * 0.001f;
		loc->scale = Vector(s, s, s);
	} else
		loc->scale = Vector(s0*0.05f, s1*0.05f, s2*0.05f);
	BuildInstanceMatrix(loc);
	loc->sphere = Sphere(loc->position, 0.0f);
}

void
InstanceRenderable::AddLocation(const InstanceLocation &loc)
{
	// safe to copy while unbound (prim not in any list); SetShapes binds later
	locations.push_back(loc);
}

void
InstanceRenderable::SetVisible(bool visible)
{
	Renderable::SetVisible(visible);
	for(u32 i = 0; i < locations.size(); i++) {
		locations[i].prim.SetVisible(visible);
		locations[i].lodPrim.SetVisible(visible);
	}
}

void
InstanceRenderable::Display(void)
{
	if(shape == nil || !isVisible)
		return;
	static const char *only = getenv("P3D_ONLYMODEL");	// debugging: render only matching models
	if(only && modelName.find(only) == std::string::npos) {
		for(u32 i = 0; i < locations.size(); i++) { locations[i].prim.Display(false); locations[i].lodPrim.Display(false); }
		return;
	}
	// retail StatePropManager culls on 2D (XZ) distance against
	// (mVisibilityEnd*globalScale)^2 + 900; frustum culling is per matrix packet.
	float maxDist = cullMax > 0.0f ? cullMax : defaultCullMax;
	float maxSq = maxDist*maxDist + 900.0f;
	// retail: near band end = (farEnd - D)*0.6 + radius, cross-fading over max(0.3*range, 20)
	float nearEnd = lodShape ? maxDist*0.6f + shape->sphere.radius : maxDist;
	float nearSq = nearEnd*nearEnd + 900.0f;
	Vector camPosition;
	View_GetCullingCamera()->GetPosition(&camPosition);
	static int dbg = 0;
	const char *dm = getenv("P3D_DEBUGMODEL");
	if(dm && dbg < 3 && modelName == dm) {
		dbg++;
		printf("%s (%s): %zu locations, cam %.1f %.1f %.1f, shape %s sphere %.1f %.1f %.1f r %.1f\n", GetName(), modelName.c_str(), locations.size(),
			camPosition.x, camPosition.y, camPosition.z, shape->GetName(), shape->sphere.centre.x, shape->sphere.centre.y, shape->sphere.centre.z, shape->sphere.radius);
		for(u32 i = 0; i < locations.size() && i < 4; i++) {
			InstanceLocation &loc = locations[i];
			printf("  loc pos %.1f %.1f %.1f rot %.0f %.0f %.0f scale %.2f  m[12..14] %.1f %.1f %.1f  dist %.1f\n", loc.position.x, loc.position.y, loc.position.z,
				loc.rotation.x, loc.rotation.y, loc.rotation.z, loc.scale.x, loc.matrix.e[12], loc.matrix.e[13], loc.matrix.e[14],
				sqrtf((loc.position.x-camPosition.x)*(loc.position.x-camPosition.x)+(loc.position.z-camPosition.z)*(loc.position.z-camPosition.z)));
		}
	}
	// the placements carry their own matrix (DisplayListPrimitive::SetInstanceMatrix),
	// so the world matrix the nodes capture must be the renderable's own --- identity.
	context->PushWorldMatrix();
	context->SetWorldMatrix(matrix);
	for(u32 i = 0; i < locations.size(); i++) {
		InstanceLocation &loc = locations[i];
		float dx = loc.position.x - camPosition.x;
		float dz = loc.position.z - camPosition.z;
		float d2 = dx*dx + dz*dz;
		loc.prim.Display(d2 < nearSq);
		if(lodShape) loc.lodPrim.Display(d2 < maxSq);
	}
	context->PopWorldMatrix();
}


void
InstanceLoader::LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	if(GetChunkID() == GAMEGROUP) {
		LoadGroup(f, inventory, 0);
		return;
	}
	InstanceRenderable *inst = LoadScriptObject(f, inventory);
	if(inst) {
		*pObject = inst;
		*pUID = inst->GetUID();
	}
}

// 0x09900191: pstring script; pstring object; pstring class; u32 ?; children (nested groups and objects)
void
InstanceLoader::LoadGroup(content::ChunkFile *f, content::LoadInventory *inventory, int depth)
{
	char tmp[256];
	f->GetString(tmp);	// script
	f->GetString(tmp);	// group name
	f->GetString(tmp);	// class (gamegroup, spawntemplategroup, pathgroup, ...)
	f->GetU32();
	while(f->ChunksRemaining()) {
		f->BeginChunk();
		if(f->GetCurrentID() == GAMEGROUP && depth < 8)
			LoadGroup(f, inventory, depth+1);
		else if(f->GetCurrentID() == SCRIPTOBJECT) {
			InstanceRenderable *inst = LoadScriptObject(f, inventory);
			if(inst)	// new objects start at refcount 0; Add takes the reference
				inventory->Add(inst->GetUID(), inst);
		}
		f->EndChunk();
	}
}

// 0x09900190: pstring script; pstring object; pstring class; children 0x09900192 properties
InstanceRenderable*
InstanceLoader::LoadScriptObject(content::ChunkFile *f, content::LoadInventory *inventory)
{
	char script[256], name[256], cls[256];
	f->GetString(script);
	f->GetString(name);
	f->GetString(cls);
	if(strcmp(cls, "instanceobject") != 0)
		return nil;

	InstanceRenderable *inst = new InstanceRenderable;
	inst->SetName(name);

	char key[256], value[256];
	while(f->ChunksRemaining()) {
		f->BeginChunk();
		if(f->GetCurrentID() == PROPERTY) {
			f->GetString(key);
			u32 type = f->GetU32();
			f->GetString(value);
			if(type == 2) {
				// preprocessed location(s) as 0x09900194 children
				while(f->ChunksRemaining()) {
					f->BeginChunk();
					if(f->GetCurrentID() == LOCATION) {
						InstanceLocation loc;
						inst->DecodeLocation(f, &loc);
						inst->AddLocation(loc);
					}
					f->EndChunk();
				}
			} else if(strcmp(key, "modelname") == 0)
				inst->modelName = value;
			else if(strcmp(key, "attractname") == 0)
				inst->attractName = value;
			else if(strcmp(key, "minculldistance") == 0)
				inst->cullMin = atof(value);
			else if(strcmp(key, "maxculldistance") == 0)
				inst->cullMax = atof(value);
			else if(strcmp(key, "fadeoutdist") == 0)
				inst->fadeDist = atof(value);
			else if(strcmp(key, "instanceposition") == 0) {
				// script-string form: "x y z rx ry rz sx [sy sz] tint name"
				InstanceLocation loc;
				float v[8]; char s1[64] = "", s2[64] = "", s3[64] = "";
				int n = sscanf(value, "%f %f %f %f %f %f %f %f %63s %63s %63s", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], s1, s2, s3);
				if(n >= 8) {
					loc.position = Vector(v[0], v[1], v[2]);
					loc.rotation = Vector(v[3], v[4], v[5]);
					if(n >= 10) { loc.scale = Vector(v[6], v[7], atof(s1)); loc.tint = atoi(s2); }
					else { loc.scale = Vector(v[6], v[6], v[6]); loc.tint = (u8)v[7]; }
					loc.state = 0;
					BuildInstanceMatrix(&loc);
					loc.sphere = Sphere(loc.position, 0.0f);
					inst->AddLocation(loc);
				}
			}
		}
		f->EndChunk();
	}
	return inst;
}

}
