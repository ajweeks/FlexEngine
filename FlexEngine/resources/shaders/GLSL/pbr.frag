#version 400

// Deferred PBR

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 ex_WorldPos;
layout (location = 1) in mat3 ex_TBN;
layout (location = 4) in vec2 ex_TexCoord;

layout (location = 0) out vec4 outPositionMetallic;
layout (location = 1) out vec4 outNormalRoughness;
layout (location = 2) out vec4 outAlbedoAO;

// Material variables
uniform vec4 constAlbedo;
uniform bool enableAlbedoSampler;
layout (binding = 0) uniform sampler2D albedoSampler;

uniform float constMetallic;
uniform bool enableMetallicSampler;
layout (binding = 1) uniform sampler2D metallicSampler;

uniform float constRoughness;
uniform bool enableRoughnessSampler;
layout (binding = 2) uniform sampler2D roughnessSampler;

uniform float constAO;
uniform bool enableAOSampler;
layout (binding = 3) uniform sampler2D aoSampler;

uniform bool enableNormalSampler;
layout (binding = 4) uniform sampler2D normalSampler;

void main() 
{
	vec3 albedo = enableAlbedoSampler ? texture(albedoSampler, ex_TexCoord).rgb : vec3(constAlbedo);
	float metallic = enableMetallicSampler ? texture(metallicSampler, ex_TexCoord).r : constMetallic;
	float roughness = enableRoughnessSampler ? texture(roughnessSampler, ex_TexCoord).r : constRoughness;
	float ao = enableAOSampler ? texture(aoSampler, ex_TexCoord).r : constAO;
	vec3 Normal = enableNormalSampler ? (ex_TBN * (texture(normalSampler, ex_TexCoord).xyz * 2 - 1)) : ex_TBN[2];
	
	outPositionMetallic.rgb = ex_WorldPos;
	outPositionMetallic.a = metallic;

	outNormalRoughness.rgb = normalize(Normal);
	outNormalRoughness.a = roughness;
	
	outAlbedoAO.rgb = albedo;
	outAlbedoAO.a = ao;
}
