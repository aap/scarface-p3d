#pragma once

#include "renderable.h"
#include "../geometry.h"
#include "../loadmanager.h"

#include <string>
#include <vector>

namespace renderer
{

using namespace content;

// One placed eco prop, decoded from a 0x09900194 record.
// Retail packs these into StatePropManager::SLocationData (24 bytes); we keep
// the decoded form plus the matrix and the world-space bound sphere.
struct InstanceLocation
{
	Vector position;
	Vector rotation;	// degrees, XYZ
	Vector scale;
	u8 tint;		// 0..15, 0 = none (retail never applies it on PC)
	u8 state;		// initial state prop state, 0 = none
	Matrix matrix;
	Sphere sphere;
	DisplayListPrimitive prim;	// InstanceShape
	DisplayListPrimitive lodPrim;	// LODShape
};

// An "instanceobject" script object: model name + placements.
// Retail: renderer::InstanceRenderable (typeMask 0x200) holding an
// InstanceContainer/InstancePrimitive that draws the <model>InstanceShape
// mesh through the pddi instancing extension. We just draw the mesh once
// per placement.
class InstanceRenderable : public Renderable
{
public:
	std::string modelName;
	std::string attractName;
	float cullMin, cullMax, fadeDist;
	std::vector<InstanceLocation> locations;	// not moved after SetShapes binds the prims
	pure3d::Geometry *shape;	// <model>InstanceShape
	pure3d::Geometry *lodShape;	// <model>LODShape (optional)

	CLASSNAME(InstanceRenderable)
	InstanceRenderable(void);
	~InstanceRenderable(void);

	void SetShapes(pure3d::Geometry *shape, pure3d::Geometry *lod);
	void AddLocation(const InstanceLocation &loc);
	void DecodeLocation(ChunkFile *f, InstanceLocation *loc);

	virtual void SetVisible(bool visible);
	virtual void Display(void);

	static float defaultCullMax;
	static float lodDist;
};

// Handles 0x09900190 (ScriptObjectDataLoader) and 0x09900191 (GameGroupDataLoader).
// Only "instanceobject" objects are turned into something; everything else is skipped.
class InstanceLoader : public SimpleChunkHandler
{
public:
	enum {
		SCRIPTOBJECT	= 0x9900190,
		GAMEGROUP	= 0x9900191,
		PROPERTY	= 0x9900192,
		LOCATION	= 0x9900194
	};
	CLASSNAME(InstanceLoader)

	InstanceLoader(u32 id) : SimpleChunkHandler(id) {}

	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
	InstanceRenderable *LoadScriptObject(content::ChunkFile *f, content::LoadInventory *inventory);
	void LoadGroup(content::ChunkFile *f, content::LoadInventory *inventory, int depth);
};

// Instance shapes live in whatever package happens to contain the model, which is
// often not the package that places it. The viewer registers every Geometry it
// sees here so placements can be resolved after everything is loaded.
void RegisterInstanceShape(pure3d::Geometry *geo);
bool ResolveInstanceShapes(InstanceRenderable *inst);

}
