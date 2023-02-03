#include "inventory.h"

#include "entity.h"

#include <stdio.h>

namespace content
{

LoadInventory::LoadInventory(void)
 : parent(nil)
{
	store = new HashMap;
//printf("new inventory %p\n", this);
}

LoadInventory::~LoadInventory(void)
{
	for(auto it = store->begin(); it != store->end(); it++)
		it->second->Release();
	Release(parent);
	delete store;
//printf("destroying inventory %p\n", this);
}

void
LoadInventory::SetParent(LoadInventory *p)
{
	Assign(parent, p);
}

void
LoadInventory::Add(u32 uid, IRefCount *obj)
{
	obj->AddRef();
	store->insert(std::pair<int, IRefCount*>(uid, obj));
}

IRefCount*
LoadInventory::Find(const BaseCaster &c, u32 uid)
{
	auto range = store->equal_range(uid);
	for(auto it = range.first; it != range.second; it++) {
		IRefCount *obj = c.cast(it->second);
		if(obj)
			return obj;
	}
	if(parent)
		return parent->Find(c, uid);
	return nil;
}

void
LoadInventory::Dump(void)
{
	printf("Inventory dump\n");
	for(auto it = store->begin(); it != store->end(); it++) {
		pure3d::Entity *e = dynamic_cast<pure3d::Entity*>(it->second);
		if(e)
			printf("\t%s\t%s\n", e->GetClassName(), e->GetName());
		else
			printf("\t%s\n", e->GetClassName());
	}
}

}
