#version 450

uniform sampler2D in_Texture;

in vec2 ex_TexCoord;
in vec4 ex_Color;

out vec4 out_Color;

void main()
{
	out_Color = ex_Color * texture(in_Texture, ex_TexCoord);
}