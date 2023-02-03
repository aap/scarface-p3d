#pragma once

#include "refcount.h"

#include <string>

namespace pure3d
{

using namespace core;

class Entity : public content::LoadObject
{
	// totally wrong
	std::string name;
public:
	// TODO: Clone where? LoadObject maybe?
	virtual void SetName(const char *s)  { name = s; }
	const char *GetName(void) { return name.c_str(); }
	u32 GetUID(void) { return core::MakeKey(name.c_str()); }
};

}
