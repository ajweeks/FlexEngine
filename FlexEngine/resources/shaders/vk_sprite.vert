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

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;

layout (location = 0) out vec2 ex_TexCoord;

void main()
{
	ex_TexCoord = in_TexCoord;
	vec4 worldPos = uboDynamic.model * vec4(in_Position, 1);
	
	gl_Position = (pushConstants.proj * pushConstants.view) * worldPos;
}