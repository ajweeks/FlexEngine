#version 450

// Deferred PBR

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 ex_TexCoord;
layout (location = 1) in vec4 ex_Color;
layout (location = 2) in mat3 ex_TBN;

layout (location = 0) out vec4 outNormalRoughness;
layout (location = 1) out vec4 outAlbedoMetallic;

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

uniform bool enableNormalSampler;
layout (binding = 4) uniform sampler2D normalSampler;

uniform mat4 view;

void main() 
{
	vec3 albedo = enableAlbedoSampler ? texture(albedoSampler, ex_TexCoord).rgb * constAlbedo.rgb : constAlbedo.rgb;
	float metallic = enableMetallicSampler ? texture(metallicSampler, ex_TexCoord).r : constMetallic;
	float roughness = enableRoughnessSampler ? texture(roughnessSampler, ex_TexCoord).r : constRoughness;
	vec3 N = enableNormalSampler ? (ex_TBN * (texture(normalSampler, ex_TexCoord).xyz * 2 - 1)) : ex_TBN[0];
	
	N = normalize(mat3(view) * N);

	outNormalRoughness.rgb = N;
	outNormalRoughness.a = roughness;
	
	outAlbedoMetallic.rgb = albedo * ex_Color.rgb;
	outAlbedoMetallic.a = metallic;
}
