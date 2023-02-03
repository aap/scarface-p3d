#include "../pddi.h"
#include "pddi_gl.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

namespace pure3d
{

UniformRegistry uniformRegistry;
glProgram *currentProgram;

static int uniformTypesize[] = {
        0, 4, 4, 16
};

i32
UniformRegistry::Register(const char *name, UniformType type, i32 num)
{
	i32 id = Find(name);
	if(id >= 0) {
		Uniform *u = &uniforms[id];
		assert(u->type == type);
		assert(u->num == num);
		return id;
	}
	Uniform *u = &uniforms[numUniforms++];
	assert(numUniforms < MAX_UNIFORMS);
	u->name = strdup(name);
	u->type = type;
	u->serial = 0;
	if(type == UNIFORM_NA) {
		u->num = 0;
		u->data = nil;
	} else {
		assert(type <= UNIFORM_MAT4);
		u->num = num;
		u->data = new u8[uniformTypesize[type]*num*sizeof(float)];
	}
	return u - uniforms;
}

i32
UniformRegistry::Find(const char *name)
{
	for(i32 i = 0; i < numUniforms; i++)
		if(strcmp(name, uniforms[i].name) == 0)
			return i;
	return -1;
}

void
UniformRegistry::SetUniform(i32 id, const void *data)
{
	assert(id >= 0 && id < numUniforms);
	Uniform *u = &uniforms[id];
	assert(u->type > UNIFORM_NA && u->type <= UNIFORM_MAT4);
	u32 sz = uniformTypesize[u->type]*u->num*sizeof(float);
	if(memcmp(u->data, data, sz) != 0) {
		memcpy(u->data, data, sz);
		u->serial++;
	}
}

void
UniformRegistry::Flush(void)
{
	for(i32 i = 0; i < numUniforms; i++) {
		// Check if program even expects to know about this uniform
		if(i >= currentProgram->numUniforms)
			continue;
		// Check if program uses this uniform
		i32 loc = currentProgram->uniforms[i].location;
		if(loc == -1)
			continue;

		Uniform *u = &uniforms[i];
		// Check if program has an old value of this uniform
		// and update if that's the case
		if(currentProgram->uniforms[i].serial != u->serial)
			switch(u->type) {
			case UNIFORM_NA:
				break;
			case UNIFORM_VEC4:
				glUniform4fv(loc, u->num, (GLfloat*)u->data);
				break;
			case UNIFORM_IVEC4:
				glUniform4iv(loc, u->num, (GLint*)u->data);
				break;
			case UNIFORM_MAT4:
				glUniformMatrix4fv(loc, u->num, GL_FALSE, (GLfloat*)u->data);
				break;
			}
		currentProgram->uniforms[i].serial = u->serial;
	}
}




static void
printShaderSource(const char **src)
{
	int f;
	const char *file;
	bool printline;
	int line = 1;
	for(f = 0; src[f]; f++) {
		int fileline = 1;
		char c;
		file = src[f];
		printline = true;
		while(c = *file++, c != '\0') {
			if(printline)
				printf("%.4d/%d:%.4d: ", line++, f, fileline++);
			putchar(c);
			printline = c == '\n';
		}
		putchar('\n');
	}
}

static int
compileshader(GLenum type, const char **src, GLuint *shader)
{
	GLint n;
	GLint shdr, success;
	GLint len;
	char *log;

	for(n = 0; src[n]; n++);

	shdr = glCreateShader(type);
	glShaderSource(shdr, n, src, nil);
	glCompileShader(shdr);
	glGetShaderiv(shdr, GL_COMPILE_STATUS, &success);
	if(!success) {
		printShaderSource(src);
		fprintf(stderr, "Error in %s shader\n",
				type == GL_VERTEX_SHADER ? "vertex" : "fragment");
		glGetShaderiv(shdr, GL_INFO_LOG_LENGTH, &len);
		log = (char*)malloc(len);
		glGetShaderInfoLog(shdr, len, nil, log);
		fprintf(stderr, "%s\n", log);
		free(log);
		return 1;
	}
	*shader = shdr;
	return 0;
}

static int
linkprogram(GLint vs, GLint fs, GLuint *program)
{
	GLint prog, success;
	GLint len;
	char *log;

	prog = glCreateProgram();

	glBindAttribLocation(prog, ATTRIB_POS, "in_pos");
	glBindAttribLocation(prog, ATTRIB_NORMAL, "in_normal");
	glBindAttribLocation(prog, ATTRIB_COLOR, "in_color");
	glBindAttribLocation(prog, ATTRIB_WEIGHTS, "in_weights");
	glBindAttribLocation(prog, ATTRIB_INDICES, "in_indices");
	glBindAttribLocation(prog, ATTRIB_TEXCOORDS0, "in_tex0");
	glBindAttribLocation(prog, ATTRIB_TEXCOORDS1, "in_tex1");

	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &success);
	if(!success) {
		fprintf(stderr, "Error in program\n");
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
		log = (char*)malloc(len);
		glGetProgramInfoLog(prog, len, nil, log);
		fprintf(stderr, "%s\n", log);
		free(log);
		return 1;
	}
	*program = prog;
	return 0;
}



// TODO: sucks we have no errors
glProgram::glProgram(const char **vsrc, const char **fsrc)
 : program(0), numUniforms(0), uniforms(nil)
{
	GLuint vs, fs;
	int i, fail;

	if(compileshader(GL_VERTEX_SHADER, vsrc, &vs))
		return;
	if(compileshader(GL_FRAGMENT_SHADER, fsrc, &fs)) {
		glDeleteShader(vs);
		return;
	}
	fail = linkprogram(vs, fs, &program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	if(fail)
		return;

#ifdef DUMPSHADERS
	int nUniforms;
	glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &nUniforms);
	printf("uniforms:\n");
	for(i = 0; i < nUniforms; i++){
		GLint size;
		GLenum type;
		char name[100];
		glGetActiveUniform(program, i, 100, nil, &size, &type, name);
		printf("%d %d %s\n", size, type, name);
	}
	printf("\n");

	int numAttribs;
	glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &numAttribs);
	printf("attribs:\n");
	for(i = 0; i < numAttribs; i++){
		GLint size;
		GLenum type;
		char name[100];
		glGetActiveAttrib(program, i, 100, nil, &size, &type, name);
		GLint bind = glGetAttribLocation(program, name);
		printf("%d %d %s. %d\n", size, type, name, bind);
	}
	printf("\n");
#endif

	// Set up uniform cache
	numUniforms = uniformRegistry.numUniforms;
	uniforms = new ProgUniform[numUniforms];
	for(i = 0; i < numUniforms; i++) {
		uniforms[i].location = glGetUniformLocation(program, uniformRegistry.uniforms[i].name);
		uniforms[i].serial = ~0;
	}

	if(currentProgram)
		glUseProgram(currentProgram->program);
}

glProgram::~glProgram(void)
{
	if(program) {
		if(currentProgram == this)
			glUseProgram(0);
		glDeleteProgram(program);
	}
	delete[] uniforms;
}

void
glProgram::Bind(void)
{
	if(currentProgram != this)
		glUseProgram(program);
	currentProgram = this;
}


}
