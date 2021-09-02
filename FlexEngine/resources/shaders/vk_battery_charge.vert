#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;

layout (location = 0) out vec2 ex_TexCoord;

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
	float time;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	vec4 constAlbedo;
	vec4 constEmissive;

	bool enableAlbedoSampler;
	bool enableEmissiveSampler;

	float chargeAmount;
} uboDynamic;

void main()
{
	ex_TexCoord = in_TexCoord;

    vec4 worldPos = uboDynamic.model * vec4(in_Position, 1.0);
    gl_Position = uboConstant.viewProjection * worldPos;
}
