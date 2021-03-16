#version 450

#define NUM_POINT_LIGHTS 8

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (constant_id = 2) const int QUALITY_LEVEL = 1;
layout (constant_id = 3) const int NUM_CASCADES = 4;

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec4 in_Colour;
layout (location = 3) in vec3 in_Normal;

layout (location = 0) out vec2 ex_TexCoord;
layout (location = 1) out vec4 ex_Colour;
layout (location = 2) out vec3 ex_NormalWS;
layout (location = 3) out vec3 ex_PositionWS;

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
	vec4 camPos;
	mat4 invView;
	mat4 viewProjection;
	mat4 invProj;
	DirectionalLight dirLight;
	PointLight pointLights[NUM_POINT_LIGHTS];
	ShadowSamplingData shadowSamplingData;
	SSAOSamplingData ssaoData;
	float zNear;
	float zFar;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

void main()
{
	ex_TexCoord = in_TexCoord;
	ex_Colour = in_Colour;

	ex_NormalWS = mat3(uboDynamic.model) * in_Normal;

    vec4 worldPos = uboDynamic.model * vec4(in_Position, 1.0);
	ex_PositionWS = worldPos.xyz;
    gl_Position = uboConstant.viewProjection * worldPos;
}
