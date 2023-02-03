#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "glad/glad.h"

#include "p3dview.h"

#ifndef nil
#define nil nullptr
#endif

void
InitGL(void *loadproc)
{
	gladLoadGLLoader((GLADloadproc)loadproc, 33);
}
