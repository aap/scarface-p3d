#include "../pddi.h"
#include "pddi_gl.h"

#include <lodepng/lodepng.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

namespace pure3d
{

glTexture::glTexture(void)
{
	gltex = 0;
	numMips = 0;

	glGenTextures(1, &gltex);
}

void
glTexture::Bind(int stage)
{
	glActiveTexture(GL_TEXTURE0 + stage);
	glBindTexture(GL_TEXTURE_2D, gltex);
}

// this is totaly stupid
void
glTexture::AddMipmap(u32 format, u32 sz, u8 *data)
{
	if(numMips)
		return;
	numMips++;

	if(format != 1)
		return;	// can only do PNG here

	u8 *image;
	u32 width, height;
	u32 error = lodepng_decode32(&image, &width, &height, data, sz);
	if(error) {
		fprintf(stderr, "PNG error\n");
		return;
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gltex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, image);

	free(image);
}

}
