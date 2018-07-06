#version 450

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

in vec3 in_Position;
in vec2 in_TexCoord;

out vec2 ex_TexCoord;

void main()
{
	ex_TexCoord = in_TexCoord;
	vec4 worldPos = vec4(in_Position, 1) * model;
	
	gl_Position = projection * view * worldPos;
}