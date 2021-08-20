#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
	vec4 constEmissive;
} uboDynamic;

layout (location = 0) in vec3 ex_NormalWS;

layout (location = 0) out vec4 outColour;

void main() 
{
	vec4 emissive = uboDynamic.constEmissive;
	vec3 N = normalize(mat3(uboConstant.view) * ex_NormalWS);

	outColour.rgb = emissive.rgb * (dot(N, vec3(0.577, 0.577, 0.577)) * 0.3 + 0.7);
	outColour.a = emissive.a;
}
