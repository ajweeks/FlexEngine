#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 ex_Colour;
layout (location = 1) in vec2 ex_TexCoord;

layout (location = 0) out vec4 fragmentColour;

void main()
{
	fragmentColour = ex_Colour;
	// TODO: Use DPI/arc length-independent edge width
	// Antialias edges
	float edgeU = smoothstep(1.0-abs(ex_TexCoord.x-0.5)*2.0, 0.0, 0.03);
	float edgeV = smoothstep(1.0-abs(ex_TexCoord.y-0.5)*2.0, 0.0, 0.003);
	fragmentColour.a = min(edgeU, edgeV);
}
