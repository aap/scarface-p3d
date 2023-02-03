#include "core.h"
#include "loadstream.h"
#include "chunkfile.h"
#include "loadmanager.h"
#include "entity.h"

#include <string.h>
#include <assert.h>

namespace content
{

LoadRequest::LoadRequest(LoadOptions &op)
 : state(CREATED), inventory(nil), stream(nil), options(op)
{
	if(options.resolverInventory)
		options.resolverInventory->AddRef();
//printf("creating request %p\n", this);
}

LoadRequest::~LoadRequest(void)
{
	Release(inventory);
	Release(options.resolverInventory);
	Release(stream);
//printf("deleting request %p\n", this);
}

void
LoadRequest::SetState(u32 st)
{
	state = st;
}

void LoadRequest::SetFilename(const char *filename)
{
	if(filename)
		memcpy(options.filename, filename, sizeof(options.filename)-1);
}


LoadManager *loadManager;

void
LoadManager::AddHandler(ObjectLoader *loader, u32 chunkID)
{
	ObjectLoader *old = chunkHandlers[chunkID];
	loader->AddRef();
	chunkHandlers[chunkID] = loader;
	if(old) old->Release();
}

void
LoadManager::AddHandler(FileLoader *loader, const char *ext)
{
	FileLoader *old = fileHandlers[ext];
	loader->AddRef();
	fileHandlers[ext] = loader;
	if(old) old->Release();
}

ObjectLoader*
LoadManager::GetHandler(u32 chunkID)
{
	return chunkHandlers[chunkID];
}

FileLoader*
LoadManager::GetHandler(const char *ext)
{
	return fileHandlers[ext];
}

void
LoadManager::Load(LoadOptions &options, LoadRequest **pReq)
{
	LoadRequest *req = new LoadRequest(options);
	req->AddRef();
	req->AddRef();	// for queue
	requestQueue.push(req);
	req->SetFilename(options.filename);
	req->SetState(LoadRequest::QUEUED);
	if(options.stream)
		req->SetStream(options.stream);
	// TODO: asynch loading
	while(req->GetState() != LoadRequest::COMPLETE)
		ServiceOne();
	*pReq = req;
}

void
LoadManager::ServiceOne(void)
{
	if(requestQueue.empty())
		return;

	LoadRequest *req = requestQueue.front();
	requestQueue.pop();

	if(req == nil)
		return;

	currentReq = req;
	if(req->GetState() != LoadRequest::CANCELED) {
		currentReq->SetState(LoadRequest::LOADING);

		const char *ext = strrchr(req->GetOptions()->filename, '.');
		assert(ext);
		FileLoader *loader = GetHandler(ext+1);
		assert(loader);
		loader->LoadFile(req->GetOptions(), req);

		if(currentReq->GetState() == LoadRequest::LOADING)
			currentReq->SetState(LoadRequest::COMPLETE);
	}

	LoadObject::Release(currentReq);
}

LoadInventory*
LoadManager::LoadFile(const char *filename, LoadInventory *resolveInv)
{
	LoadRequest *req = nil;
	LoadOptions options;
	strcpy(options.filename, filename);
	options.resolverInventory = resolveInv;
	Load(options, &req);
	LoadInventory *inv = req->GetInventory();
	if(inv)
		inv->AddRef();
	req->Release();
	return inv;
}





void
ReadShit(ChunkFile *cf)
{
	while(cf->ChunksRemaining()) {
		cf->BeginChunk();
		ReadShit(cf);
		cf->EndChunk();
	}
}

void
P3DFileHandler::LoadFile(LoadOptions *options, LoadRequest *req)
{
	LoadStream *stream = req->GetStream();
	if(stream == nil) {
printf("opening file %s\n", options->filename);
		// not accurate
		stream = new LoadStream(options->filename);
		if(!stream->IsOpen()) {
			fprintf(stderr, "couldn't open file %s\n", options->filename);
			stream->AddRef();
			stream->Release();
			return;
		}
		req->SetStream(stream);
	}

	options->stream = stream;
	stream->AddRef();

	LoadInventory *inventory = new LoadInventory;
	inventory->SetParent(options->resolverInventory);
	inventory->AddRef();
	req->SetInventory(inventory);

	ChunkFile cf(stream);
	cf.SetName(options->filename);

	while(cf.ChunksRemaining()) {
		if(req->GetState() == LoadRequest::CANCELED)
			break;
		cf.BeginChunk();
		ObjectLoader *loader = loadManager->GetHandler(cf.GetCurrentID());
		if(loader) {
			loader->AddRef();
			IRefCount *object = nil;
			u32 UID = 0;	// or possibly name....
			SimpleChunkHandler *cl = dynamic_cast<SimpleChunkHandler*>(loader);
			if(cl) {
				// so no multithreaded loading...
				cl->chunkFile = &cf;
				cl->Load(&object, &UID, inventory);
				if(object)
					object->AddRef();
				cl->chunkFile = nil;
			} else {
				// TODO: something else
			}
			loader->Release();

			if(object) {
				inventory->Add(UID, object);
				object->Release();
			}
		} else
			0 && printf("no loader for chunk %X\n", cf.GetCurrentID());
//		ReadShit(&cf);
		cf.EndChunk();
	}

	inventory->SetParent(nil);
	if(req->GetState() == LoadRequest::CANCELED)
		req->SetInventory(nil);
	stream->Release();	// BUG? dangling pointer in options
	inventory->Release();
}


void
SimpleChunkHandler::Load(IRefCount **pObject, u32 *pUID, LoadInventory *inventory)
{
//	printf("loading simple chunk %X\n", chunkID);
	LoadObject(pObject, pUID, chunkFile, inventory);
}

}
