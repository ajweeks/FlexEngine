#version 450

// Deferred PBR

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
} uboConstant;

// Updated once per object
layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	// Constant values to use when not using samplers
	vec4 constAlbedo;
	float constMetallic;
	float constRoughness;
	float constAO;

	// PBR samplers
	bool enableAlbedoSampler;
	bool enableMetallicSampler;
	bool enableRoughnessSampler;
	bool enableAOSampler;
	bool enableNormalSampler;
} uboDynamic;

layout (location = 0) in vec3 ex_WorldPos;
layout (location = 1) in vec2 ex_TexCoord;
layout (location = 2) in vec4 ex_Color;
layout (location = 3) in mat3 ex_TBN;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2D metallicSampler;
layout (binding = 4) uniform sampler2D roughnessSampler;
layout (binding = 5) uniform sampler2D aoSampler;
layout (binding = 6) uniform sampler2D normalSampler;

layout (location = 0) out vec4 outPositionMetallic;
layout (location = 1) out vec4 outNormalRoughness;
layout (location = 2) out vec4 outAlbedoAO;

void main() 
{
	vec3 albedo = uboDynamic.enableAlbedoSampler ? texture(albedoSampler, ex_TexCoord).rgb : vec3(uboDynamic.constAlbedo);
	float metallic = uboDynamic.enableMetallicSampler ? texture(metallicSampler, ex_TexCoord).r : uboDynamic.constMetallic;
	float roughness = uboDynamic.enableRoughnessSampler ? texture(roughnessSampler, ex_TexCoord).r : uboDynamic.constRoughness;
	float ao = uboDynamic.enableAOSampler ? texture(aoSampler, ex_TexCoord).r : uboDynamic.constAO;
	vec3 Normal = normalize(uboDynamic.enableNormalSampler ? (ex_TBN * (texture(normalSampler, ex_TexCoord).xyz * 2 - 1)) : ex_TBN[2]);

	outPositionMetallic.rgb = ex_WorldPos;
	outPositionMetallic.a = metallic;

	outNormalRoughness.rgb = normalize(Normal);
	outNormalRoughness.a = roughness;
	
	outAlbedoAO.rgb = albedo * vec3(ex_Color);
	outAlbedoAO.a = ao;
}
