#version 450 

layout (location = 0) in vec3 in_Position;
layout (location = 1) in vec2 in_TexCoord;

layout (location = 0) out vec2 ex_TexCoord;

layout (binding = 0) uniform UBOConstant
{
	vec4 charResXYspreadZsampleDensityW;
	int texChannel;
} uboConstant;

void main()
{
	ex_TexCoord = in_TexCoord;
	vec2 adjustment = vec2(uboConstant.charResXYspreadZsampleDensityW.xy * 2.0) / uboConstant.charResXYspreadZsampleDensityW.z;
	ex_TexCoord -= vec2(0.5);
	ex_TexCoord *= (vec2(1.0) + adjustment);
	ex_TexCoord += vec2(0.5);
	gl_Position = vec4(in_Position, 1.0);
}