#include "../pure3d.h"
#include "../fake/pddi_fake.h"

class PrintNameLoader : public content::SimpleChunkHandler
{
public:
	PrintNameLoader(void) : content::SimpleChunkHandler(0) {}
	virtual void LoadObject(IRefCount **pObject, core::u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

void
PrintNameLoader::LoadObject(IRefCount **pObject, core::u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];
	f->GetString(name);
	printf("\t\tNameloader %X %X %s\n", f->BeginInset()->GetPosition(), f->GetCurrentID(), name);
}


class PrintNameLoader2 : public content::SimpleChunkHandler
{
public:
	PrintNameLoader2(void) : content::SimpleChunkHandler(0) {}
	virtual void LoadObject(IRefCount **pObject, core::u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

void
PrintNameLoader2::LoadObject(IRefCount **pObject, core::u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];
	f->GetU32();
	f->GetString(name);
	printf("\t\tNameloader2 %X %X %s\n", f->BeginInset()->GetPosition(), f->GetCurrentID(), name);
}


class PrintNameLoader3 : public content::SimpleChunkHandler
{
public:
	PrintNameLoader3(void) : content::SimpleChunkHandler(0) {}
	virtual void LoadObject(IRefCount **pObject, core::u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);
};

void
PrintNameLoader3::LoadObject(IRefCount **pObject, core::u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory)
{
	char name[256];
	f->GetLString(name);
	printf("\t\tNameloader3 %X %X %s\n", f->BeginInset()->GetPosition(), f->GetCurrentID(), name);
}

int
main()
{
	pure3d::initFake();


	content::loadManager = new content::LoadManager;
	content::loadManager->AddHandler(new content::P3DFileHandler, "p3d");
	content::loadManager->AddHandler(new pure3d::TextureLoader, pure3d::Texture::TEXTURE);
	content::loadManager->AddHandler(new pure3d::ShaderLoader, pure3d::Shader::SHADER);
	content::loadManager->AddHandler(new pure3d::GeometryLoader, pure3d::Geometry::MESH);
	content::loadManager->AddHandler(new pure3d::CompositeDrawableLoader, pure3d::CompositeDrawable::COMPOSITE_DRAWABLE);


	content::SimpleChunkHandler *printloader = new PrintNameLoader;
	content::SimpleChunkHandler *printloader2 = new PrintNameLoader2;
	content::SimpleChunkHandler *printloader3 = new PrintNameLoader3;
//	content::loadManager->AddHandler(printloader, 0x10000);
	content::loadManager->AddHandler(printloader, 0x1001A);
//	content::loadManager->AddHandler(printloader, 0x11000);
	content::loadManager->AddHandler(printloader, 0x23000);
	content::loadManager->AddHandler(printloader2, 0x121000);
//	content::loadManager->AddHandler(printloader2, 0x123000);
	content::loadManager->AddHandler(printloader3, 0x1000001);
	content::loadManager->AddHandler(printloader3, 0x1005008);
	content::loadManager->AddHandler(printloader, 0x7010000);
	content::loadManager->AddHandler(printloader, 0x7010025);
	content::loadManager->AddHandler(printloader, 0x7011000);
	content::loadManager->AddHandler(printloader, 0x8800001);
	content::loadManager->AddHandler(printloader, 0x8800008);
	content::loadManager->AddHandler(printloader, 0x8800181);


	content::LoadInventory *parentInv = new content::LoadInventory;
	parentInv->AddRef();

	content::LoadInventory *inv;
	inv = content::loadManager->LoadFile("../bacinari.p3d", parentInv);
//	inv = content::loadManager->LoadFile("../tony_only_bacinari.p3d", parentInv);
	if(inv == nil) {
		printf("couldn't load file\n");
		return 1;
	}

	inv->Dump();

	pure3d::Entity *tex = inv->Find<pure3d::Entity>("bacinari_map.tga");
	if(tex)
		printf("found %s\n", tex->GetName());
	pure3d::Shader *sh = inv->Find<pure3d::Shader>("vehicleShader3");
	if(sh)
		printf("found %s\n", sh->GetName());

	inv->Release();
	parentInv->Release();

	return 0;
}
