#version 400 

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;

out vec2 ex_TexCoord;

uniform vec2 charResolution;
uniform float spread;

void main()
{
	ex_TexCoord = in_TexCoord;
	vec2 adjustment = vec2(spread * 2.0) / charResolution;
	ex_TexCoord -= vec2(0.5);
	ex_TexCoord *= (vec2(1.0) + adjustment);
	ex_TexCoord += vec2(0.5);
	gl_Position = vec4(in_Position, 1.0);
}