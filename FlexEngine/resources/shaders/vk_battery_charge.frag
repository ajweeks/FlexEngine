#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

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

layout (location = 0) in vec2 ex_TexCoord;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2D emissiveSampler;

layout (location = 0) out vec4 outColour;

void main() 
{
	vec3 albedo = uboDynamic.enableAlbedoSampler ? texture(albedoSampler, ex_TexCoord).rgb : uboDynamic.constAlbedo.xyz;
	
	vec3 emissive = uboDynamic.constEmissive.xyz;
	if (uboDynamic.enableEmissiveSampler)
	{
		emissive *= texture(emissiveSampler, ex_TexCoord).rgb;
	}

	float blend = 0.0005;
	float dist = clamp(uboDynamic.chargeAmount - ex_TexCoord.y, 0.0, 1.0);
	float d = clamp(smoothstep(0.0, blend, dist) * 10.0, 0.0, 1.0);

	outColour = vec4(mix(albedo, emissive, d), 1.0);
}
