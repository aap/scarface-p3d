#version 120

varying vec4 v_color;
varying vec2 v_tex0;
varying float v_fogDist;

uniform sampler2D tex0;

uniform vec4 u_alphaTest;
uniform vec4 u_debug;	// z: no textures
// x: the per-primitive cross-fade of PDDI_SP_FADE as an alpha multiplier, 1 = opaque
uniform vec4 u_fade;

// pddiContext::SetFog(colour, start, end) / EnableFog
uniform vec4 u_fogColour;	// rgb: the fog colour, a: 0 = fog off
uniform vec4 u_fogRange;	// x: start, y: end, z: FogClamp/255, w: apply the clamp

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
	// the cross-fade goes in after the alpha test, so that an alpha-tested surface
	// keeps exactly the pixels it had (retail instead scales the test threshold down
	// as the fade rises, notes/shaderstate.md)
	color.a *= u_fade.x;
	// D3DFOG_LINEAR: f = (end - d)/(end - start), 1 = unfogged. Fog blends the colour
	// only; the alpha the frame buffer blends with is untouched.
	if(u_fogColour.a > 0.0) {
		float f = clamp((u_fogRange.y - v_fogDist)/(u_fogRange.y - u_fogRange.x), 0.0, 1.0);
		// retail's own shaders also get FogClamp/255 (vertex constant c49.w), which
		// D3D's fixed-function fog cannot express. What they do with it is not
		// reversed; reading it as a cap on the fog amount is the viewer's guess and is
		// off by default (View tab > Fog).
		if(u_fogRange.w > 0.0) f = max(f, 1.0 - u_fogRange.z);
		color.rgb = mix(u_fogColour.rgb, color.rgb, f);
	}
	gl_FragColor = color;
}
