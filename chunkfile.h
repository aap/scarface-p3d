#pragma once
#include "loadstream.h"

namespace content
{

using namespace core;

struct Chunk
{
	u32 id, dataLength, chunkLength, startPosition;
};

class ChunkFile
{
	char filename[128];
	Chunk chunkStack[32];
	int stackTop;
	LoadStream *realFile;

public:
	ChunkFile(LoadStream *f);
	~ChunkFile(void) { realFile->Release(); }

	bool ChunksRemaining(void);
	u32 BeginChunk(void);
	u32 BeginChunk(u32 chunkID);
	void EndChunk(void);
	u32 GetCurrentID(void);
	void SetName(const char *name);
	const char *GetName(void) { return filename; }

	LoadStream *BeginInset(void);
	void EndInset(LoadStream *f);

	void GetData(void *buf, u32 count, u32 sz = 1) { realFile->GetData(buf, count, sz); }
	u8 GetU8(void) { return realFile->GetU8(); }
	u16 GetU16(void) { return realFile->GetU16(); }
	u32 GetU32(void) { return realFile->GetU32(); }
	i8 GetI8(void) { return realFile->GetI8(); }
	i16 GetI16(void) { return realFile->GetI16(); }
	i32 GetI32(void) { return realFile->GetI32(); }
	float GetFloat(void) { return realFile->GetFloat(); }
	char *GetString(char *s) { u8 tmp = GetU8(); realFile->GetData(s, tmp); s[tmp] = '\0'; return s; }
	char *GetLString(char *s) { u32 tmp = GetU32(); realFile->GetData(s, tmp+1); return s; }
};

}
