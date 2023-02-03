#version 120

varying vec4 v_color;
varying vec2 v_tex0;

uniform sampler2D tex0;

uniform vec4 u_alphaTest;

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
	vec4 color = v_color*texture2D(tex0, flipped);
	DoAlphaTest(color.w);
	gl_FragColor = color;
}
