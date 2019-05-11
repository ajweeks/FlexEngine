#version 450

layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
} uboConsant;

layout (binding = 1) uniform UBODynamic
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
	vec4 worldPos = vec4(in_Position, 1) * uboDynamic.model;
	
	gl_Position = uboConsant.viewProjection * worldPos;
}