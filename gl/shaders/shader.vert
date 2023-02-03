#version 120

attribute vec3 in_pos;
attribute vec3 in_normal;
attribute vec4 in_color;
attribute vec2 in_tex0;

varying vec4 v_color;
varying vec2 v_tex0;

uniform mat4 u_world;
uniform mat4 u_view;
uniform mat4 u_proj;

// not using these
uniform vec4 u_matAmbient;
uniform vec4 u_matDiffuse;
uniform vec4 u_matSpecular;
uniform vec4 u_matEmissive;

uniform vec4 u_ambientColour;
uniform vec4 u_lightDir1;
uniform vec4 u_lightColour1;

void
main(void)
{
	gl_Position = u_proj * u_view * u_world * vec4(in_pos, 1.0);
	vec3 N = mat3(u_world) * in_normal;

	vec3 lighting = u_ambientColour.rgb;
	vec3 Ldir = normalize(u_lightDir1.xyz);
	float l = max(0.0f, dot(N, -Ldir));
	lighting += l*u_lightColour1.rgb;

	v_color.rgb = lighting*2*in_color.bgr;
	v_color.a = in_color.a;

	v_tex0 = in_tex0;
}

