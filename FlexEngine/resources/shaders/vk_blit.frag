#version 450

layout (binding = 0) uniform sampler2D in_Texture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Colour;

void main()
{
	out_Colour = texture(in_Texture, ex_TexCoord);
}
