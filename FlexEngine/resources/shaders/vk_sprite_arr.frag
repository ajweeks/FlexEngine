#version 450

layout (push_constant) uniform PushConstants
{
	layout (offset = 0) mat4 view;
	layout (offset = 64) mat4 proj;
	layout (offset = 128) uint textureLayer;
} pushConstants;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 colorMultiplier;
	bool enableAlbedoSampler;
} uboDynamic;

layout (binding = 1) uniform sampler2DArray in_Texture;

layout (location = 0) in vec2 ex_TexCoord;

layout (location = 0) out vec4 out_Color;

void main()
{
	if (uboDynamic.enableAlbedoSampler)
	{
		out_Color = uboDynamic.colorMultiplier * 
			texture(in_Texture, vec3(ex_TexCoord.x, 1-ex_TexCoord.y, pushConstants.textureLayer));
	}
	else
	{
		out_Color = uboDynamic.colorMultiplier;
	}
}