#version 450

layout (location = 0) out vec4 out_Color;

layout (location = 0) in vec4 ex_Color;

layout (binding = 1) uniform sampler2D in_Texture;

void main()
{
	out_Color = ex_Color;
}