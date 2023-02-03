#include "core.h"
#include "imagefactory.h"
#include "texture.h"
#include "pddi.h"

#include <assert.h>

namespace pure3d
{

using namespace content;

Texture::Texture(void)
{
	texture = device->NewTexture();
	texture->AddRef();
}

Texture::~Texture(void)
{
	texture->Release();
}


TextureLoader::TextureLoader(void)
 : content::SimpleChunkHandler(Texture::TEXTURE)
{
	imageFactory = new ImageFactory;
}

TextureLoader::~TextureLoader(void)
{
	delete imageFactory;
}

void
TextureLoader::LoadObject(IRefCount **pObject, u32 *pUID, ChunkFile *f, LoadInventory *inventory)
{
	Texture *texture = LoadTexture(f);
	if(texture) {
//		printf("Loaded texture %s\n", texture->GetName());
		*pObject = texture;
		*pUID = texture->GetUID();
	}
}

Texture*
TextureLoader::LoadTexture(content::ChunkFile *f)
{
	char name[128];
	f->GetString(name);
	i32 version = f->GetI32();
	i32 width = f->GetI32();
	i32 height = f->GetI32();
	i32 bpp = f->GetI32();
	i32 alphaDepth = f->GetI32();
	i32 numMipmaps = f->GetI32();
	if(numMipmaps) numMipmaps--;
	// pddi stuff
	u32 textureType = f->GetU32();
	u32 usage = f->GetU32();

//	printf("reading texture %s %d %d %d\n", name, width, height, bpp);

	// TODO: imageFactory stuff

	Texture *texture = nil;
	int mipmap = 0;
	bool volume = false;
	bool image = false;
	while(f->ChunksRemaining() && mipmap <= numMipmaps) {
		switch(f->BeginChunk()){
		case Texture::IMAGE:
			if(!volume) {
				texture = LoadImage(f, texture, mipmap);
				volume = false;
				image = true;
				mipmap++;
			}
			break;
		case Texture::VOLUME_IMAGE:
			if(!image) {
				assert(0 && "no volume images");
				volume = true;
				image = false;
				mipmap++;
			}
			break;
		}
		f->EndChunk();
	}

	if(texture)
		texture->SetName(name);
	return texture;
}

Texture*
TextureLoader::LoadImage(content::ChunkFile *f, Texture *buildTexture, int mipmap)
{
	// TODO: imageFactory stuff
	char name[128];
	f->GetString(name);
	i32 version = f->GetI32();
	i32 width = f->GetI32();
	i32 height = f->GetI32();
	i32 bpp = f->GetI32();
	i32 palettized = f->GetI32();
	i32 alpha = f->GetI32();
	u32 format = f->GetU32();

//	printf("reading image %s %d %d %d %X\n", name, width, height, bpp, format);

	Texture *texture = buildTexture;
	while(f->ChunksRemaining()) {
		switch(f->BeginChunk()){
		case Texture::IMAGE_DATA: {
			u32 sz = f->GetU32();
			LoadStream *s = f->BeginInset();
			if(texture)
				imageFactory->ParseIntoTexture(s, texture, sz, format, mipmap);
			else
				texture = imageFactory->ParseAsTexture(s, sz, format);
			f->EndInset(s);
			break;
		}

		case Texture::IMAGE_FILENAME: {
			char fileName[256];
			f->GetString(fileName);
			assert(0 && "cannot load images\n");
			break;
		}

		}
		f->EndChunk();
	}

	if(texture)
		texture->SetName(name);

	return texture;
}

}
