#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0) uniform UBOConstant
{
	mat4 view;
	mat4 viewProjection;
	float time;
} uboConstant;

layout (binding = 1) uniform UBODynamic
{
	mat4 model;
	vec4 constEmissive;
} uboDynamic;

layout (location = 0) in vec3 ex_PositionWS;
layout (location = 1) in vec3 ex_NormalWS;

layout (location = 0) out vec4 outColour;

void main() 
{
	vec4 emissive = uboDynamic.constEmissive;
	vec3 N = normalize(mat3(uboConstant.view) * ex_NormalWS);

	float wavelen = 9.0;
	float waveSpeed = 7.0;
	float pulseSpeed = 4.5;
	float pulse = sin((-ex_PositionWS.y + N.y * 0.5) * wavelen + uboConstant.time * waveSpeed) * 0.5 + 0.5;
	pulse = pulse * 0.4 + 1.0;
	float pulse2 = sin(uboConstant.time * pulseSpeed) * 0.2  + 1.0;

	outColour.rgb = emissive.rgb * (dot(N, vec3(0.577, 0.577, 0.577)) * 0.3 + 0.7);
	outColour.rgb *= pulse;
	outColour.rgb *= pulse2;
	outColour.a = emissive.a;
}
