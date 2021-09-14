#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "vk_misc.glsl"

layout (location = 0) in vec3 in_Position;

layout (location = 0) out vec4 ex_PositionOS;

// Updated once per frame
layout (binding = 0) uniform UBOConstant
{
	mat4 viewInv;
	mat4 viewProjection;
	SkyboxData skyboxData;
	float time;
	vec4 screenSize; // (w, h, 1/w, 1/h)
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
} uboDynamic;

void main()
{
    ex_PositionOS.xyz = mat3(uboDynamic.model) * in_Position;
    vec4 positionWS = uboDynamic.model * vec4(in_Position, 1.0);
    gl_Position = uboConstant.viewProjection * positionWS;
}
