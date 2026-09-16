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
uniform vec4 u_debug;	// x: no lighting, y: no vertex colours
uniform vec4 u_vertexFade;	// x: start, y: end (view-space distance), z: enable

void
main(void)
{
	vec4 viewPos = u_view * u_world * vec4(in_pos, 1.0);
	gl_Position = u_proj * viewPos;
	vec3 N = mat3(u_world) * in_normal;

	vec3 lighting = u_ambientColour.rgb;
	vec3 Ldir = normalize(u_lightDir1.xyz);
	float l = max(0.0f, dot(N, -Ldir));
	lighting += l*u_lightColour1.rgb;
	if(u_debug.x > 0.0) lighting = vec3(0.5);
	vec4 col = u_debug.y > 0.0 ? vec4(1.0) : in_color;

	v_color.rgb = lighting*2*col.bgr;
	v_color.a = col.a;
	if(u_vertexFade.z > 0.0)
		v_color.a *= clamp((length(viewPos.xyz) - u_vertexFade.x) / (u_vertexFade.y - u_vertexFade.x), 0.0, 1.0);

	v_tex0 = in_tex0;
}

