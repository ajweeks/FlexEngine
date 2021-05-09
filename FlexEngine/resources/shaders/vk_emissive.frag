#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define NUM_POINT_LIGHTS 8

layout (constant_id = 2) const int QUALITY_LEVEL = 1;
layout (constant_id = 3) const int NUM_CASCADES = 4;

struct DirectionalLight 
{
	vec3 direction;
	int enabled;
	vec3 colour;
	float brightness;
	int castShadows;
	float shadowDarkness;
	vec2 _pad;
};

struct PointLight 
{
	vec3 position;
	int enabled;
	vec3 colour;
	float brightness;
};

struct ShadowSamplingData
{
	mat4 cascadeViewProjMats[NUM_CASCADES];
	vec4 cascadeDepthSplits;
};

struct SSAOSamplingData
{
	int enabled; // TODO: Make specialization constant
	float powExp;
	vec2 _pad;
};

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;

	vec4 constAlbedo;
	vec4 constEmissive;
	float constRoughness;

	bool enableAlbedoSampler;
	bool enableEmissiveSampler;
	bool enableNormalSampler;
} uboDynamic;

layout (location = 0) in vec2 ex_TexCoord;
layout (location = 1) in vec4 ex_Colour;
layout (location = 2) in mat3 ex_TBN;

layout (binding = 2) uniform sampler2D albedoSampler;
layout (binding = 3) uniform sampler2D emissiveSampler;
layout (binding = 4) uniform sampler2D normalSampler;

layout (location = 0) out vec4 outColour;

void main() 
{
	vec3 albedo = uboDynamic.enableAlbedoSampler ? texture(albedoSampler, ex_TexCoord).rgb : uboDynamic.constAlbedo.xyz;
	vec3 emissive = uboDynamic.enableEmissiveSampler ? texture(emissiveSampler, ex_TexCoord).rgb : uboDynamic.constEmissive.xyz;
	float roughness = uboDynamic.constRoughness;
	vec3 N = uboDynamic.enableNormalSampler ? (ex_TBN * (texture(normalSampler, ex_TexCoord).xyz * 2 - 1)) : ex_TBN[2];

	N = normalize(mat3(uboConstant.view) * N);

	outColour = vec4(albedo * emissive * ex_Colour.xyz, 1.0);
}
