#pragma once

#include "core.h"

namespace content
{
class LoadStream;
}

namespace pure3d
{

using namespace core;
using namespace content;

class Texture;

class ImageFactory
{
public:
	Texture *ParseAsTexture(LoadStream *stream, u32 size, u32 format);
	// added size
	void ParseIntoTexture(LoadStream *stream, Texture *tex, u32 size, u32 format, i32 mipmap);
};

}
