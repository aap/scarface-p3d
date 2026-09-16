#pragma once

#include "renderable.h"
#include "../loadmanager.h"

#include <string>
#include <vector>

namespace renderer
{

using namespace content;
using math::Vector;

// art/levels/z04/streamgraph.p3d --- the retail StreamManager's polygon triggers.
// One chunk 0x08800101 per trigger: while the player is inside the polygon the listed
// packages have to be resident, each in the named stream slot (Global_S/D, Region_S/D,
// Shell, Detail, ZoneAnimation). See re/notes/streaming.md and re/streamgraph.py.
class StreamTrigger : public content::LoadObject
{
public:
	CLASSNAME(StreamTrigger)
	struct Load { std::string package; std::string slot; };

	std::string name;		// "loadTrigger_100"
	std::string tag;		// the subzone: "sbeachn_01_shell"
	float height;			// prism height above the polygon
	std::vector<Vector> points;	// closed polygon, y is the base
	std::vector<Load> loads;

	bool Contains(float x, float z) const;	// 2-D even-odd test
	void Bounds(float &x0, float &x1, float &z0, float &z1) const;
	void Packages(const char *slot, std::vector<std::string> &out) const;
	const char *Region(void) const;	// first Region_S ending in _region, without the suffix
};

// retail: StreamTriggerLoader, vtable 0x73be44, ctor 0x4b9440 (stores 0x8800101).
// The strings in this chunk are padded: the length byte counts the NUL padding up to
// a multiple of 4.
class StreamTriggerLoader : public content::SimpleChunkHandler
{
public:
	enum { STREAMTRIGGER = 0x8800101, STREAMPACKAGE = 0x8800100 };
	CLASSNAME(StreamTriggerLoader)
	StreamTriggerLoader(void);
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

}
