#pragma once

#include "refcount.h"
#include "inventory.h"
#include "chunkfile.h"

#include <map>
#include <queue>
#include <string>

namespace content
{

using namespace core;

struct LoadOptions
{
	char filename[256];
	LoadStream *stream;
	LoadInventory *resolverInventory;
//	int allocator;
//	unknown
//	bool syncLoad;
	LoadOptions(void) {
		filename[0] = 0;
		stream = nil;
		resolverInventory = nil;
	}
};

class LoadRequest : public LoadObject
{
//	u32 loadStartTime;
//	u32 loadCompleteTime;
	u32 state;
	LoadInventory *inventory;
	LoadStream *stream;
	LoadOptions options;
public:
	CLASSNAME(LoadRequest)
	enum { CREATED, QUEUED, LOADING, COMPLETE, CANCELED };
	LoadRequest(LoadOptions &options);
	~LoadRequest(void);
	u32 GetState(void) { return state; }
	void SetState(u32 state);
	void SetInventory(LoadInventory *inventory) { Assign(this->inventory, inventory); }
	LoadInventory *GetInventory(void) { return inventory; }
	void SetStream(LoadStream *stream) { Assign(this->stream, stream); }
	LoadStream *GetStream(void) { return stream; }
	void SetFilename(const char *filename);
	LoadOptions *GetOptions(void) { return &options; }
};

class FileLoader : public LoadObject
{
public:
	virtual void LoadFile(LoadOptions *options, LoadRequest *req) = 0;
};

class ObjectLoader : public LoadObject
{
	// TODO: more arguments
	virtual void Load(IRefCount **pObject, u32 *pUID, LoadInventory *inventory) = 0;
};



class P3DFileHandler : public FileLoader
{
public:
	CLASSNAME(P3DFileHandler)
	virtual void LoadFile(LoadOptions *options, LoadRequest *req);
};

class SimpleChunkHandler : public ObjectLoader
{
	u32 chunkID;
//	u32 status;	// ??
public:
	SimpleChunkHandler(u32 id) : chunkID(id), chunkFile(nil) {}
	virtual void Load(IRefCount **pObject, u32 *pUID, LoadInventory *inventory);
	virtual u32 GetChunkID(void) { return chunkID; }
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *cf, LoadInventory *inventory) = 0;

	ChunkFile *chunkFile;
};

class LoadManager
{
	LoadRequest *currentReq;
	std::map<std::string, FileLoader*> fileHandlers;
	std::map<u32, ObjectLoader*> chunkHandlers;
	std::queue<LoadRequest*> requestQueue;

public:
	CLASSNAME(LoadManager)
	LoadManager(void) : currentReq(nil) {}
	void AddHandler(ObjectLoader *loader, u32 chunkID);
	void AddHandler(FileLoader *loader, const char *ext);
	ObjectLoader *GetHandler(u32 chunkID);
	FileLoader *GetHandler(const char *ext);
	void Load(LoadOptions &options, LoadRequest **pReq);
	// wrong
	void ServiceOne(void);
	// convenience
	LoadInventory *LoadFile(const char *filename, LoadInventory *resolveInv);
};

extern LoadManager *loadManager;

}
