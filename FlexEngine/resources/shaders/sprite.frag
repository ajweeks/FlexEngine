#version 450

uniform sampler2D in_Texture;
uniform vec4 colorMultiplier;

in vec2 ex_TexCoord;

out vec4 out_Color;

void main()
{
	out_Color = colorMultiplier * texture(in_Texture, ex_TexCoord);
}