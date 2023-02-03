#pragma once

#include "refcount.h"

#include <stdio.h>

namespace content
{

using namespace core;

// mostly my own thing
// incomplete
class LoadStream : public LoadObject
{
	FILE *fp;
public:
	CLASSNAME(LoadStream)
	LoadStream(const char *filename);
	~LoadStream(void);
	bool OpenRead(const char *filename);
	void Close(void);
	bool GetData(void *buf, u32 count, u32 sz = 1);
	u32 GetSize(void);
	u32 GetPosition(void);
	void Advance(u32 skip);
	bool IsOpen(void) { return fp != nil; }

	u8 GetU8(void) { u8 tmp; GetData(&tmp, 1, sizeof(tmp)); return tmp; }
	u16 GetU16(void) { u16 tmp; GetData(&tmp, 1, sizeof(tmp)); return tmp; }
	u32 GetU32(void) { u32 tmp; GetData(&tmp, 1, sizeof(tmp)); return tmp; }
	i8 GetI8(void) { i8 tmp; GetData(&tmp, 1, sizeof(tmp)); return tmp; }
	i16 GetI16(void) { i16 tmp; GetData(&tmp, 1, sizeof(tmp)); return tmp; }
	i32 GetI32(void) { i32 tmp; GetData(&tmp, 1, sizeof(tmp)); return tmp; }
	float GetFloat(void) { float tmp; GetData(&tmp, 1, sizeof(tmp)); return tmp; }
};

}
