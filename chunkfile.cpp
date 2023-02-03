#include "core.h"
#include "chunkfile.h"

#include <string.h>
#include <assert.h>

namespace content
{

ChunkFile::ChunkFile(LoadStream *f)
 : stackTop(-1)
{
	realFile = f;
	realFile->AddRef();
	u32 id = realFile->GetU32();
	if(id != 0xFF443350){
		fprintf(stderr, "ERROR: not a pure3d chunk file\n");
		return;
	}
	BeginChunk(id);
}

bool
ChunkFile::ChunksRemaining(void)
{
	Chunk *c = &chunkStack[stackTop];
	return c->chunkLength > c->dataLength &&
	       realFile->GetPosition() < c->startPosition+c->chunkLength;
}

void indent(int n) { while(n--) putchar(' '); }

u32
ChunkFile::BeginChunk(void)
{
	stackTop++;

	// skip end to end of chunk
	if(stackTop != 0) {
		u32 start = chunkStack[stackTop-1].startPosition + chunkStack[stackTop-1].dataLength;
		if(realFile->GetPosition() < start)
			realFile->Advance(start - realFile->GetPosition());
	}
	chunkStack[stackTop].startPosition = realFile->GetPosition();
	chunkStack[stackTop].id = GetU32();
	chunkStack[stackTop].dataLength = GetU32();
	chunkStack[stackTop].chunkLength = GetU32();
//indent(stackTop);
//printf("Chunk %08X at %08X (%08X %08X)\n", chunkStack[stackTop].id, chunkStack[stackTop].startPosition, chunkStack[stackTop].dataLength, chunkStack[stackTop].chunkLength);

	return chunkStack[stackTop].id;
}

u32
ChunkFile::BeginChunk(u32 chunkID)
{
	stackTop++;
	chunkStack[stackTop].startPosition = realFile->GetPosition() - 4;
	chunkStack[stackTop].id = chunkID;
	chunkStack[stackTop].dataLength = GetU32();
	chunkStack[stackTop].chunkLength = GetU32();
//indent(stackTop);
//printf("Chunk %08X at %08X (%08X %08X)\n", chunkStack[stackTop].id, chunkStack[stackTop].startPosition, chunkStack[stackTop].dataLength, chunkStack[stackTop].chunkLength);

	return chunkStack[stackTop].id;
}

void
ChunkFile::EndChunk(void)
{
	u32 start = chunkStack[stackTop].startPosition;
	u32 chunkLength = chunkStack[stackTop].chunkLength;
	realFile->Advance(start + chunkLength - realFile->GetPosition());
	stackTop--;
}

u32
ChunkFile::GetCurrentID(void)
{
	return chunkStack[stackTop].id;
}

void
ChunkFile::SetName(const char *name)
{
	strncpy(filename, name, sizeof(filename)-1);
}

LoadStream*
ChunkFile::BeginInset(void)
{
	return realFile;
}

void
ChunkFile::EndInset(LoadStream *f)
{
	assert(f->GetPosition() <= chunkStack[stackTop].startPosition + chunkStack[stackTop].chunkLength);
}

}
