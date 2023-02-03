const char *shader_frag_src =
"#version 120\n"
"\n"
"varying vec4 v_color;\n"
"varying vec2 v_tex0;\n"
"\n"
"uniform sampler2D tex0;\n"
"\n"
"uniform vec4 u_alphaTest;\n"
"\n"
"void DoAlphaTest(float a)\n"
"{\n"
"	if(a < u_alphaTest.x || a >= u_alphaTest.y)\n"
"		discard;\n"
"}\n"
"\n"
"void\n"
"main(void)\n"
"{\n"
"	// TODO: eventually we should fix textures instead\n"
"	vec2 flipped = vec2(v_tex0.x, -v_tex0.y);\n"
"	vec4 color = v_color*texture2D(tex0, flipped);\n"
"	DoAlphaTest(color.w);\n"
"	gl_FragColor = color;\n"
"}\n"
;