#pragma once

#include "core.h"
#include "loadstream.h"

#include <string>
#include <vector>

namespace content
{

using namespace core;

// The hash the cement directory is sorted by: radcore's radMakeCaseInsensitiveKey32
// (PS2 0x7541dc, PC in the same core:: file layer). h*31 over a *blind* lower-casing
// (c < 'a' ? c+32 : c, so '.' -> 'N', '0' -> 'P', '\\' -> '|'), one leading backslash
// skipped. re/notes/ps2.md §1.1; '/' is normalised to '\\' first.
u32 CementHash(const char *name);

// "ATG CORE CEMENT LIBRARY" == cement.rcf, the archive the game ships all its data in.
// retail: the magic check is PC sub_6e6790, CementLibrary::OpenFile PC 0x6e6810 --- which
// takes a *hash*, not a name (it sprintf's "0x%X" for the sub-file's debug name), so no
// file name ever reaches the archive code. Layout in re/notes/ps2.md §1.2 and re/rcf.py.
class RCFArchive
{
public:
	struct Entry {
		u32 hash, offset, size;
	};

	RCFArchive(void) : fp(nil) {}
	~RCFArchive(void) { Close(); }

	bool Open(const char *path);
	void Close(void);
	bool IsOpen(void) const { return fp != nil; }
	const char *GetPath(void) const { return path.c_str(); }

	u32 NumEntries(void) const { return (u32)dir.size(); }
	const Entry &GetEntry(u32 i) const { return dir[i]; }
	// the name of entry i, "" for a directory entry no name record hashes to
	const char *GetName(u32 i) const { return names[i].c_str(); }

	// binary search by hash, like the game
	const Entry *Find(const char *name) const;
	// every name under a directory prefix ("packages/z04/"), in directory order
	void List(const char *prefix, std::vector<std::string> &out) const;

	// the entry's bytes as a stream ChunkFile can read; 'P3DZ' entries come out
	// LZR-decompressed. nil if the name is not in the archive.
	LoadStream *OpenFile(const char *name);

private:
	std::string path;
	FILE *fp;
	std::vector<Entry> dir;		// as in the file: sorted ascending by hash
	std::vector<std::string> names;	// parallel to dir, matched through the hash
};

// The one mounted archive (p3dview: -rcf <path>, $P3D_RCF) plus the loose-file roots
// that stand in for it when a name is not in it (the extracted ../assets tree).
bool MountRCF(const char *path);
RCFArchive *MountedRCF(void);
void AddContentRoot(const char *dir);
const std::vector<std::string> &ContentRoots(void);
// is <name> there at all? (the archive's directory, or a loose file) --- for the
// callers that used to probe with fopen
bool ContentFileExists(const char *name);

// What P3DFileHandler resolves a file name through: the mounted archive first (by
// hash, so case and / vs \\ do not matter), then <name> as a plain path, then
// <root>/<name> for every content root. nil if nothing has it.
LoadStream *OpenContentFile(const char *name);

// LZR (Lempel-Ziv-Radical), a port of pure3d/p3d/lzr.cpp: literal runs and matches,
// both with a 4-bit count extended by 0-terminated 255s. `out` must hold outsize.
void LZRDecompress(const u8 *in, u32 insize, u8 *out, u32 outsize);
// 'P3DZ' (Pure3D::DATA_FILE_COMPRESSED 0x5A443350): u32 total uncompressed size, then
// blocks { u32 compressedSize, u32 uncompressedSize, u8 data[compressedSize] } of 4 KB
// each; the concatenation is the plain 'P3D\xff' file. tChunkFile::tChunkFile +
// tFileMem::SetCompressed, written by tools/commandline/p3dcompress. Returns a new[]
// buffer, nil if the data is not a P3DZ container.
u8 *P3DZDecompress(const u8 *in, u32 insize, u32 *outsize);

}
