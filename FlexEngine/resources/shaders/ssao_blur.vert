#version 450

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;

layout (location = 0) out vec2 ex_UV;

void main()
{
	ex_UV = in_TexCoord;
    gl_Position = vec4(in_Position, 1.0);
}
