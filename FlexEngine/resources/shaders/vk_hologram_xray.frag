#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// The parts of a hologram which are behind the scene, drawn as scrolling dashes. The pipeline's depth test (the
// opposite of the regular one) keeps this to the fragments the regular hologram hides.

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
	float pulse = sin(uboConstant.time * 4.5) * 0.2 + 1.0;

	float stripe = fract((gl_FragCoord.x + gl_FragCoord.y) / 16.0 - uboConstant.time * 0.7);
	if (stripe > 0.5)
	{
		discard;
	}
	outColour.rgb = emissive.rgb * pulse;
	outColour.a = min(1.0, emissive.a * 2.0);
}
