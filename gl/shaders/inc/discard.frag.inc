const char *discard_frag_src =
"#version 120\n"
"\n"
"varying vec4 v_color;\n"
"varying vec2 v_tex0;\n"
"\n"
"uniform sampler2D tex0;\n"
"\n"
"void\n"
"main(void)\n"
"{\n"
"	discard;\n"
"}\n"
;
