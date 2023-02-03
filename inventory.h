#pragma once

#include "refcount.h"

#include <map>
#include <vector>

namespace content
{

using namespace core;

class LoadInventory : public LoadObject
{
	typedef std::multimap<int, IRefCount*> HashMap;

	HashMap *store;
	LoadInventory *parent;
public:
	class BaseCaster {
	public:
		virtual IRefCount *cast(IRefCount *obj) const = 0;
	};
	template <class T> class DynamicCaster : public BaseCaster {
	public:
		virtual IRefCount *cast(IRefCount *obj) const {
			return dynamic_cast<T*>(obj);
		}
	};

	CLASSNAME(LoadInventory)
	LoadInventory(void);
	~LoadInventory(void);
	void SetParent(LoadInventory *parent);
	void Add(u32 uid, IRefCount *obj);
	IRefCount *Find(const BaseCaster &c, u32 uid);
	template <class T> T *Find(u32 uid) { return (T*)Find(DynamicCaster<T>(), uid); }
	template <class T> T *Find(const char *name) { return Find<T>(MakeKey(name)); }
	template <class T> void Collect(std::vector<T*> &vec) {
	for(auto it = store->begin(); it != store->end(); it++) {
			T *e = dynamic_cast<T*>(it->second);
			if(e)
				vec.push_back(e);
		}
	}
	void Dump(void);
};

}
