#version 450

uniform sampler2D in_Texture;
uniform vec4 colorMultiplier;
uniform bool enableAlbedoSampler;

in vec2 ex_TexCoord;

out vec4 out_Color;

void main()
{
	if (enableAlbedoSampler)
	{
		out_Color = colorMultiplier * texture(in_Texture, ex_TexCoord);
	}
	else
	{
		out_Color = colorMultiplier;
	}
}