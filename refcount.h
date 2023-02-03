#pragma once

#include "core.h"

namespace core
{

class IRefCount
{
public:
	// only for debugging
#define CLASSNAME(name) virtual const char *GetClassName(void) { return #name; }
	virtual const char *GetClassName(void) = 0;

	virtual void AddRef(void) = 0;
	virtual void Release(void) = 0;
	virtual int GetRef(void) = 0;
	virtual ~IRefCount(void) = 0;
};

inline IRefCount::~IRefCount(void) {}

}

namespace content
{

class LoadObject : public core::IRefCount
{
	int refCount;
public:
	LoadObject(void) : refCount(0) {}
	virtual void AddRef(void) { refCount++; }
	virtual void Release(void) { if(--refCount == 0) delete this; }
	virtual int GetRef(void) { return refCount; }
	virtual ~LoadObject(void) {}

	// total kludge to put this here
	template <class T> static void Assign(T *&oldRef, T *newRef) {
		if(newRef) newRef->AddRef();
		if(oldRef) oldRef->Release();
		oldRef = newRef;
	}
	template <class T> static void Release(T *&ref) {
		if(ref) {
			ref->Release();
			ref = nil;
		}
	}
};

}

namespace pure3d
{

// very unclear how this is different from LoadObject
class NonCopyable : public core::IRefCount
{
	int refCount;
public:
	NonCopyable(void) : refCount(0) {}
	virtual void AddRef(void) { refCount++; }
	virtual void Release(void) { if(--refCount == 0) delete this; }
	virtual int GetRef(void) { return refCount; }
	virtual ~NonCopyable(void) {}

	// total kludge again
	template <class T> static void Assign(T *&oldRef, T *newRef) {
		if(newRef) newRef->AddRef();
		if(oldRef) oldRef->Release();
		oldRef = newRef;
	}
	template <class T> static void Release(T *&ref) {
		if(ref) {
			ref->Release();
			ref = nil;
		}
	}
};

}
