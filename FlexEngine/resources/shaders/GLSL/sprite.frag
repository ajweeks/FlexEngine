#version 450

uniform sampler2D in_Texture;

in vec2 ex_TexCoord;
in vec3 ex_Color;

out vec4 out_Color;

void main()
{
	out_Color = vec4(ex_Color, 1) * texture(in_Texture, ex_TexCoord);
}