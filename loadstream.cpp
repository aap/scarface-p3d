#include "core.h"
#include "loadstream.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

namespace content
{

LoadStream::LoadStream(const char *filename)
 : fp(nil), mem(nil), owned(nil), size(0), pos(0)
{
	OpenRead(filename);
}

LoadStream::LoadStream(const void *data, u32 sz, bool takeOwnership)
 : fp(nil), mem((const u8*)data), owned(takeOwnership ? (u8*)data : nil), size(sz), pos(0)
{
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
	fp = nil;
	delete[] owned;
	owned = nil;
	mem = nil;
}

bool
LoadStream::GetData(void *buf, u32 count, u32 sz)
{
	if(mem) {
		u32 n = count*sz;
		if(pos + n > size) {
			memset(buf, 0, n);
			pos = size;
			return false;
		}
		memcpy(buf, mem+pos, n);
		pos += n;
		return true;
	}
	return fread(buf, sz, count, fp) == count;
}

u32
LoadStream::GetSize(void)
{
	if(mem)
		return size;
	u32 here = ftell(fp);
	fseek(fp, 0, SEEK_END);
	u32 sz = ftell(fp);
	fseek(fp, here, SEEK_SET);
	return sz;
}

u32
LoadStream::GetPosition(void)
{
	return mem ? pos : (u32)ftell(fp);
}

void
LoadStream::Advance(u32 skip)
{
	if(mem) {
		// the chunk reader skips with unsigned arithmetic, so a wrapped count
		// (a chunk that is already finished) must not run off the buffer
		pos += skip > size - pos ? size - pos : skip;
	} else
		fseek(fp, skip, SEEK_CUR);
}

}
