#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 ex_Colour;
layout (location = 1) in vec2 ex_TexCoord;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 colourMultiplier;
	vec2 uvBlendAmount;
	vec2 _pad;
} uboDynamic;

layout (location = 0) out vec4 fragmentColour;

void main()
{
	fragmentColour = ex_Colour;
	// Anti-alias edges
	vec2 edges = clamp((vec2(1.0)-abs(ex_TexCoord-vec2(0.5))*vec2(2.0)) / uboDynamic.uvBlendAmount, vec2(0.0), vec2(1.0));
	fragmentColour.a *= min(edges.x, edges.y);
}
