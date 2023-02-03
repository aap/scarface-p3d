#pragma once

#include "core.h"

#include <assert.h>
#include <new>

// TODO: most of this is wrong. Initialization is done mostly with copy ctors
template <class T> class Array
{
	T *start;
	T *end;
public:
	Array(void) : start(nil), end(nil) {}
	Array(int n) : start(nil), end(nil) {
		Create(n);
	}
	~Array(void) {
		if(start == nil)
			return;
		for(T *i = start; i != end; i++)
			i->~T();
		delete[] (core::u8*)start;
	}
	void Create(int n) {
		if(Allocate(n))
			Reset();
	}
	bool Allocate(int n) {
		if(start)
			return false;
		if(n == 0) {
			start = nil;
			end = nil;
			return false;
		}
		start = (T*)new core::u8[n*sizeof(T)];
		end = start + n;
		return true;		
	}
	void Reset(void) {
		for(T *i = start; i != end; i++)
			new (i) T;
	}
	core::u32 Size(void) { return end - start; }
	T &operator[](core::u32 i) { assert(&start[i] < end); return start[i]; }
	T *At(core::u32 i) { return &start[i]; }
};

template <class T> class Array2 : public Array<T>
{
	int unknown;
public:
	Array2(void) : Array<T>() {}
	Array2(int n) : Array<T>(n) {}
};
