#pragma once
#include "../pddi.h"

#include <vector>

namespace pure3d
{

class fakeTexture : public pddiTexture
{
	// totally fake
	struct RawImage {
		u32 format;
		u32 sz;
		u8 *data;
	};
	std::vector<RawImage> imgdata;

public:
	virtual void AddMipmap(u32 format, u32 sz, u8 *data);
};


void initFake(void);

}
