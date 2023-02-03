#include "core.h"
#include "loadstream.h"

#include <stdio.h>
#include <assert.h>

namespace content
{

LoadStream::LoadStream(const char *filename)
{
	fp = nil;
	OpenRead(filename);
}

LoadStream::~LoadStream(void)
{
	Close();
}

bool
LoadStream::OpenRead(const char *filename)
{
	fp = fopen(filename, "rb");
	return fp != nil;
}

void
LoadStream::Close(void)
{
	if(fp)
		fclose(fp);
}

bool
LoadStream::GetData(void *buf, u32 count, u32 sz)
{
	return fread(buf, sz, count, fp) == count;
}

u32
LoadStream::GetSize(void)
{
	assert(0);
	return 0;	// TODO
}

u32
LoadStream::GetPosition(void)
{
	return ftell(fp);
}

void
LoadStream::Advance(u32 skip)
{
	fseek(fp, skip, SEEK_CUR);
}

}
