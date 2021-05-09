#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 in_Position2D;
layout (location = 1) in vec2 in_TexCoord;
layout (location = 2) in vec4 in_Colour;

layout (binding = 0) uniform UBODynamic
{
	mat4 model;
	vec4 colourMultiplier;
	vec2 uvBlendAmount;
	vec2 _pad;
} uboDynamic;

layout (location = 0) out vec4 ex_Colour;
layout (location = 1) out vec2 ex_TexCoord;

void main() 
{
	gl_Position = uboDynamic.model * vec4(in_Position2D, 0.0, 1.0);

	ex_Colour = in_Colour * uboDynamic.colourMultiplier;
	ex_TexCoord = in_TexCoord;
}
