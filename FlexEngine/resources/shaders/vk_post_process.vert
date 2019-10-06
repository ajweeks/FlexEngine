#version 450

layout (binding = 0) uniform UBODynamic
{
	vec4 colorMultiplier;
	mat4 contrastBrightnessSaturation;
} uboDynamic;

layout (location = 0) in vec2 in_Position2D;
layout (location = 1) in vec2 in_TexCoord;

layout (location = 0) out vec2 ex_TexCoord;

void main()
{
	ex_TexCoord = in_TexCoord;
	gl_Position = vec4(in_Position2D, 0, 1);
}
