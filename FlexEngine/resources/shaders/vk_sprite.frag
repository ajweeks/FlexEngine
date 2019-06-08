#version 450

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 colorMultiplier;
	bool enableAlbedoSampler;
} uboDynamic;

layout (binding = 1) uniform sampler2D in_Texture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Color;

void main()
{
	if (uboDynamic.enableAlbedoSampler)
	{
		out_Color = uboDynamic.colorMultiplier * texture(in_Texture, ex_TexCoord);
	}
	else
	{
		out_Color = uboDynamic.colorMultiplier;
	}
}