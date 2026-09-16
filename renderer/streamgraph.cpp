#include "streamgraph.h"

#include <string.h>

namespace renderer
{

bool
StreamTrigger::Contains(float x, float z) const
{
	bool inside = false;
	size_t n = points.size();
	for(size_t i = 0, j = n-1; i < n; j = i++) {
		float xi = points[i].x, zi = points[i].z;
		float xj = points[j].x, zj = points[j].z;
		if((zi > z) != (zj > z) && x < (xj-xi)*(z-zi)/(zj-zi) + xi)
			inside = !inside;
	}
	return inside;
}

void
StreamTrigger::Bounds(float &x0, float &x1, float &z0, float &z1) const
{
	x0 = z0 = 1e30f; x1 = z1 = -1e30f;
	for(size_t i = 0; i < points.size(); i++) {
		if(points[i].x < x0) x0 = points[i].x;
		if(points[i].x > x1) x1 = points[i].x;
		if(points[i].z < z0) z0 = points[i].z;
		if(points[i].z > z1) z1 = points[i].z;
	}
}

void
StreamTrigger::Packages(const char *slot, std::vector<std::string> &out) const
{
	for(size_t i = 0; i < loads.size(); i++)
		if(strcasecmp(loads[i].slot.c_str(), slot) == 0)
			out.push_back(loads[i].package);
}

const char*
StreamTrigger::Region(void) const
{
	static char buf[64];
	for(size_t i = 0; i < loads.size(); i++) {
		const std::string &p = loads[i].package;
		if(strcasecmp(loads[i].slot.c_str(), "Region_S") == 0 &&
		   p.size() > 7 && strcasecmp(p.c_str()+p.size()-7, "_region") == 0) {
			snprintf(buf, sizeof(buf), "%.*s", (int)(p.size()-7), p.c_str());
			return buf;
		}
	}
	return "";
}

StreamTriggerLoader::StreamTriggerLoader(void)
 : SimpleChunkHandler(STREAMTRIGGER)
{
}

// GetString stops at the first NUL when used as a C string, so the padding is harmless
void
StreamTriggerLoader::LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char buf[256];
	StreamTrigger *t = new StreamTrigger;
	t->name = f->GetString(buf);
	t->tag = f->GetString(buf);
	t->height = f->GetFloat();
	u32 n = f->GetU32();
	t->points.resize(n);
	for(u32 i = 0; i < n; i++) {
		t->points[i].x = f->GetFloat();
		t->points[i].y = f->GetFloat();
		t->points[i].z = f->GetFloat();
	}
	u32 np = f->GetU32();
	t->loads.resize(np);
	for(u32 i = 0; i < np; i++)
		t->loads[i].package = f->GetString(buf);
	u32 ns = f->GetU32();
	for(u32 i = 0; i < ns; i++) {
		f->GetString(buf);
		if(i < np) t->loads[i].slot = buf;
	}
	*pObject = t;
	*pUID = GetHash(t->name.c_str());
}

}
