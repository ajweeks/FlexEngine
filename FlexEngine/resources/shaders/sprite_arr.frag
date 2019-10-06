#version 450

uniform sampler2DArray in_Texture;
uniform vec4 colorMultiplier;
uniform bool enableAlbedoSampler;
uniform uint layer;

in vec2 ex_TexCoord;

out vec4 out_Color;

void main()
{
	if (enableAlbedoSampler)
	{
		out_Color = colorMultiplier * texture(in_Texture, vec3(ex_TexCoord, layer));
	}
	else
	{
		out_Color = colorMultiplier;
	}
}