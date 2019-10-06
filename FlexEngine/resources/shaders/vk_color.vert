#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec4 in_Color;

layout (binding = 0) uniform UBOConstant
{
	mat4 viewProjection;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
	vec4 colorMultiplier;
} uboDynamic;

layout (location = 0) out vec4 outColor;

out gl_PerVertex 
{
	vec4 gl_Position;
};

void main() 
{
	gl_Position = uboConstant.viewProjection * uboDynamic.model * vec4(in_Position, 1.0);

	outColor = in_Color * uboDynamic.colorMultiplier;
}
