#version 450

// Deferred PBR Combine

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;

layout (location = 0) out vec3 ex_WorldPosition;

// TODO: Either move this to separate file or figure out how to remove it from this one
struct DirectionalLight 
{
	vec4 direction;
	vec4 color;
	bool enabled;
};

struct PointLight 
{
	vec4 position;
	vec4 color;
	bool enabled;
};
#define NUMBER_POINT_LIGHTS 4

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
	vec4 camPos;
	DirectionalLight dirLight;
	PointLight pointLights[NUMBER_POINT_LIGHTS];
} uboConstant;

// Updated once per object
layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

void main()
{
    ex_WorldPosition = in_Position;

    vec4 clipPos = uboConstant.viewProjection * uboDynamic.model * vec4(in_Position, 1.0);
	gl_Position = clipPos.xyww;
}
