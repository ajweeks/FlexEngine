#version 450

layout (location = 0) in vec2 in_Position2D;
layout (location = 1) in vec2 in_TexCoord;

layout (location = 0) out vec4 ex_Color;

void main()
{
	ex_Color = vec4(in_Position2D, in_TexCoord);
}