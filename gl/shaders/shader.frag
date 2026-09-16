#version 120

varying vec4 v_color;
varying vec2 v_tex0;

uniform sampler2D tex0;

uniform vec4 u_alphaTest;
uniform vec4 u_debug;	// z: no textures

void DoAlphaTest(float a)
{
	if(a < u_alphaTest.x || a >= u_alphaTest.y)
		discard;
}

void
main(void)
{
	// TODO: eventually we should fix textures instead
	vec2 flipped = vec2(v_tex0.x, -v_tex0.y);
	vec4 tex = texture2D(tex0, flipped);
	if(u_debug.z > 0.0) tex = vec4(1.0, 1.0, 1.0, tex.a);
	vec4 color = v_color*tex;
	DoAlphaTest(color.w);
	gl_FragColor = color;
}
