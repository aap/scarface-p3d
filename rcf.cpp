#include "core.h"
#include "rcf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace content
{

// ---------------------------------------------------------------- the name hash

u32
CementHash(const char *name)
{
	const char *s = name;
	// the archive spells paths with backslashes and hashes them blindly, so '/'
	// would hash as 'O'; fold it here instead
	if(*s == '\\' || *s == '/')
		s++;			// exactly one leading separator
	u32 h = 0;
	for(; *s; s++) {
		int c = (signed char)*s;
		if(c == '/')
			c = '\\';
		if(c < 'a')
			c += 32;	// NOT tolower(): digits and punctuation move too
		h = h*31 + c;
	}
	return h;
}

// ---------------------------------------------------------------- LZR / 'P3DZ'

// pure3d/p3d/lzr.cpp, byte-wise: the original copies matches four bytes at a time
// ("shortest match is 4 characters, so we can unroll the loop") which also makes it
// read past the end of a short match; matches may overlap, so byte-wise is both safe
// and what the compressor assumes.
void
LZRDecompress(const u8 *in, u32 insize, u8 *out, u32 outsize)
{
	const u8 *inend = in + insize;
	u32 n = 0;
	while(n < outsize && in < inend) {
		u32 code = *in++;
		if(code > 15) {
			// a match: count in the low nibble, offset in the high nibble
			// plus 16 * the next byte
			u32 count = code & 15;
			if(count == 0) {
				count += 15;
				while(in < inend && *in == 0) {
					count += 255;
					in++;
				}
				count += *in++;
			}
			u32 offset = (code >> 4) | ((u32)*in++ << 4);
			if(offset == 0 || offset > n || n + count > outsize)
				return;		// corrupt
			const u8 *match = out + n - offset;
			while(count--)
				out[n++] = *match++;
		} else {
			// a literal run; code 0 means 15 + the extension, and those
			// first 15 bytes come before the extended count
			u32 count = code;
			if(count == 0) {
				while(in < inend && *in == 0) {
					count += 255;
					in++;
				}
				count += *in++;
				count += 15;
			}
			if(n + count > outsize || in + count > inend)
				return;		// corrupt
			memcpy(out + n, in, count);
			n += count;
			in += count;
		}
	}
}

u8*
P3DZDecompress(const u8 *in, u32 insize, u32 *outsize)
{
	if(insize < 8 || (memcmp(in, "P3DZ", 4) != 0 && memcmp(in, "ZD3P", 4) != 0))
		return nil;
	u32 total;
	memcpy(&total, in+4, 4);
	if(memcmp(in, "ZD3P", 4) == 0)	// DATA_FILE_COMPRESSED_SWAP: byte-swapped host
		total = (total>>24) | ((total>>8)&0xFF00) | ((total<<8)&0xFF0000) | (total<<24);
	u8 *out = new u8[total];
	u32 off = 8, n = 0;
	while(n < total && off + 8 <= insize) {
		u32 clen, ulen;
		memcpy(&clen, in+off, 4);
		memcpy(&ulen, in+off+4, 4);
		off += 8;
		if(off + clen > insize || n + ulen > total)
			break;
		LZRDecompress(in+off, clen, out+n, ulen);
		off += clen;
		n += ulen;
	}
	*outsize = n;
	return out;
}

// ---------------------------------------------------------------- the archive

bool
RCFArchive::Open(const char *filename)
{
	Close();
	fp = fopen(filename, "rb");
	if(fp == nil)
		return false;
	path = filename;

	u8 hdr[0x3c];
	if(fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) ||
	   memcmp(hdr, "ATG CORE CEMENT LIBRARY", 23) != 0) {
		fprintf(stderr, "%s: not an ATG CORE CEMENT LIBRARY\n", filename);
		Close();
		return false;
	}
	u32 h[7];
	memcpy(h, hdr+0x20, sizeof(h));
	u32 dirOffset = h[1], nameOffset = h[3], nameSize = h[4], nFiles = h[6];
	if(nFiles == 0 || nFiles > 1000000) {
		fprintf(stderr, "%s: %u files?\n", filename, nFiles);
		Close();
		return false;
	}

	// the directory: (hash, offset, size), sorted ascending by hash. Little endian
	// in both shipped archives (PC and PS2 are both little endian hosts).
	static_assert(sizeof(Entry) == 12, "RCF directory entry must be 12 bytes");
	dir.resize(nFiles);
	if(fseek(fp, dirOffset, SEEK_SET) != 0 ||
	   fread(&dir[0], 12, nFiles, fp) != nFiles) {
		fprintf(stderr, "%s: short directory\n", filename);
		Close();
		return false;
	}

	// the name table: a u32 flags + u32 zero header, then one record per file in
	// *packer* order, so the names have to be matched back through the hash
	names.assign(nFiles, std::string());
	u8 *nt = new u8[nameSize];
	if(fseek(fp, nameOffset, SEEK_SET) == 0 && fread(nt, 1, nameSize, fp) == nameSize) {
		u32 off = 8;
		for(u32 i = 0; i < nFiles && off + 16 <= nameSize; i++) {
			u32 len;
			memcpy(&len, nt+off+12, 4);	// timestamp, flags, zero, nameLen
			off += 16;
			if(len == 0 || off + len > nameSize)
				break;
			std::string name((const char*)nt+off, len-1);	// len counts the NUL
			off += len + 3;			// always 3 filler bytes
			// (a name record whose hash has no directory entry is a stale
			// one left by the packer --- the PS2 archive has exactly one)
			const Entry *e = Find(name.c_str());
			if(e && names[e - &dir[0]].empty())
				names[e - &dir[0]] = name;
		}
	} else
		fprintf(stderr, "%s: short name table\n", filename);
	delete[] nt;
	return true;
}

void
RCFArchive::Close(void)
{
	if(fp)
		fclose(fp);
	fp = nil;
	path.clear();
	dir.clear();
	names.clear();
}

const RCFArchive::Entry*
RCFArchive::Find(const char *name) const
{
	u32 hash = CementHash(name);
	u32 lo = 0, hi = (u32)dir.size();
	while(lo < hi) {
		u32 mid = (lo + hi)/2;
		if(dir[mid].hash < hash) lo = mid+1;
		else hi = mid;
	}
	return lo < dir.size() && dir[lo].hash == hash ? &dir[lo] : nil;
}

void
RCFArchive::List(const char *prefix, std::vector<std::string> &out) const
{
	// the prefix may be spelled either way round; compare a backslash copy
	std::string pre = prefix;
	for(u32 i = 0; i < pre.size(); i++)
		if(pre[i] == '/') pre[i] = '\\';
	for(u32 i = 0; i < names.size(); i++)
		if(strncasecmp(names[i].c_str(), pre.c_str(), pre.size()) == 0 && !names[i].empty())
			out.push_back(names[i]);
}

LoadStream*
RCFArchive::OpenFile(const char *name)
{
	const Entry *e = Find(name);
	if(e == nil || fp == nil)
		return nil;
	// one read of the whole entry instead of mmap: portable, and ChunkFile reads
	// its files in a great many tiny sequential pieces anyway
	u8 *data = new u8[e->size];
	if(fseek(fp, e->offset, SEEK_SET) != 0 || fread(data, 1, e->size, fp) != e->size) {
		fprintf(stderr, "%s: short read of %s (%u bytes at %u)\n",
			path.c_str(), name, e->size, e->offset);
		delete[] data;
		return nil;
	}
	// a compressed file is 'P3DZ' + LZR blocks; neither shipped cement.rcf has one
	u32 usize;
	u8 *plain = P3DZDecompress(data, e->size, &usize);
	if(plain) {
		delete[] data;
		return new LoadStream(plain, usize, true);
	}
	return new LoadStream(data, e->size, true);
}

// ---------------------------------------------------------------- the mount

static RCFArchive *mounted;
static std::vector<std::string> contentRoots;

bool
MountRCF(const char *path)
{
	if(path == nil || *path == '\0')
		return false;
	RCFArchive *rcf = new RCFArchive;
	if(!rcf->Open(path)) {
		delete rcf;
		return false;
	}
	delete mounted;
	mounted = rcf;
	printf("mounted %s: %u files\n", path, rcf->NumEntries());
	return true;
}

RCFArchive*
MountedRCF(void)
{
	return mounted && mounted->IsOpen() ? mounted : nil;
}

void
AddContentRoot(const char *dir)
{
	std::string r = dir;
	while(!r.empty() && (r[r.size()-1] == '/' || r[r.size()-1] == '\\'))
		r.erase(r.size()-1);
	contentRoots.push_back(r);
}

const std::vector<std::string>&
ContentRoots(void)
{
	return contentRoots;
}

bool
ContentFileExists(const char *name)
{
	if(RCFArchive *rcf = MountedRCF())
		if(rcf->Find(name))
			return true;
	for(u32 i = 0; i <= contentRoots.size(); i++) {
		std::string p = i == 0 ? std::string(name) : contentRoots[i-1] + "/" + name;
		FILE *f = fopen(p.c_str(), "rb");
		if(f) {
			fclose(f);
			return true;
		}
	}
	return false;
}

// a file on disk, LZR-decompressed if it happens to be a 'P3DZ' container (retail
// does this in tChunkFile's constructor, for any file)
static LoadStream*
OpenLooseFile(const char *path)
{
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return nil;
	char magic[4] = { 0, 0, 0, 0 };
	size_t got = fread(magic, 1, 4, f);
	if(got != 4 || (memcmp(magic, "P3DZ", 4) != 0 && memcmp(magic, "ZD3P", 4) != 0)) {
		fclose(f);
		return new LoadStream(path);
	}
	fseek(f, 0, SEEK_END);
	u32 size = (u32)ftell(f);
	fseek(f, 0, SEEK_SET);
	u8 *data = new u8[size];
	bool ok = fread(data, 1, size, f) == size;
	fclose(f);
	u32 usize = 0;
	u8 *plain = ok ? P3DZDecompress(data, size, &usize) : nil;
	delete[] data;
	return plain ? new LoadStream(plain, usize, true) : nil;
}

LoadStream*
OpenContentFile(const char *name)
{
	bool verbose = getenv("P3D_VERBOSE") != nil;
	if(RCFArchive *rcf = MountedRCF()) {
		LoadStream *s = rcf->OpenFile(name);
		if(s) {
			if(verbose)
				printf("%s <- %s\n", name, rcf->GetPath());
			return s;
		}
	}
	// a plain path (loose files, absolute paths, ../foo.p3d), then the roots
	for(u32 i = 0; i <= contentRoots.size(); i++) {
		std::string p = i == 0 ? std::string(name) : contentRoots[i-1] + "/" + name;
		LoadStream *s = OpenLooseFile(p.c_str());
		if(s && s->IsOpen()) {
			if(verbose)
				printf("%s <- %s\n", name, p.c_str());
			return s;
		}
		if(s) {
			s->AddRef();
			s->Release();
		}
	}
	return nil;
}

}
