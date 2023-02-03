#include "texture.h"
#include "pddi.h"
#include "imagefactory.h"

namespace pure3d
{

/*
 * All of this is fake
 */

Texture*
ImageFactory::ParseAsTexture(LoadStream *stream, u32 size, u32 format)
{
	Texture *texture = new Texture;

	u8 *data = new u8[size];
	stream->GetData(data, size);
	texture->GetTexture()->AddMipmap(format, size, data);
	delete[] data;

	return texture;
}

void
ImageFactory::ParseIntoTexture(LoadStream *stream, Texture *texture, u32 size, u32 format, i32 mipmap)
{
	u8 *data = new u8[size];
	stream->GetData(data, size);
	texture->GetTexture()->AddMipmap(format, size, data);
	delete[] data;
}

}
