#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec4 in_Colour;
layout (location = 2) in vec3 in_Normal;

layout (location = 0) out vec3 ex_PositionWS;
layout (location = 1) out vec4 ex_Colour;
layout (location = 2) out vec3 ex_NormalWS;

struct DirectionalLight 
{
	vec3 direction;
	int enabled;
	vec3 color;
	float brightness;
	int castShadows;
	float shadowDarkness;
	vec2 _dirLightPad;
};

struct OceanColours
{
	vec4 top;
	vec4 mid;
	vec4 btm;
	float fresnelFactor;
	float fresnelPower;
};

layout (binding = 0) uniform UBOConstant
{
	vec4 camPos;
	mat4 view;
	mat4 projection;
	DirectionalLight dirLight;
	OceanColours oceanColours;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

void main()
{
    ex_NormalWS = mat3(uboDynamic.model) * in_Normal;
    vec4 posWS = uboDynamic.model * vec4(in_Position, 0.0);
    ex_PositionWS = posWS.xyz;
    gl_Position = (uboConstant.projection * uboConstant.view) * vec4(posWS.xyz, 1.0);
    ex_Colour = in_Colour;
}
