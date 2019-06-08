#version 450

uniform mat4 model;

in vec2 in_Position2D;
in vec2 in_TexCoord;

out vec2 ex_TexCoord;

void main()
{
	ex_TexCoord = in_TexCoord;
	vec3 transformedPos = (vec4(in_Position2D, 0, 1) * model).xyz;
	gl_Position = vec4(transformedPos, 1);
}