#pragma once

#include "core.h"
#include "entity.h"
#include "loadmanager.h"

#include <vector>

namespace pure3d
{

using namespace core;

class pddiTexture;

class Texture : public Entity
{
	pddiTexture *texture;
public:
	enum {
		TEXTURE = 0x19000,
		IMAGE = 0x19001,
		IMAGE_DATA = 0x19002,
		IMAGE_FILENAME = 0x19003,
		VOLUME_IMAGE = 0x19004
	};

	CLASSNAME(Texture)
	Texture(void);
	~Texture(void);

	pddiTexture *GetTexture(void) { return texture; }
};

class ImageFactory;

class TextureLoader : public content::SimpleChunkHandler
{
	// TODO: image converter
	ImageFactory *imageFactory;
public:
	CLASSNAME(TextureLoader)
	TextureLoader(void);
	~TextureLoader(void);
	virtual void LoadObject(IRefCount **pObject, u32 *pUID, content::ChunkFile *f, content::LoadInventory *inventory);

	Texture *LoadTexture(content::ChunkFile *f);
	Texture *LoadImage(content::ChunkFile *f, Texture *texture, int mipmap);
};

}
